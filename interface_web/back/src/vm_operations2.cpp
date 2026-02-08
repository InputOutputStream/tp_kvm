#include "../include/vm_operations.hpp"
#include "../include/utils.hpp"
#include "../include/validation.hpp"
#include "../include/remote_executor.hpp"
#include "../include/host_manager.hpp"
#include "../include/baseimage_manager.hpp"

#include <regex>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include <libvirt/virterror.h>

json VMOperations::getIP(const std::string& name) {
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
    
    // Check if domain is running
    virDomainInfo info;
    if (virDomainGetInfo(domain, &info) < 0 || info.state != VIR_DOMAIN_RUNNING) {
        result["error"] = "VM is not running";
        virDomainFree(domain);
        return result;
    }
    
    // Essayez d'abord l'agent QEMU Guest Agent (c'est le plus fiable)
    virDomainInterfacePtr *ifaces = NULL;
    int ifaces_count = virDomainInterfaceAddresses(domain, &ifaces,
                                                   VIR_DOMAIN_INTERFACE_ADDRESSES_SRC_AGENT, 0);
    
    // Si AGENT échoue, essayez LEASE (DHCP)
    if (ifaces_count <= 0) {
        ifaces_count = virDomainInterfaceAddresses(domain, &ifaces,
                                                   VIR_DOMAIN_INTERFACE_ADDRESSES_SRC_LEASE, 0);
    }
    
    // Si les deux échouent, ARP (moins fiable)
    if (ifaces_count <= 0) {
        ifaces_count = virDomainInterfaceAddresses(domain, &ifaces,
                                                   VIR_DOMAIN_INTERFACE_ADDRESSES_SRC_ARP, 0);
    }
    
    if (ifaces_count <= 0) {
        // Dernière tentative : essayer de récupérer via les baux DHCP du réseau
        result = getIPFromDHCPLeases(domain, name);
        virDomainFree(domain);
        return result;
    }
    
    json interfaces = json::array();
    bool foundIP = false;
    
    for (auto i = 0; i < ifaces_count; i++) {
        json iface;
        iface["name"] = ifaces[i]->name;
        iface["hwaddr"] = ifaces[i]->hwaddr ? ifaces[i]->hwaddr : "";
        
        json addrs = json::array();
        for (auto j = 0; j < ifaces[i]->naddrs; j++) {
            virDomainIPAddressPtr addr = &ifaces[i]->addrs[j];
            
            json addrInfo;
            addrInfo["type"] = (addr->type == VIR_IP_ADDR_TYPE_IPV4) ? "ipv4" : "ipv6";
            addrInfo["addr"] = addr->addr;
            addrInfo["prefix"] = addr->prefix;
            
            addrs.push_back(addrInfo);
            
            if (addr->addr && strlen(addr->addr) > 0 && 
                strcmp(addr->addr, "127.0.0.1") != 0) {
                foundIP = true;
            }
        }
        
        iface["addrs"] = addrs;
        interfaces.push_back(iface);
        virDomainInterfaceFree(ifaces[i]);
    }
    
    free(ifaces);
    virDomainFree(domain);
    
    if (!foundIP) {
        result["error"] = "No IP addresses found";
        return result;
    }
    
    result["success"] = true;
    result["interfaces"] = interfaces;
    
    // Extract primary IP
    for (const auto& iface : interfaces) {
        for (const auto& addr : iface["addrs"]) {
            if (addr["type"] == "ipv4" && addr["addr"] != "127.0.0.1") {
                result["primaryIP"] = addr["addr"];
                break;
            }
        }
        if (result.contains("primaryIP")) break;
    }
    
    return result;
}

json VMOperations::getIPFromDHCPLeases(virDomainPtr domain, const std::string& name) {
    json result;
    result["success"] = false;
    
    // Récupérer le XML de la VM pour trouver le réseau
    char* xmlDesc = virDomainGetXMLDesc(domain, 0);
    if (!xmlDesc) {
        result["error"] = "Failed to get VM XML";
        return result;
    }
    
    std::string xml(xmlDesc);
    free(xmlDesc);
    
    // Parser le XML pour trouver l'adresse MAC
    std::regex macRegex("<mac address='([^']+)'");
    std::smatch macMatch;
    std::string macAddress;
    
    if (std::regex_search(xml, macMatch, macRegex)) {
        macAddress = macMatch[1].str();
    }
    
    if (macAddress.empty()) {
        result["error"] = "Could not find MAC address in VM configuration";
        return result;
    }
    
    // Convertir MAC en format pour dnsmasq (minuscules, sans :)
    std::string macLower = macAddress;
    std::transform(macLower.begin(), macLower.end(), macLower.begin(), ::tolower);
    macLower.erase(std::remove(macLower.begin(), macLower.end(), ':'), macLower.end());
    
    // Essayez de lire le fichier dnsmasq.leases
    // Sur l'hôte distant, ce fichier est dans /var/lib/libvirt/dnsmasq/
    RemoteExec::RemoteExecutor remoteExec(conn);
    
    // Chercher dans différents chemins possibles
    std::vector<std::string> leasePaths = {
        "/var/lib/libvirt/dnsmasq/default.leases",
        "/var/lib/libvirt/dnsmasq/*.leases",
        "/var/lib/misc/dnsmasq.leases",
        "/var/lib/dnsmasq/dnsmasq.leases"
    };
    
    std::string ipAddress;
    for (const auto& path : leasePaths) {
        std::string cmd;
        if (path.find('*') != std::string::npos) {
            // Utiliser find pour les chemins avec wildcards
            cmd = "for f in " + path + "; do [ -f \"$f\" ] && cat \"$f\"; done 2>/dev/null";
        } else {
            cmd = "sudo cat \"" + path + "\" 2>/dev/null || true";
        }
        
        auto leaseResult = remoteExec.execute(cmd);
        if (leaseResult.success() && !leaseResult.output.empty()) {
            // Parser le fichier de baux
            std::istringstream iss(leaseResult.output);
            std::string line;
            while (std::getline(iss, line)) {
                // Format: timestamp mac ip hostname clientid
                std::istringstream lineStream(line);
                std::string timestamp, mac, ip, hostname, clientid;
                lineStream >> timestamp >> mac >> ip >> hostname >> clientid;
                
                if (!mac.empty() && !ip.empty()) {
                    // Comparer les MAC addresses (enlever les :)
                    std::string leaseMac = mac;
                    leaseMac.erase(std::remove(leaseMac.begin(), leaseMac.end(), ':'), leaseMac.end());
                    
                    if (leaseMac == macLower) {
                        ipAddress = ip;
                        break;
                    }
                }
            }
        }
        if (!ipAddress.empty()) break;
    }
    
    if (ipAddress.empty()) {
        // Dernière tentative: utiliser arp sur l'hôte
        std::string arpCmd = "sudo arp -n | grep -i " + macAddress + " | awk '{print $1}'";
        auto arpResult = remoteExec.execute(arpCmd);
        if (arpResult.success() && !arpResult.output.empty()) {
            ipAddress = arpResult.output;
            // Nettoyer la sortie
            ipAddress.erase(std::remove(ipAddress.begin(), ipAddress.end(), '\n'), ipAddress.end());
        }
    }
    
    if (!ipAddress.empty()) {
        result["success"] = true;
        result["primaryIP"] = ipAddress;
        result["mac"] = macAddress;
        result["method"] = "dhcp_lease";
        
        json interfaces = json::array();
        json iface;
        iface["name"] = "eth0";
        iface["hwaddr"] = macAddress;
        
        json addrs = json::array();
        json addrInfo;
        addrInfo["type"] = "ipv4";
        addrInfo["addr"] = ipAddress;
        addrInfo["prefix"] = 24; // Par défaut
        addrs.push_back(addrInfo);
        
        iface["addrs"] = addrs;
        interfaces.push_back(iface);
        
        result["interfaces"] = interfaces;
    } else {
        result["error"] = "Could not find IP in DHCP leases or ARP table";
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

std::vector<std::string> VMOperations::getDiskPaths(virDomainPtr domain) {
    std::vector<std::string> diskPaths;
    
    if (!domain) return diskPaths;
    
    char* xmlDesc = virDomainGetXMLDesc(domain, 0);
    if (!xmlDesc) {
        fprintf(stderr, "Failed to get domain XML\n");
        return diskPaths;
    }
    
    std::string xml(xmlDesc);
    free(xmlDesc);
    
    // Extract all disk file paths from XML
    // Look for: <source file='/path/to/disk.qcow2'/>
    std::regex diskRegex("<source file='([^']+)'");
    std::sregex_iterator iter(xml.begin(), xml.end(), diskRegex);
    std::sregex_iterator end;
    
    while (iter != end) {
        std::smatch match = *iter;
        std::string diskPath = match[1].str();
        
        // Skip ISO files and cloud-init ISOs (they're typically temporary)
        if (diskPath.find(".iso") != std::string::npos && 
            diskPath.find("cloud-init") == std::string::npos) {
            // Skip regular ISO files
            continue;
        }
        diskPaths.push_back(diskPath);
        fprintf(stdout, "Found disk: %s\n", diskPath.c_str());
        
        ++iter;
    }
    
    return diskPaths;
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
            
           deleteVM(displayName, true);
        }
        virDomainFree(domains[i]);
    }
    
    free(domains);
    
    return true;    
}


json VMOperations::deleteVM(const std::string& name, bool removeDisks=true) {
    json result;
    result["success"] = false;
    result["steps"] = json::array();
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
      
    // Step 1: Lookup domain
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) {
        virErrorPtr err = virGetLastError();
        std::string errorMsg = "VM not found";
        if (err) {
            errorMsg += ": " + std::string(err->message);
        }
        result["error"] = errorMsg;
        return result;
    }
    
    std::vector<std::string> diskPaths;
    
    // Step 2: Get disk paths (before undefining, if we need to remove them)
    if (removeDisks) {
        result["steps"].push_back("Getting disk paths...");
        diskPaths = getDiskPaths(domain);
        
        if (!diskPaths.empty()) {
            result["diskPaths"] = diskPaths;
            fprintf(stdout, "Found %zu disk(s) to remove\n", diskPaths.size());
        }
    }
    
    // Step 3: Stop VM if running
    result["steps"].push_back("Checking VM state...");
    if (!stopVMIfRunning(domain)) {
        result["error"] = "Failed to stop VM";
        result["steps"].push_back("ERROR: Failed to stop VM");
        virDomainFree(domain);
        return result;
    }
    result["steps"].push_back("VM stopped successfully");
    
    // Step 4: Delete all snapshots
    result["steps"].push_back("Deleting snapshots...");
    if (!deleteAllSnapshots(domain)) {
        result["warning"] = "Some snapshots could not be deleted";
        result["steps"].push_back("WARNING: Some snapshots failed to delete");
    } else {
        result["steps"].push_back("Snapshots deleted successfully");
    }
    
    // Step 5: Undefine the domain
    result["steps"].push_back("Undefining VM...");
    
    // Use VIR_DOMAIN_UNDEFINE_MANAGED_SAVE to remove saved state
    // Use VIR_DOMAIN_UNDEFINE_SNAPSHOTS_METADATA to remove snapshot metadata
    unsigned int undefineFlags = VIR_DOMAIN_UNDEFINE_MANAGED_SAVE | 
                                 VIR_DOMAIN_UNDEFINE_SNAPSHOTS_METADATA;
    
    if (virDomainUndefineFlags(domain, undefineFlags) < 0) {
        // Try simple undefine as fallback
        if (virDomainUndefine(domain) < 0) {
            virErrorPtr err = virGetLastError();
            std::string errorMsg = "Failed to undefine VM";
            if (err) {
                errorMsg += ": " + std::string(err->message);
            }
            result["error"] = errorMsg;
            result["steps"].push_back("ERROR: " + errorMsg);
            virDomainFree(domain);
            return result;
        }
    }
    
    result["steps"].push_back("VM undefined successfully");
    fprintf(stdout, "VM '%s' undefined successfully\n", name.c_str());
    
    // Free domain handle
    virDomainFree(domain);
    
    // Step 6: Delete disk files (if requested)
    if (removeDisks && !diskPaths.empty()) {
        result["steps"].push_back("Deleting disk files...");
        
        if (deleteDiskFiles(diskPaths)) {
            result["steps"].push_back("All disk files deleted successfully");
            result["disksDeleted"] = true;
        } else {
            result["warning"] = "Some disk files could not be deleted";
            result["steps"].push_back("WARNING: Some disk files failed to delete");
            result["disksDeleted"] = false;
        }
    } else if (removeDisks && diskPaths.empty()) {
        result["steps"].push_back("No disk files found to delete");
        result["disksDeleted"] = false;
    }
    
    // Success!
    result["success"] = true;
    result["message"] = "VM deleted successfully";
    
    fprintf(stdout, "\n========================================\n");
    fprintf(stdout, "VM '%s' deleted successfully!\n", name.c_str());
    fprintf(stdout, "========================================\n\n");
    
    return result;
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

json VMOperations::createPortForward(const std::string& vmName, int vmPort, 
                                     int hostPort, const std::string& protocol) {
    json result;
    result["success"] = false;
    
    // Get VM IP first
    auto ipResult = getVMIP(vmName);
    if (!ipResult["success"].get<bool>() || ipResult["primaryIP"].get<std::string>().empty()) {
        result["error"] = "Could not determine VM IP address";
        return result;
    }
    
    std::string vmIP = ipResult["primaryIP"];
    
    // If no host port specified, auto-assign one
    if (hostPort == 0) {
        hostPort = findAvailablePort(10000, 60000);
        if (hostPort == -1) {
            result["error"] = "No available ports for forwarding";
            return result;
        }
    }
    
    // Check if port is already in use
    if (isPortInUse(hostPort)) {
        result["error"] = "Host port " + std::to_string(hostPort) + " is already in use";
        return result;
    }
    
    // Create iptables NAT rule
    RemoteExec::RemoteExecutor remoteExec(conn);
    
    // DNAT rule: Forward incoming traffic on hostPort to VM
    std::stringstream dnatCmd;
    dnatCmd << "iptables -t nat -A PREROUTING "
            << "-p " << protocol << " --dport " << hostPort << " "
            << "-j DNAT --to-destination " << vmIP << ":" << vmPort;
    
    auto dnatResult = remoteExec.execute("sudo " + dnatCmd.str());
    
    if (!dnatResult.success()) {
        result["error"] = "Failed to create DNAT rule: " + dnatResult.output;
        return result;
    }
    
    // MASQUERADE rule: Allow forwarded traffic to be NAT'd
    std::stringstream masqCmd;
    masqCmd << "iptables -t nat -A POSTROUTING "
            << "-p " << protocol << " -d " << vmIP << " --dport " << vmPort << " "
            << "-j MASQUERADE";
    
    auto masqResult = remoteExec.execute("sudo " + masqCmd.str());
    
    if (!masqResult.success()) {
        // Rollback DNAT rule
        std::stringstream rollbackCmd;
        rollbackCmd << "iptables -t nat -D PREROUTING "
                   << "-p " << protocol << " --dport " << hostPort << " "
                   << "-j DNAT --to-destination " << vmIP << ":" << vmPort;
        remoteExec.execute("sudo " + rollbackCmd.str());
        
        result["error"] = "Failed to create MASQUERADE rule: " + masqResult.output;
        return result;
    }
    
    // Save the port forward mapping
    std::string forwardId = savePortForward(vmName, vmIP, vmPort, hostPort, protocol);
    
    result["success"] = true;
    result["forwardId"] = forwardId;
    result["vmPort"] = vmPort;
    result["hostPort"] = hostPort;
    result["protocol"] = protocol;
    result["vmIP"] = vmIP;
    
    return result;
}

json VMOperations::listPortForwards(const std::string& vmName) {
    json result;
    result["success"] = false;
    
    // Load port forwards from file
    std::string forwardsFile = "/var/lib/thoth-cloud/port_forwards.json";
    json forwards = json::array();
    
    std::ifstream file(forwardsFile);
    if (file.is_open()) {
        try {
            json allForwards;
            file >> allForwards;
            file.close();
            
            // Filter by VM name
            for (const auto& fwd : allForwards) {
                if (fwd["vmName"] == vmName) {
                    forwards.push_back(fwd);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error loading port forwards: " << e.what() << std::endl;
        }
    }
    
    result["success"] = true;
    result["forwards"] = forwards;
    result["count"] = forwards.size();
    
    return result;
}

json VMOperations::deletePortForward(const std::string& vmName, const std::string& forwardId) {
    json result;
    result["success"] = false;
    
    std::string forwardsFile = "/var/lib/thoth-cloud/port_forwards.json";
    json allForwards = json::array();
    json forwardToDelete;
    bool found = false;
    
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
    
    // Find and remove the forward
    json newForwards = json::array();
    for (const auto& fwd : allForwards) {
        if (fwd["id"] == forwardId && fwd["vmName"] == vmName) {
            forwardToDelete = fwd;
            found = true;
        } else {
            newForwards.push_back(fwd);
        }
    }
    
    if (!found) {
        result["error"] = "Port forward not found";
        return result;
    }
    
    // Remove iptables rules
    RemoteExec::RemoteExecutor remoteExec(conn);
    
    std::string vmIP = forwardToDelete["vmIP"];
    int vmPort = forwardToDelete["vmPort"];
    int hostPort = forwardToDelete["hostPort"];
    std::string protocol = forwardToDelete["protocol"];
    
    // Remove DNAT rule
    std::stringstream dnatCmd;
    dnatCmd << "iptables -t nat -D PREROUTING "
            << "-p " << protocol << " --dport " << hostPort << " "
            << "-j DNAT --to-destination " << vmIP << ":" << vmPort;
    
    remoteExec.execute("sudo " + dnatCmd.str());
    
    // Remove MASQUERADE rule
    std::stringstream masqCmd;
    masqCmd << "iptables -t nat -D POSTROUTING "
            << "-p " << protocol << " -d " << vmIP << " --dport " << vmPort << " "
            << "-j MASQUERADE";
    
    remoteExec.execute("sudo " + masqCmd.str());
    
    // Save updated forwards
    std::ofstream outFile(forwardsFile);
    if (outFile.is_open()) {
        outFile << newForwards.dump(2);
        outFile.close();
    }
    
    result["success"] = true;
    result["message"] = "Port forward deleted";
    
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