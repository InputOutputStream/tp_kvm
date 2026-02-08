#include "../include/vm_operations.hpp"
#include "../include/utils.hpp"
#include "../include/validation.hpp"
#include "../include/remote_executor.hpp"
#include "../include/host_manager.hpp"
#include "../include/baseimage_manager.hpp"
#include "../include/isolation_utils.hpp"
#include "../include/network_proxy_service.hpp"

#include <regex>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include <libvirt/virterror.h>


// Initialize in VMOperations constructor

VMOperations::VMOperations(virConnectPtr connection, HostManager *hostMgr, 
    NetworkManager *netMgr, ResourceMetadataStore *g_metadataStore, NetworkProxyService *g_proxyService)
 : conn(connection), hostManager(hostMgr), networkManager(netMgr), 
 g_metadataStore(g_metadataStore), g_proxyService(g_proxyService) 
 {
    
    // Initialize metadata store
    if (!g_metadataStore) {
        g_metadataStore = new ResourceMetadataStore();
    }
    
    // Initialize proxy service
    if (!g_proxyService && hostMgr) {
        // Get libvirt host address
        char* hostname = virConnectGetHostname(connection);
        std::string hostAddr = hostname ? hostname : "localhost";
        free(hostname);
        
        RemoteExec::RemoteExecutor* remoteExec = new RemoteExec::RemoteExecutor(connection);
        g_proxyService = new NetworkProxyService(remoteExec, hostAddr);
    }
}

/**
 * @brief Deploy VM with proper isolation and metadata tracking
 */
bool VMOperations::deployVM(const json& vmParams) {
    try {
        // =========================
        // 1) Isolation & Metadata
        // =========================
        std::string username = vmParams["username"].get<std::string>();
        std::string displayName = vmParams.value("vmName", "my-instance");

        if (displayName.length() > 50) {
            displayName = displayName.substr(0, 50);
        }

        // Generate resource ID (AWS-style)
        std::string resourceID = ResourceIDGenerator::generateVMID();

        // Create internal libvirt name
        std::string internalName =
            IsolatedResourceNaming::createInternalVMName(username, resourceID);

        std::cout << "Deploying VM:" << std::endl;
        std::cout << "  User: " << username << std::endl;
        std::cout << "  Display Name: " << displayName << std::endl;
        std::cout << "  Resource ID: " << resourceID << std::endl;
        std::cout << "  Internal Name: " << internalName << std::endl;

        // Register resource BEFORE deployment
        json additionalData = {
            {"flavor", vmParams.value("flavor", "")},
            {"baseImage", vmParams.value("baseImage", "")},
            {"network", vmParams.value("network", "")}
        };

        g_metadataStore->registerResource(
            resourceID,
            internalName,
            displayName,
            username,
            "vm",
            additionalData
        );

        // Create modified params with internal name
        json internalParams = vmParams;
        internalParams["hostname"] = internalName;
        internalParams["vmName"]   = internalName;

        // =========================
        // 2) Host Selection
        // =========================
        auto hostSelection = selectOptimalHost(internalParams);
        if (!hostSelection.connection) {
            return false;
        }

        // Update connection if host was selected
        if (!hostSelection.hostname.empty()) {
            conn = hostSelection.connection;
        }

        // Create remote executor
        RemoteExec::RemoteExecutor remoteExec(conn);
        fprintf(stdout, "📡 Target Host: %s\n\n", remoteExec.getHostInfo().c_str());

        // =========================
        // 3) Validation Phase
        // =========================
        if (!validateConnection()) return false;
        if (!validateInputParameters(internalParams)) return false;

        std::string hostname   = internalParams["hostname"];
        std::string baseImageId = internalParams.value("baseImage", "");

        if (!validateVMNameAvailability(hostname)) return false;
        if (!validateRemoteDirectories(remoteExec)) return false;
        if (!validateRemoteTools(remoteExec)) return false;
        if (!validateBaseImage(remoteExec, baseImageId)) return false;
        if (!validateDiskSpace(remoteExec, internalParams["disk"])) return false;
        if (!validateNetwork()) return false;

        // Print configuration
        printConfiguration(internalParams);

        // =========================
        // 4) Cloud-init
        // =========================
        auto cloudInitConfig = createCloudInitConfig(internalParams);

        // Auto-select base image if not specified
        if (baseImageId.empty()) {
            BaseImageManager imageManager(&remoteExec);
            auto imagesList = imageManager.listImages();

            if (imagesList["count"].get<int>() == 0) {
                fprintf(stderr, "❌ No base images available on target host\n");
                return false;
            }

            baseImageId = imagesList["images"][0]["id"];
            fprintf(stdout, "ℹ️  No base image specified, using: %s\n",
                    imagesList["images"][0]["displayName"].get<std::string>().c_str());
        }

        // Handle password hashing if needed
        if (internalParams.value("authMethod", "password") == "password" &&
            internalParams.contains("password") &&
            !internalParams["password"].get<std::string>().empty()) {

            std::string hashedPassword = hashPassword(remoteExec, internalParams["password"]);
            if (hashedPassword.empty()) {
                fprintf(stderr, "   ❌ Failed to generate password hash on target host\n");
                return false;
            }

            // Insert password into user-data
            size_t pos = cloudInitConfig.userData.find("    shell: /bin/bash\n");
            if (pos != std::string::npos) {
                std::string passwordSection =
                    "    passwd: " + hashedPassword + "\n" +
                    "    lock_passwd: false\n";
                cloudInitConfig.userData.insert(pos + 24, passwordSection);
            }
        }

        // =========================
        // 5) Deployment Phase
        // =========================
        if (!writeCloudInitFiles(remoteExec, hostname, cloudInitConfig)) return false;
        if (!createCloudInitISO(remoteExec, hostname)) return false;
        if (!copyBaseImage(remoteExec, hostname, baseImageId)) return false;
        if (!resizeDisk(remoteExec, hostname, internalParams["disk"])) return false;

        fprintf(stdout, "Step 5/7: Creating VM definition...\n");
        std::string xml = generateDomainXML(internalParams);
        fprintf(stdout, "   ✅ VM definition created\n");

        virDomainPtr domain = defineVM(xml);
        if (!domain) return false;

        bool started = startVM(domain);
        virDomainFree(domain);

        // =========================
        // 6) Metadata Update
        // =========================
        if (started) {
            g_metadataStore->updateResource(resourceID, username, {
                {"status", "running"}
            });
        }

        return started;

    } catch (const std::exception& e) {
        fprintf(stderr, "\n❌ Exception during deployment: %s\n", e.what());
        return false;
    }
}

/**
 * @brief List VMs for a specific user (with isolation)
 */
json VMOperations::listUserVMs(const std::string& userId) {
    json result;
    result["success"] = false;
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    // Get all VMs from libvirt
    virDomainPtr* domains;
    int numDomains = virConnectListAllDomains(conn, &domains, 0);
    
    if (numDomains < 0) {
        result["error"] = "Error listing VMs";
        return result;
    }
    
    json vms = json::array();
    
    // Filter VMs that belong to this user
    for (int i = 0; i < numDomains; i++) {
        const char* internalName = virDomainGetName(domains[i]);
        
        // Check if user owns this VM
        if (IsolatedResourceNaming::isOwner(internalName, userId)) {
            // Extract resource ID
            std::string resourceID = IsolatedResourceNaming::extractResourceID(internalName);
            
            // Get metadata
            json metadata = g_metadataStore->getResource(resourceID, userId);
            if (metadata.contains("error")) {
                virDomainFree(domains[i]);
                continue;
            }
            
            // Get VM info
            virDomainInfo info;
            virDomainGetInfo(domains[i], &info);
            
            int id = virDomainGetID(domains[i]);
            std::string state = getStateString(info.state);
            bool isRunning = (info.state == VIR_DOMAIN_RUNNING);
            
            json vm = {
                {"id", resourceID},  // Show opaque ID, not internal name
                {"name", metadata["displayName"]},  // User-friendly name
                {"state", state},
                {"running", isRunning},
                {"created", metadata["created"]},
                {"tags", metadata.value("tags", json::object())},
                {"stats", nullptr}
            };
            
            if (isRunning) {
                vm["stats"] = getVMStatsInternal(domains[i], internalName);
            }
            
            vms.push_back(vm);
        }
        
        virDomainFree(domains[i]);
    }
    
    free(domains);
    
    result["success"] = true;
    result["vms"] = vms;
    result["count"] = vms.size();
    
    return result;
}


bool VMOperations::performVMAction(const std::string& resourceID, std::string userId, std::string action)
{

}

/**
 * @brief Get VM IP with network proxy support
 */
json VMOperations::getVMIP(const std::string& resourceID, const std::string& username) {
    json result;
    result["success"] = false;
    
    // Get internal name from resource ID
    std::string internalName = g_metadataStore->getInternalName(resourceID, username);
    if (internalName.empty()) {
        result["error"] = "VM not found or access denied";
        return result;
    }
    
    // Get metadata to find network
    json metadata = g_metadataStore->getResource(resourceID, username);
    std::string networkName = metadata["data"].value("network", "default");
    
    // Use proxy service to get IP
    if (g_proxyService) {
        json ipResult = g_proxyService->getVMIP(internalName, networkName);
        if (ipResult["success"].get<bool>()) {
            result["success"] = true;
            result["privateIP"] = ipResult["ip"];
            result["method"] = ipResult["method"];
            return result;
        }
    }
    
    result["error"] = "Could not determine VM IP";
    return result;
}

/**
 * @brief Get VNC info with tunnel creation
 */
json VMOperations::getVNCInfo(const std::string& resourceID, const std::string& username) {
    json result;
    result["success"] = false;
    
    // Get internal name
    std::string internalName = g_metadataStore->getInternalName(resourceID, username);
    if (internalName.empty()) {
        result["error"] = "VM not found or access denied";
        return result;
    }
    
    // Get VM domain
    virDomainPtr domain = virDomainLookupByName(conn, internalName.c_str());
    if (!domain) {
        result["error"] = "VM not found in libvirt";
        return result;
    }
    
    // Check if running
    virDomainInfo info;
    if (virDomainGetInfo(domain, &info) < 0 || info.state != VIR_DOMAIN_RUNNING) {
        virDomainFree(domain);
        result["error"] = "VM is not running";
        return result;
    }
    
    // Get VNC port from XML
    char* xmlDesc = virDomainGetXMLDesc(domain, 0);
    if (!xmlDesc) {
        virDomainFree(domain);
        result["error"] = "Failed to get VM XML";
        return result;
    }
    
    std::string xml(xmlDesc);
    free(xmlDesc);
    virDomainFree(domain);
    
    // Extract VNC port
    std::regex vncRegex("<graphics type='vnc'[^>]*port='(\\d+)'");
    std::smatch match;
    
    int vncPort = -1;
    if (std::regex_search(xml, match, vncRegex)) {
        vncPort = std::stoi(match[1].str());
    }
    
    if (vncPort == -1) {
        result["error"] = "VNC not configured";
        return result;
    }
    
    // Get VM IP
    json ipResult = getVMIP(resourceID, username);
    if (!ipResult["success"].get<bool>()) {
        result["error"] = "Could not get VM IP for VNC tunnel";
        return result;
    }
    
    std::string vmIP = ipResult["privateIP"];
    
    // Create VNC tunnel using proxy service
    if (g_proxyService) {
        json tunnelResult = g_proxyService->createVNCTunnel(
            internalName, vmIP, vncPort, username
        );
        
        if (tunnelResult["success"].get<bool>()) {
            result["success"] = true;
            result["vncURL"] = tunnelResult["vncURL"];
            result["novncURL"] = tunnelResult["novncURL"];
            result["tunnelID"] = tunnelResult["tunnelID"];
            result["message"] = "VNC tunnel created. Use the provided URL to connect.";
            return result;
        } else {
            result["error"] = "Failed to create VNC tunnel: " + 
                             tunnelResult["error"].get<std::string>();
            return result;
        }
    }
    
    result["error"] = "Proxy service not available";
    return result;
}

/**
 * @brief Start VM (with resource ID)
 */
bool VMOperations::startVM(const std::string& resourceID, const std::string& username) {
    // Get internal name
    std::string internalName = g_metadataStore->getInternalName(resourceID, username);
    if (internalName.empty()) {
        return false;
    }
    
    virDomainPtr domain = virDomainLookupByName(conn, internalName.c_str());
    if (!domain) {
        return false;
    }
    
    int ret = virDomainCreate(domain);
    virDomainFree(domain);
    
    if (ret == 0) {
        g_metadataStore->updateResource(resourceID, username, {
            {"status", "running"}
        });
    }
    
    return ret == 0;
}

/**
 * @brief Stop VM (with resource ID)
 */
bool VMOperations::shutdownVM(const std::string& resourceID, const std::string& username) {
    std::string internalName = g_metadataStore->getInternalName(resourceID, username);
    if (internalName.empty()) {
        return false;
    }
    
    virDomainPtr domain = virDomainLookupByName(conn, internalName.c_str());
    if (!domain) {
        return false;
    }
    
    int ret = virDomainShutdown(domain);
    virDomainFree(domain);
    
    if (ret == 0) {
        g_metadataStore->updateResource(resourceID, username, {
            {"status", "stopping"}
        });
    }
    
    return ret == 0;
}

/**
 * @brief Delete VM (with cleanup)
 */
json VMOperations::deleteVM(const std::string& resourceID, 
                           const std::string& username, 
                           bool removeDisks) {
    json result;
    result["success"] = false;
    
    // Get internal name
    std::string internalName = g_metadataStore->getInternalName(resourceID, username);
    if (internalName.empty()) {
        result["error"] = "VM not found or access denied";
        return result;
    }
    
    virDomainPtr domain = virDomainLookupByName(conn, internalName.c_str());
    if (!domain) {
        // VM doesn't exist in libvirt, but might be in metadata
        g_metadataStore->deleteResource(resourceID, username);
        result["success"] = true;
        result["message"] = "VM removed from system (not found in libvirt)";
        return result;
    }
    
    // Stop if running
    stopVMIfRunning(domain);
    
    // Delete snapshots
    deleteAllSnapshots(domain);
    
    // Get disk paths before undefining
    std::vector<std::string> diskPaths;
    if (removeDisks) {
        diskPaths = getDiskPaths(domain);
    }
    
    // Undefine VM
    if (virDomainUndefine(domain) < 0) {
        virDomainFree(domain);
        result["error"] = "Failed to undefine VM";
        return result;
    }
    
    virDomainFree(domain);
    
    // Delete disk files
    if (removeDisks && !diskPaths.empty()) {
        deleteDiskFiles(diskPaths);
    }
    
    // Remove from metadata
    g_metadataStore->deleteResource(resourceID, username);
    
    result["success"] = true;
    result["message"] = "VM deleted successfully";
    return result;
}

/**
 * @brief Create port forward for VM application access
 */
json VMOperations::createPortForward(const std::string& resourceID,
                                    const std::string& username,
                                    int vmPort,
                                    const std::string& protocol) {
    json result;
    result["success"] = false;
    
    if (!g_proxyService) {
        result["error"] = "Proxy service not available";
        return result;
    }
    
    // Get internal name and IP
    std::string internalName = g_metadataStore->getInternalName(resourceID, username);
    if (internalName.empty()) {
        result["error"] = "VM not found or access denied";
        return result;
    }
    
    json ipResult = getVMIP(resourceID, username);
    if (!ipResult["success"].get<bool>()) {
        result["error"] = "Could not get VM IP";
        return result;
    }
    
    std::string vmIP = ipResult["privateIP"];
    
    // Create port forward
    return g_proxyService->createPortForward(
        internalName, vmIP, vmPort, protocol, username
    );
}

/**
 * @brief List port forwards for user's VMs
 */
json VMOperations::listPortForwards(const std::string& username) {
    if (!g_proxyService) {
        return {{"error", "Proxy service not available"}};
    }
    
    return g_proxyService->listTunnels(username);
}

/**
 * @brief Delete port forward
 */
bool VMOperations::deletePortForward(const std::string& forwardID, 
                                    const std::string& username) {
    if (!g_proxyService) {
        return false;
    }
    
    return g_proxyService->deleteTunnel(forwardID, username);
}

/**
 * @brief Update VM display name or tags
 */
json VMOperations::updateVMMetadata(const std::string& resourceID,
                                   const std::string& username,
                                   const json& updates) {
    json result;
    result["success"] = false;
    
    if (!g_metadataStore->checkOwnership(resourceID, username)) {
        result["error"] = "VM not found or access denied";
        return result;
    }
    
    if (g_metadataStore->updateResource(resourceID, username, updates)) {
        result["success"] = true;
        result["message"] = "VM metadata updated";
    } else {
        result["error"] = "Failed to update metadata";
    }
    
    return result;
}


json VMOperations::listSnapshots(const std::string& name) {
    json result;
    result["success"] = false;
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) {
        result["error"] = "VM not found";
        return result;
    }
    
    virDomainSnapshotPtr* snapshots;
    int numSnapshots = virDomainListAllSnapshots(domain, &snapshots, 0);
    
    json snapshotList = json::array();
    
    if (numSnapshots >= 0) {
        for (int i = 0; i < numSnapshots; i++) {
            const char* snapName = virDomainSnapshotGetName(snapshots[i]);
            char* xmlDesc = virDomainSnapshotGetXMLDesc(snapshots[i], 0);
            
            std::string xml(xmlDesc);
            std::regex timeRegex("<creationTime>(\\d+)</creationTime>");
            std::regex stateRegex("<state>(\\w+)</state>");
            std::smatch match;
            
            std::string timeStr = "Unknown";
            if (std::regex_search(xml, match, timeRegex)) {
                time_t timestamp = std::stoll(match[1].str());
                char buffer[100];
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&timestamp));
                timeStr = buffer;
            }
            
            std::string state = "unknown";
            if (std::regex_search(xml, match, stateRegex)) {
                state = match[1].str();
            }
            
            snapshotList.push_back({
                {"name", snapName},
                {"creationTime", timeStr},
                {"state", state}
            });
            
            free(xmlDesc);
            virDomainSnapshotFree(snapshots[i]);
        }
        free(snapshots);
    }
    
    virDomainFree(domain);
    
    result["success"] = true;
    result["snapshots"] = snapshotList;
    return result;
}

bool VMOperations::createSnapshot(const std::string& name, const std::string& snapName, const std::string& desc) {
    if (!conn) return false;
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) return false;
    
    std::string snapshotXML = 
        "<domainsnapshot>"
        "<name>" + snapName + "</name>"
        "<description>" + desc + "</description>"
        "</domainsnapshot>";
    
    virDomainSnapshotPtr snapshot = virDomainSnapshotCreateXML(domain, snapshotXML.c_str(), 0);
    virDomainFree(domain);
    
    if (snapshot) {
        virDomainSnapshotFree(snapshot);
        return true;
    }
    return false;
}

bool VMOperations::revertSnapshot(const std::string& name, const std::string& snapName) {
    if (!conn) return false;
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) return false;
    
    virDomainSnapshotPtr snapshot = virDomainSnapshotLookupByName(domain, snapName.c_str(), 0);
    if (!snapshot) {
        virDomainFree(domain);
        return false;
    }
    
    int result = virDomainRevertToSnapshot(snapshot, 0);
    
    virDomainSnapshotFree(snapshot);
    virDomainFree(domain);
    
    return result >= 0;
}

bool VMOperations::deleteSnapshot(const std::string& name, const std::string& snapName) {
    if (!conn) return false;
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) return false;
    
    virDomainSnapshotPtr snapshot = virDomainSnapshotLookupByName(domain, snapName.c_str(), 0);
    if (!snapshot) {
        virDomainFree(domain);
        return false;
    }
    
    int result = virDomainSnapshotDelete(snapshot, 0);
    
    virDomainSnapshotFree(snapshot);
    virDomainFree(domain);
    
    return result >= 0;
}

bool VMOperations::cloneVM(const std::string& name, const std::string& cloneName) {
    if (!conn) return false;
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) return false;
    
    char* xmlDesc = virDomainGetXMLDesc(domain, 0);
    std::string xml(xmlDesc);
    free(xmlDesc);
    virDomainFree(domain);
    
    // Modify XML for clone
    xml = std::regex_replace(xml, std::regex("<name>" + name + "</name>"), "<name>" + cloneName + "</name>");
    xml = std::regex_replace(xml, std::regex("<uuid>.*?</uuid>"), "");
    
    // Copy disks
    std::regex diskRegex("<source file='([^']+)'");
    std::smatch match;
    std::string::const_iterator searchStart(xml.cbegin());
    
    while (std::regex_search(searchStart, xml.cend(), match, diskRegex)) {
        std::string oldPath = match[1].str();
        std::string newPath = std::regex_replace(oldPath, std::regex(name), cloneName);
        
        try {
            std::string cpCmd = "cp " + oldPath + " " + newPath;
            execCommand(cpCmd);
            
            xml = std::regex_replace(xml, std::regex(oldPath), newPath);
        } catch (...) {
            return false;
        }
        
        searchStart = match.suffix().first;
    }
    
    // Define new domain
    virDomainPtr newDomain = virDomainDefineXML(conn, xml.c_str());
    if (!newDomain) {
        return false;
    }
    
    virDomainFree(newDomain);
    return true;
}


// ========================================
// HELPER METHODS FOR VM DELETION
// ========================================

bool VMOperations::stopVMIfRunning(virDomainPtr domain) {
    if (!domain) return false;
    
    virDomainInfo info;
    if (virDomainGetInfo(domain, &info) < 0) {
        fprintf(stderr, "Failed to get domain info\n");
        return false;
    }
    
    // If VM is running or paused, we need to stop it
    if (info.state == VIR_DOMAIN_RUNNING || info.state == VIR_DOMAIN_PAUSED) {
        fprintf(stdout, "VM is running, attempting graceful shutdown...\n");
        
        // Try graceful shutdown first
        if (virDomainShutdown(domain) == 0) {
            fprintf(stdout, "Shutdown signal sent, waiting up to 30 seconds...\n");
            
            // Wait up to 30 seconds for graceful shutdown
            for (int i = 0; i < GRACEFULL_SHUTDOWN_TIME; i++) {
                sleep(1);
                
                if (virDomainGetInfo(domain, &info) < 0) {
                    break;
                }
                
                if (info.state == VIR_DOMAIN_SHUTOFF) {
                    fprintf(stdout, "VM shutdown gracefully\n");
                    return true;
                }
            }
            
            fprintf(stdout, "Graceful shutdown timeout, forcing shutdown...\n");
        }
        
        // If graceful shutdown failed or timed out, force destroy
        if (virDomainDestroy(domain) < 0) {
            virErrorPtr err = virGetLastError();
            if (err) {
                fprintf(stderr, "Failed to destroy domain: %s\n", err->message);
            }
            return false;
        }
        
        fprintf(stdout, "VM forcefully stopped\n");
    }
    
    return true;
}

bool VMOperations::deleteAllSnapshots(virDomainPtr domain) {
    if (!domain) return false;
    
    virDomainSnapshotPtr* snapshots = nullptr;
    int numSnapshots = virDomainListAllSnapshots(domain, &snapshots, 0);
    
    if (numSnapshots < 0) {
        virErrorPtr err = virGetLastError();
        if (err) {
            fprintf(stderr, "Failed to list snapshots: %s\n", err->message);
        }
        return false;
    }
    
    if (numSnapshots == 0) {
        fprintf(stdout, "No snapshots to delete\n");
        return true;
    }
    
    fprintf(stdout, "Deleting %d snapshot(s)...\n", numSnapshots);
    
    bool allSuccess = true;
    for (int i = 0; i < numSnapshots; i++) {
        const char* snapName = virDomainSnapshotGetName(snapshots[i]);
        fprintf(stdout, "Deleting snapshot: %s\n", snapName);
        
        if (virDomainSnapshotDelete(snapshots[i], VIR_DOMAIN_SNAPSHOT_DELETE_METADATA_ONLY) < 0) {
            virErrorPtr err = virGetLastError();
            if (err) {
                fprintf(stderr, "Failed to delete snapshot %s: %s\n", snapName, err->message);
            }
            allSuccess = false;
        }
        
        virDomainSnapshotFree(snapshots[i]);
    }
    
    free(snapshots);
    
    if (allSuccess) {
        fprintf(stdout, "All snapshots deleted successfully\n");
    }
    
    return allSuccess;
}


// ========================================
// DELETE METHODS
// ========================================


bool VMOperations::deleteDiskFiles(const std::vector<std::string>& diskPaths) {
    if (diskPaths.empty()) {
        fprintf(stdout, "No disk files to delete\n");
        return true;
    }
    
    // Check if we're on remote host
    RemoteExec::RemoteExecutor remoteExec(conn);
    
    bool allSuccess = true;
    
    for (const auto& diskPath : diskPaths) {
        fprintf(stdout, "Deleting disk file: %s\n", diskPath.c_str());
        
        // Check if file exists (works for local and remote)
        if (!remoteExec.fileExists(diskPath)) {
            fprintf(stdout, "Disk file does not exist (already deleted?): %s\n", 
                    diskPath.c_str());
            continue;
        }
        
        // Delete file using remote executor
        std::string deleteCmd = "sudo rm -f \"" + diskPath + "\"";
        auto result = remoteExec.execute(deleteCmd);
        
        if (!result.success()) {
            fprintf(stderr, "Failed to delete disk file: %s (error: %s)\n", 
                    diskPath.c_str(), result.output.c_str());
            allSuccess = false;
        } else {
            fprintf(stdout, "Successfully deleted: %s\n", diskPath.c_str());
        }
    }
    
    return allSuccess;
}

bool VMOperations::deleteAllVMs(std::string& username)
{
    if (!conn) {
        return false;
    }
    
    virDomainPtr* domains;
    int numDomains = virConnectListAllDomains(conn, &domains, 0);
    
    if (numDomains < 0) {
        return false;
    }
    
    VMNameManager nameManager;
    json vms = json::array();
    
    for (int i = 0; i < numDomains; i++) {
        const char* name = virDomainGetName(domains[i]);
        
        // Check if VM belongs to user
        if (nameManager.isOwner(name, username)) {
            virDomainInfo info;
            virDomainGetInfo(domains[i], &info);
            
            int id = virDomainGetID(domains[i]);
            std::string state = getStateString(info.state);
            bool isRunning = (info.state == VIR_DOMAIN_RUNNING);
            
            // Parse name to get display name
            auto nameInfo = nameManager.parseVMName(name);
            std::string displayName = nameInfo.valid ? nameInfo.vmName : name;
            
           deleteVM(displayName, username, true);
        }
        virDomainFree(domains[i]);
    }
    
    free(domains);
    
    return true;    
}


bool VMOperations::undefineVM(const std::string& name) {
    if (!conn) return false;
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) return false;
    
    unsigned int undefineFlags = VIR_DOMAIN_UNDEFINE_MANAGED_SAVE | 
                                 VIR_DOMAIN_UNDEFINE_SNAPSHOTS_METADATA;
    
    int result = virDomainUndefineFlags(domain, undefineFlags);
    
    if (result < 0) {
        // Fallback to simple undefine
        result = virDomainUndefine(domain);
    }
    
    virDomainFree(domain);
    return result >= 0;
}


bool VMOperations::validateNetwork(const std::string& networkName, 
                                  const std::string& username) {
    if (!networkManager) return false;
    
    auto networks = networkManager->getUserNetworks(username);
    for (const auto& net : networks["networks"]) {
        if (net["networkName"] == networkName) {
            return true;
        }
    }
    
    // Check if it's default network
    return networkName == "default";
}
json VMOperations::getVMIP(const std::string& vmName) {
    json result;
    result["success"] = false;
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    virDomainPtr domain = virDomainLookupByName(conn, vmName.c_str());
    if (!domain) {
        result["error"] = "VM not found";
        return result;
    }
    
    // Check if VM is running
    virDomainInfo info;
    if (virDomainGetInfo(domain, &info) < 0 || info.state != VIR_DOMAIN_RUNNING) {
        result["error"] = "VM is not running";
        virDomainFree(domain);
        return result;
    }
    
    // Get IP addresses using libvirt's virDomainInterfaceAddresses
    virDomainInterfacePtr *ifaces = nullptr;
    int ifaces_count = virDomainInterfaceAddresses(domain, &ifaces, 
                                                   VIR_DOMAIN_INTERFACE_ADDRESSES_SRC_LEASE, 0);
    
    json interfaces = json::array();
    std::string primaryIP = "";
    
    if (ifaces_count > 0) {
        for (int i = 0; i < ifaces_count; i++) {
            virDomainInterfacePtr iface = ifaces[i];
            
            json ifaceInfo;
            ifaceInfo["name"] = iface->name ? iface->name : "";
            ifaceInfo["mac"] = iface->hwaddr ? iface->hwaddr : "";
            
            // Get IP addresses for this interface
            json addrs = json::array();
            for (unsigned int j = 0; j < iface->naddrs; j++) {
                virDomainIPAddressPtr addr = &iface->addrs[j];
                
                if (addr->type == VIR_IP_ADDR_TYPE_IPV4) {
                    std::string ip = addr->addr ? addr->addr : "";
                    addrs.push_back({
                        {"ip", ip},
                        {"prefix", addr->prefix},
                        {"type", "ipv4"}
                    });
                    
                    // Set primary IP (first IPv4 we find)
                    if (primaryIP.empty() && !ip.empty()) {
                        primaryIP = ip;
                    }
                }
            }
            
            ifaceInfo["addresses"] = addrs;
            if (!addrs.empty()) {
                ifaceInfo["ip"] = addrs[0]["ip"];
            }
            
            interfaces.push_back(ifaceInfo);
            
            virDomainInterfaceFree(iface);
        }
        free(ifaces);
    }
    
    // Fallback: Try DHCP leases if no IP found
    if (primaryIP.empty()) {
        // Get network name from XML
        char* xmlDesc = virDomainGetXMLDesc(domain, 0);
        if (xmlDesc) {
            std::string xml(xmlDesc);
            free(xmlDesc);
            
            // Extract network name
            std::regex netRegex("<source network='([^']+)'");
            std::smatch match;
            if (std::regex_search(xml, match, netRegex)) {
                std::string networkName = match[1].str();
                
                // Try to get DHCP lease
                virNetworkPtr network = virNetworkLookupByName(conn, networkName.c_str());
                if (network) {
                    virNetworkDHCPLeasePtr *leases = nullptr;
                    int nleases = virNetworkGetDHCPLeases(network, nullptr, &leases, 0);
                    
                    if (nleases > 0) {
                        // Find lease for this VM's MAC address
                        for (int i = 0; i < nleases; i++) {
                            if (leases[i]->ipaddr) {
                                primaryIP = leases[i]->ipaddr;
                                break;
                            }
                        }
                        
                        for (int i = 0; i < nleases; i++) {
                            virNetworkDHCPLeaseFree(leases[i]);
                        }
                        free(leases);
                    }
                    
                    virNetworkFree(network);
                }
            }
        }
    }
    
    virDomainFree(domain);
    
    result["success"] = true;
    result["primaryIP"] = primaryIP;
    result["interfaces"] = interfaces;
    result["vmName"] = vmName;
    
    return result;
}


// Helper methods

int VMOperations::findAvailablePort(int startPort, int endPort) {
    for (int port = startPort; port <= endPort; port++) {
        if (!isPortInUse(port)) {
            return port;
        }
    }
    return -1;
}

bool VMOperations::isPortInUse(int port) {
    RemoteExec::RemoteExecutor remoteExec(conn);
    
    std::stringstream cmd;
    cmd << "netstat -tuln | grep ':" << port << " ' || ss -tuln | grep ':" << port << " '";
    
    auto result = remoteExec.execute(cmd.str());
    
    // If output is not empty, port is in use
    return !result.output.empty();
}

std::string VMOperations::savePortForward(const std::string& vmName, const std::string& vmIP,
                                         int vmPort, int hostPort, const std::string& protocol) {
    std::string forwardsFile = "/var/lib/thoth-cloud/port_forwards.json";
    json allForwards = json::array();
    
    // Load existing forwards
    std::ifstream inFile(forwardsFile);
    if (inFile.is_open()) {
        try {
            inFile >> allForwards;
            inFile.close();
        } catch (const std::exception& e) {
            allForwards = json::array();
        }
    }
    
    // Generate forward ID
    std::string forwardId = vmName + "_" + std::to_string(vmPort) + "_" + 
                           std::to_string(getCurrentTimeMs());
    
    // Add new forward
    json newForward = {
        {"id", forwardId},
        {"vmName", vmName},
        {"vmIP", vmIP},
        {"vmPort", vmPort},
        {"hostPort", hostPort},
        {"protocol", protocol},
        {"created", getCurrentTimeMs()}
    };
    
    allForwards.push_back(newForward);
    
    // Save
    std::ofstream outFile(forwardsFile);
    if (outFile.is_open()) {
        outFile << allForwards.dump(2);
        outFile.close();
    }
    
    return forwardId;
}