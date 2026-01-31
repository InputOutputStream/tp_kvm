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

VMOperations::VMOperations(virConnectPtr connection, HostManager *hostMgr, NetworkManager *netMgr)
 : conn(connection), hostManager(hostMgr), networkManager(netMgr){}

std::string VMOperations::getStateString(int state) {
    const char* states[] = {"no state", "running", "blocked", "paused", 
                           "shutdown", "shut off", "crashed", "pmsuspended"};
    if (state >= 0 && state < 8) {
        return states[state];
    }
    return "unknown";
}


json VMOperations::listUserVMs(const std::string& userId) {
    json result;
    result["success"] = false;
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    virDomainPtr* domains;
    int numDomains = virConnectListAllDomains(conn, &domains, 0);
    
    if (numDomains < 0) {
        result["error"] = "Error listing VMs";
        return result;
    }
    
    VMNameManager nameManager;
    json vms = json::array();
    
    for (int i = 0; i < numDomains; i++) {
        const char* name = virDomainGetName(domains[i]);
        
        // Check if VM belongs to user
        if (nameManager.isOwner(name, userId)) {
            virDomainInfo info;
            virDomainGetInfo(domains[i], &info);
            
            int id = virDomainGetID(domains[i]);
            std::string state = getStateString(info.state);
            bool isRunning = (info.state == VIR_DOMAIN_RUNNING);
            
            // Parse name to get display name
            auto nameInfo = nameManager.parseVMName(name);
            std::string displayName = nameInfo.valid ? nameInfo.vmName : name;
            
            json vm = {
                {"id", id},
                {"name", name},  // Internal name
                {"displayName", displayName},  // User-friendly name
                {"state", state},
                {"running", isRunning},
                {"owner", userId},
                {"stats", nullptr}
            };
            
            if (isRunning) {
                vm["stats"] = getVMStatsInternal(domains[i], name);
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

// Update listAllVMs to include owner info
json VMOperations::listAllVMs() {
    json result;
    result["success"] = false;
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    virDomainPtr* domains;
    int numDomains = virConnectListAllDomains(conn, &domains, 0);
    
    if (numDomains < 0) {
        result["error"] = "Error listing VMs";
        return result;
    }
    
    VMNameManager nameManager;
    json vms = json::array();
    
    for (int i = 0; i < numDomains; i++) {
        const char* name = virDomainGetName(domains[i]);
        virDomainInfo info;
        virDomainGetInfo(domains[i], &info);
        
        int id = virDomainGetID(domains[i]);
        std::string state = getStateString(info.state);
        bool isRunning = (info.state == VIR_DOMAIN_RUNNING);
        
        // Parse name to extract owner and display name
        auto nameInfo = nameManager.parseVMName(name);
        std::string displayName = nameInfo.valid ? nameInfo.vmName : name;
        std::string owner = nameInfo.valid ? nameInfo.username : "unknown";
        
        json vm = {
            {"id", id},
            {"name", name},  // Internal name
            {"displayName", displayName},  // User-friendly name
            {"owner", owner},
            {"state", state},
            {"running", isRunning},
            {"stats", nullptr}
        };
        
        if (isRunning) {
            vm["stats"] = getVMStatsInternal(domains[i], name);
        }
        
        vms.push_back(vm);
        virDomainFree(domains[i]);
    }
    
    free(domains);
    
    result["success"] = true;
    result["vms"] = vms;
    result["totalCount"] = vms.size();
    
    return result;
}

json VMOperations::getVMStatsInternal(virDomainPtr domain, const std::string& vmName) {
    json stats;
    
    virDomainInfo info;
    if (virDomainGetInfo(domain, &info) < 0) {
        return stats;
    }
    
    // CPU usage
    double cpuUsage = 0.0;
    unsigned long long cpuTime = info.cpuTime;
    
    if (statsCache.find(vmName) != statsCache.end()) {
        CPUCache cached = statsCache[vmName];
        long long timeDiff = getCurrentTimeMs() - cached.timestamp;
        unsigned long long cpuDiff = cpuTime - cached.cpuTime;
        if (timeDiff > 0) {
            cpuUsage = ((double)cpuDiff / (timeDiff * 1000000.0)) * 100.0;
        }
    }
    statsCache[vmName] = {cpuTime, getCurrentTimeMs()};
    
    stats["cpu"] = cpuUsage;
    
    // Memory
    stats["memory"] = {
        {"used", info.memory},
        {"max", info.maxMem},
        {"percent", info.maxMem > 0 ? (info.memory * 100.0 / info.maxMem) : 0}
    };
    
    // Disk stats
    virDomainBlockStatsStruct blockStats;
    long long diskRead = 0, diskWrite = 0;
    if (virDomainBlockStats(domain, "vda", &blockStats, sizeof(blockStats)) == 0) {
        diskRead = blockStats.rd_bytes;
        diskWrite = blockStats.wr_bytes;
    }
    
    stats["disk"] = {
        {"read", diskRead},
        {"write", diskWrite},
        {"readMB", diskRead / 1024.0 / 1024.0},
        {"writeMB", diskWrite / 1024.0 / 1024.0}
    };
    
    // Network stats
    virDomainInterfaceStatsStruct netStats;
    long long netRx = 0, netTx = 0;
    if (virDomainInterfaceStats(domain, "vnet0", &netStats, sizeof(netStats)) == 0) {
        netRx = netStats.rx_bytes;
        netTx = netStats.tx_bytes;
    }
    
    stats["network"] = {
        {"rx", netRx},
        {"tx", netTx},
        {"rxMB", netRx / 1024.0 / 1024.0},
        {"txMB", netTx / 1024.0 / 1024.0}
    };
    
    return stats;
}

json VMOperations::getVMInfo(const std::string& name) {
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
    
    virDomainInfo info;
    virDomainGetInfo(domain, &info);
    
    char* xmlDesc = virDomainGetXMLDesc(domain, 0);
    
    json parsed = {
        {"Max memory", std::to_string(info.maxMem) + " KB"},
        {"Used memory", std::to_string(info.memory) + " KB"},
        {"CPU(s)", info.nrVirtCpu},
        {"CPU time", std::to_string(info.cpuTime) + "ns"},
        {"State", info.state}
    };
    
    std::stringstream infoStr;
    infoStr << "Max memory: " << info.maxMem << " KB\n";
    infoStr << "Used memory: " << info.memory << " KB\n";
    infoStr << "CPU(s): " << info.nrVirtCpu << "\n";
    infoStr << "CPU time: " << info.cpuTime << "ns\n";
    infoStr << "State: " << info.state;
    
    result["success"] = true;
    result["info"] = infoStr.str();
    result["parsed"] = parsed;
    result["xml"] = xmlDesc;
    
    free(xmlDesc);
    virDomainFree(domain);
    
    return result;
}

json VMOperations::getVMStats(const std::string& name) {
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
    
    json stats = getVMStatsInternal(domain, name);
    virDomainFree(domain);
    
    result["success"] = true;
    result["stats"] = stats;
    return result;
}

json VMOperations::getVMStatus(const std::string& name) {
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
    
    virDomainInfo info;
    virDomainGetInfo(domain, &info);
    
    std::string state = getStateString(info.state);
    bool isRunning = (info.state == VIR_DOMAIN_RUNNING);
    
    result["success"] = true;
    result["state"] = state;
    result["running"] = isRunning;
    
    virDomainFree(domain);
    return result;
}

bool VMOperations::startVM(const std::string& name) {
    if (!conn) return false;
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) return false;
    
    int result = virDomainCreate(domain);
    virDomainFree(domain);
    
    return result >= 0;
}

bool VMOperations::shutdownVM(const std::string& name) {
    if (!conn) return false;
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) return false;
    
    int result = virDomainShutdown(domain);
    virDomainFree(domain);
    
    return result >= 0;
}

// ==========================================
// RESOURCE MANAGEMENT
// ==========================================

VMOperations::HostSelection VMOperations::selectOptimalHost(const json& vmParams) {
    HostSelection result = {"", nullptr};
    
    if (!vmParams.contains("hostManager")) {
        result.connection = conn;
        return result;
    }
    
    int memory = vmParams["memory"];
    int vcpus = vmParams["vcpus"];
    int disk = vmParams["disk"];
    long long diskBytes = (long long)disk * 1024 * 1024 * 1024;
    
    json availability = hostManager->checkResourceAvailability(memory, vcpus, diskBytes);
    
    if (!availability["available"].get<bool>()) {
        logDeploymentFailure(vmParams, availability);
        return result;
    }
    
    std::string bestHost = hostManager->findBestHost(memory, vcpus, diskBytes);
    if (bestHost.empty()) {
        fprintf(stderr, "❌ No suitable host found\n");
        return result;
    }
    
    fprintf(stdout, "✅ Selected host: %s\n", bestHost.c_str());
    
    virConnectPtr hostConn = hostManager->getConnection(bestHost);
    if (!hostConn) {
        fprintf(stderr, "❌ Failed to get connection to host: %s\n", bestHost.c_str());
        return result;
    }
    
    result.hostname = bestHost;
    result.connection = hostConn;
    return result;
}

bool VMOperations::logDeploymentFailure(const json& vmParams, const json& availability) {
    int memory = vmParams["memory"];
    int vcpus = vmParams["vcpus"];
    int disk = vmParams["disk"];
    
    fprintf(stderr, "❌ INSUFFICIENT RESOURCES ACROSS ALL HOSTS\n");
    fprintf(stderr, "Required: %d MB RAM, %d vCPUs, %d GB Disk\n", memory, vcpus, disk);
    fprintf(stderr, "\nAvailable on hosts:\n");
    
    for (const auto& host : availability["hosts"]) {
        fprintf(stderr, "  • %s: %lu MB RAM, %d vCPUs, %lld GB Disk\n",
               host["hostname"].get<std::string>().c_str(),
               host["availableMemory"].get<unsigned long>(),
               host["availableCPUs"].get<int>(),
               host["availableDisk"].get<long long>());
    }
    
    std::ofstream logFile("/var/log/thoth-cloud/deployment-failures.log", std::ios::app);
    if (logFile.is_open()) {
        auto now = std::time(nullptr);
        logFile << std::ctime(&now) << " - INSUFFICIENT RESOURCES\n";
        logFile << "  VM: " << vmParams["hostname"] << "\n";
        logFile << "  Required: " << memory << "MB, " << vcpus << " vCPUs, " << disk << "GB\n";
        logFile << "  User: " << vmParams.value("owner", "unknown") << "\n\n";
    }
    
    return false;
}

// ==========================================
// VALIDATION METHODS
// ==========================================

bool VMOperations::validateConnection() {
    fprintf(stdout, "\n🔍 Validating libvirt connection...\n");
    
    auto connResult = Validation::SystemValidator::checkLibvirtConnection(conn);
    if (!connResult.valid) {
        fprintf(stderr, "❌ %s\n", connResult.error.c_str());
        return false;
    }
    
    fprintf(stdout, "✅ Libvirt connection verified\n");
    return true;
}

bool VMOperations::validateInputParameters(const json& vmParams) {
    fprintf(stdout, "\n🔍 Validating input parameters...\n");
    
    auto validationResult = Validation::Validator::validateDeploymentParams(vmParams);
    if (!validationResult.valid) {
        fprintf(stderr, "❌ Validation failed: %s\n", validationResult.error.c_str());
        return false;
    }
    
    for (const auto& warning : validationResult.warnings) {
        fprintf(stdout, "⚠️  Warning: %s\n", warning.c_str());
    }
    
    fprintf(stdout, "✅ Input parameters validated\n");
    return true;
}

bool VMOperations::validateVMNameAvailability(const std::string& hostname) {
    fprintf(stdout, "\n🔍 Checking VM name availability...\n");
    
    auto nameResult = Validation::SystemValidator::checkVMNameAvailable(conn, hostname);
    if (!nameResult.valid) {
        fprintf(stderr, "❌ %s\n", nameResult.error.c_str());
        fprintf(stderr, "   Suggestion: Choose a different hostname or delete the existing VM\n");
        return false;
    }
    
    fprintf(stdout, "✅ VM name '%s' is available\n", hostname.c_str());
    return true;
}

bool VMOperations::validateRemoteDirectories(RemoteExec::RemoteExecutor& remoteExec) {
    fprintf(stdout, "\n🔍 Checking required directories on target host...\n");
    
    std::vector<std::string> requiredDirs = {
        "/var/lib/libvirt/images",
        "/var/lib/libvirt/images/baseimg",
        "/var/lib/libvirt/images/cloud-init-iso"
    };
    
    std::vector<std::string> missingDirs;
    for (const auto& dir : requiredDirs) {
        if (!remoteExec.directoryExists(dir)) {
            missingDirs.push_back(dir);
        }
    }
    
    if (!missingDirs.empty()) {
        fprintf(stderr, "❌ Required directories missing on target host:\n");
        for (const auto& dir : missingDirs) {
            fprintf(stderr, "   - %s\n", dir.c_str());
        }
        fprintf(stderr, "\n💡 On the target host, run:\n");
        fprintf(stderr, "   sudo mkdir -p /var/lib/libvirt/images/baseimg /var/lib/libvirt/images/cloud-init-iso\n");
        fprintf(stderr, "   sudo chown -R libvirt-qemu:kvm /var/lib/libvirt/images\n");
        return false;
    }
    
    fprintf(stdout, "✅ All required directories exist on target host\n");
    return true;
}

bool VMOperations::validateRemoteTools(RemoteExec::RemoteExecutor& remoteExec) {
    fprintf(stdout, "\n🔍 Checking required tools on target host...\n");
    
    std::vector<std::string> requiredTools = {"qemu-img", "genisoimage", "mkpasswd"};
    std::vector<std::string> missingTools;
    
    for (const auto& tool : requiredTools) {
        if (!remoteExec.commandExists(tool)) {
            missingTools.push_back(tool);
        }
    }
    
    if (!missingTools.empty()) {
        fprintf(stderr, "❌ Required tools missing on target host:\n");
        for (const auto& tool : missingTools) {
            fprintf(stderr, "   - %s\n", tool.c_str());
        }
        fprintf(stderr, "\n💡 On the target host, install them:\n");
        fprintf(stderr, "   sudo apt-get install -y qemu-utils genisoimage whois\n");
        return false;
    }
    
    fprintf(stdout, "✅ All required tools are installed on target host\n");
    return true;
}

bool VMOperations::validateBaseImage(RemoteExec::RemoteExecutor& remoteExec, 
                                     const std::string& baseImageId) {
    fprintf(stdout, "\n🔍 Validating base image on target host...\n");
    fprintf(stdout, "   Requested image: %s\n", baseImageId.c_str());
    
    // Create BaseImageManager to discover available images
    BaseImageManager imageManager(&remoteExec);
    
    // Check if requested image is available
    if (!imageManager.isImageAvailable(baseImageId)) {
        fprintf(stderr, "❌ Base image '%s' not found on target host\n", baseImageId.c_str());
        
        // List available images
        auto imagesList = imageManager.listImages();
        if (imagesList["count"].get<int>() > 0) {
            fprintf(stderr, "\n📋 Available base images:\n");
            for (const auto& img : imagesList["images"]) {
                fprintf(stderr, "   - %s (%s)\n", 
                        img["id"].get<std::string>().c_str(),
                        img["displayName"].get<std::string>().c_str());
            }
        } else {
            fprintf(stderr, "\n📥 No base images found. Download images to: %s\n", 
                    imagesList["baseImageDir"].get<std::string>().c_str());
        }
        return false;
    }
    
    // Get image details
    auto imageInfo = imageManager.getImage(baseImageId);
    std::string imagePath = imageInfo["image"]["path"];
    
    // Verify image is valid
    if (!remoteExec.isValidDiskImage(imagePath)) {
        fprintf(stderr, "❌ Base image is corrupted or invalid: %s\n", imagePath.c_str());
        return false;
    }
    
    fprintf(stdout, "✅ Base image is valid\n");
    fprintf(stdout, "   Image: %s\n", imageInfo["image"]["displayName"].get<std::string>().c_str());
    fprintf(stdout, "   Path: %s\n", imagePath.c_str());
    
    return true;
}

bool VMOperations::validateDiskSpace(RemoteExec::RemoteExecutor& remoteExec, int diskGB) {
    fprintf(stdout, "\n🔍 Checking available disk space on target host...\n");
    
    long long requiredBytes = (long long)diskGB * 1024 * 1024 * 1024;
    requiredBytes += 1024 * 1024 * 1024;  // Add 1GB buffer
    
    long long availableBytes = remoteExec.getAvailableDiskSpace("/var/lib/libvirt/images");
    
    if (availableBytes < 0) {
        fprintf(stdout, "⚠️  Could not verify disk space. Proceeding with deployment...\n");
        return true;
    }
    
    if (availableBytes < requiredBytes) {
        fprintf(stderr, "❌ Insufficient disk space on target host.\n");
        fprintf(stderr, "   Required: %.2f GB\n", requiredBytes / (1024.0*1024.0*1024.0));
        fprintf(stderr, "   Available: %.2f GB\n", availableBytes / (1024.0*1024.0*1024.0));
        return false;
    }
    
    fprintf(stdout, "✅ Sufficient disk space available on target host\n");
    fprintf(stdout, "   Available: %.2f GB\n", availableBytes / (1024.0*1024.0*1024.0));
    
    long long remainingBytes = availableBytes - requiredBytes;
    if (remainingBytes < 10LL * 1024 * 1024 * 1024) {
        fprintf(stdout, "⚠️  Warning: Less than 10GB will remain after allocation\n");
    }
    
    return true;
}

bool VMOperations::validateNetwork() {
    fprintf(stdout, "\n🔍 Checking default network...\n");
    
    auto networkResult = Validation::SystemValidator::checkNetworkAvailable(conn, "default");
    if (!networkResult.valid) {
        fprintf(stderr, "❌ %s\n", networkResult.error.c_str());
        return false;
    }
    
    fprintf(stdout, "✅ Network 'default' is active on target host\n");
    return true;
}

// ==========================================
// CLOUD-INIT METHODS
// ==========================================

VMOperations::CloudInitConfig VMOperations::createCloudInitConfig(const json& vmParams) {
    CloudInitConfig config;
    
    std::string hostname = vmParams["hostname"];
    std::string actualHostname = vmParams["owner"].get<std::string>() + "-" + hostname;
    std::string username = vmParams.value("username", "ubuntu");
    std::string authMethod = vmParams.value("authMethod", "password");
    std::string sshKey = vmParams.value("sshKey", "");
    
    // Create meta-data
    std::stringstream metaData;
    metaData << "instance-id: " << hostname << "\n"
             << "local-hostname: " << hostname << "\n";
    
    // Create user-data
    std::stringstream userData;
    userData << "#cloud-config\n"
             << "hostname: " << actualHostname << "\n"
             << "fqdn: " << hostname << ".local\n"
             << "manage_etc_hosts: true\n\n"
             << "users:\n"
             << "  - name: " << username << "\n"
             << "    sudo: ALL=(ALL) NOPASSWD:ALL\n"
             << "    groups: users, admin\n"
             << "    shell: /bin/bash\n";
    
    if (authMethod == "ssh-key" && !sshKey.empty()) {
        userData << "    ssh_authorized_keys:\n"
                 << "      - " << sshKey << "\n";
    }
    
    userData << "\n"
             << "ssh_pwauth: " << (authMethod == "password" ? "true" : "false") << "\n"
             << "disable_root: false\n"
             << "chpasswd:\n"
             << "  expire: false\n\n"
             << "package_update: true\n"
             << "package_upgrade: false\n\n"
             << "packages:\n"
             << "  - qemu-guest-agent\n"
             << "  - cloud-init\n\n"
             << "runcmd:\n"
             << "  - systemctl enable qemu-guest-agent\n"
             << "  - systemctl start qemu-guest-agent\n"
             << "  - echo 'Cloud-init setup complete' > /var/log/cloudinit-done\n\n"
             << "power_state:\n"
             << "  mode: reboot\n"
             << "  timeout: 30\n"
             << "  condition: true\n";
    
    config.metaData = metaData.str();
    config.userData = userData.str();
    
    return config;
}

std::string VMOperations::hashPassword(RemoteExec::RemoteExecutor& remoteExec, 
                                       const std::string& password) {
    std::string hashCmd = "mkpasswd --method=SHA-512 --rounds=4096 '" + password + "'";
    auto hashResult = remoteExec.execute(hashCmd);
    
    if (!hashResult.success()) {
        return "";
    }
    
    std::string hashedPassword = hashResult.output;
    hashedPassword.erase(hashedPassword.find_last_not_of("\n\r") + 1);
    return hashedPassword;
}

bool VMOperations::writeCloudInitFiles(RemoteExec::RemoteExecutor& remoteExec,
                                       const std::string& hostname,
                                       const CloudInitConfig& config) {
    fprintf(stdout, "Step 1/7: Creating cloud-init configuration...\n");
    
    std::string cloudInitDir = "/tmp/cloudinit-" + hostname;
    
    auto mkdirResult = remoteExec.execute("mkdir -p " + cloudInitDir);
    if (!mkdirResult.success()) {
        fprintf(stderr, "   ❌ Failed to create temp directory on target host\n");
        return false;
    }
    
    std::string writeMetaCmd = "cat > " + cloudInitDir + "/meta-data << 'EOF'\n" + 
                               config.metaData + "\nEOF";
    std::string writeUserCmd = "cat > " + cloudInitDir + "/user-data << 'EOF'\n" + 
                               config.userData + "\nEOF";
    
    auto writeMetaResult = remoteExec.execute(writeMetaCmd);
    auto writeUserResult = remoteExec.execute(writeUserCmd);
    
    if (!writeMetaResult.success() || !writeUserResult.success()) {
        fprintf(stderr, "   ❌ Failed to write cloud-init files on target host\n");
        return false;
    }
    
    fprintf(stdout, "   ✅ Cloud-init configuration created\n");
    return true;
}

bool VMOperations::createCloudInitISO(RemoteExec::RemoteExecutor& remoteExec,
                                      const std::string& hostname) {
    fprintf(stdout, "Step 2/7: Creating cloud-init ISO...\n");
    
    std::string cloudInitDir = "/tmp/cloudinit-" + hostname;
    std::string cloudInitPath =
    "/tmp/" + hostname + "-cloudinit.iso";

    std::string createIsoCmd = "xorriso -as mkisofs "
            "-output " + cloudInitPath +
            " -volid cidata -joliet -rock " +
            cloudInitDir + "/user-data " +
            cloudInitDir + "/meta-data 2>&1";
    
    auto isoResult = remoteExec.execute(createIsoCmd);

    auto mvResult = remoteExec.execute(
        "sudo mv " + cloudInitPath +
        " /var/lib/libvirt/images/cloud-init-iso/"
    );

    if (!mvResult.success()) {
        fprintf(stderr, "   ❌ Failed to move cloud-init ISO\n");
        return false;
    }

    if (!isoResult.success()) {
        fprintf(stderr, "   ❌ Failed to create cloud-init ISO: %s\n", isoResult.output.c_str());
        return false;
    }
    
    fprintf(stdout, "   ✅ Cloud-init ISO created\n");
    
    // Clean up temp directory
    remoteExec.execute("rm -rf " + cloudInitDir);
    
    return true;
}

// ==========================================
// DISK OPERATIONS
// ==========================================

bool VMOperations::copyBaseImage(RemoteExec::RemoteExecutor& remoteExec,
                                 const std::string& hostname,
                                 const std::string& baseImageId) {
    fprintf(stdout, "Step 3/7: Copying base cloud image...\n");
    
    // Use BaseImageManager to get the actual image path
    BaseImageManager imageManager(&remoteExec);
    std::string baseImagePath = imageManager.getImagePath(baseImageId);
    
    if (baseImagePath.empty()) {
        fprintf(stderr, "   ❌ Failed to locate base image: %s\n", baseImageId.c_str());
        return false;
    }
    
    fprintf(stdout, "   Using image: %s\n", baseImagePath.c_str());
    
    std::string diskPath = "/var/lib/libvirt/images/" + hostname + ".qcow2";
    std::string copyCmd = "sudo cp " + baseImagePath + " " + diskPath;
    
    auto copyResult = remoteExec.execute(copyCmd);
    if (!copyResult.success()) {
        fprintf(stderr, "   ❌ Failed to copy base image: %s\n", copyResult.output.c_str());
        return false;
    }
    
    fprintf(stdout, "   ✅ Base image copied\n");
    return true;
}

bool VMOperations::resizeDisk(RemoteExec::RemoteExecutor& remoteExec,
                              const std::string& hostname, int diskGB) {
    fprintf(stdout, "📝 Step 4/7: Resizing disk to %dGB...\n", diskGB);
    
    std::string diskPath = "/var/lib/libvirt/images/" + hostname + ".qcow2";
    std::string resizeCmd = "sudo qemu-img resize  " + diskPath + "  +" + std::to_string(diskGB) + "G";

    auto resizeResult = remoteExec.execute(resizeCmd);
    
    if (!resizeResult.success()) {
        fprintf(stderr, "   ❌ Failed to resize disk: %s\n", resizeResult.output.c_str());
        return false;
    }
    
    fprintf(stdout, "   ✅ Disk resized\n");
    return true;
}

// ==========================================
// VM CREATION
// ==========================================

std::string VMOperations::generateDomainXML(const json& vmParams) {
    std::string hostname = vmParams["hostname"];
    int memory = vmParams["memory"];
    int vcpus = vmParams["vcpus"];
    
    // Get network from parameters, default to 'default'
    std::string network = vmParams.value("network", "default");
    
    std::string diskPath = "/var/lib/libvirt/images/" + hostname + ".qcow2";
    std::string cloudInitPath = "/var/lib/libvirt/images/cloud-init-iso/" + hostname + "-cloudinit.iso";
    
    std::stringstream xmlConfig;
    xmlConfig << "<domain type='kvm'>"
              << "  <name>" << hostname << "</name>"
              << "  <memory unit='MiB'>" << memory << "</memory>"
              << "  <currentMemory unit='MiB'>" << memory << "</currentMemory>"
              << "  <vcpu placement='static'>" << vcpus << "</vcpu>"
              << "  <os>"
              << "    <type arch='x86_64' machine='pc'>hvm</type>"
              << "    <boot dev='hd'/>"
              << "  </os>"
              << "  <features>"
              << "    <acpi/>"
              << "    <apic/>"
              << "  </features>"
              << "  <cpu mode='host-passthrough'/>"
              << "  <clock offset='utc'/>"
              << "  <on_poweroff>destroy</on_poweroff>"
              << "  <on_reboot>restart</on_reboot>"
              << "  <on_crash>destroy</on_crash>"
              << "  <devices>"
              << "    <emulator>/usr/bin/qemu-system-x86_64</emulator>"
              << "    <disk type='file' device='disk'>"
              << "      <driver name='qemu' type='qcow2'/>"
              << "      <source file='" << diskPath << "'/>"
              << "      <target dev='vda' bus='virtio'/>"
              << "    </disk>"
              << "    <disk type='file' device='cdrom'>"
              << "      <driver name='qemu' type='raw'/>"
              << "      <source file='" << cloudInitPath << "'/>"
              << "      <target dev='hdc' bus='ide'/>"
              << "      <readonly/>"
              << "    </disk>"
              << "    <interface type='network'>"
              << "      <source network='" << network << "'/>"  
              << "      <model type='virtio'/>"
              << "    </interface>"
              << "    <serial type='pty'>"
              << "      <target type='isa-serial' port='0'>"
              << "        <model name='isa-serial'/>"
              << "      </target>"
              << "    </serial>"
              << "    <console type='pty'>"
              << "      <target type='serial' port='0'/>"
              << "    </console>"
              << "    <channel type='unix'>"
              << "      <target type='virtio' name='org.qemu.guest_agent.0'/>"
              << "    </channel>"
              << "    <graphics type='vnc' port='-1' autoport='yes' listen='0.0.0.0'>"
              << "      <listen type='address' address='0.0.0.0'/>"
              << "    </graphics>"
              << "  </devices>"
              << "</domain>";
    
    return xmlConfig.str();
}

std::string VMOperations::generateVNCSessionToken()
{
    static int session_no = 0;
    std::string tok = "vnc" + std::to_string(session_no) + "__" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    session_no ++;
    return tok;
}

json VMOperations::generateVNCToken(int vncPort, json result)
{
    std::string vncToken = generateVNCSessionToken();
    std::string tokenFile = "/var/lib/thoth-cloud/novnc/tokens";

    // Write token: token: host:port
    std::ofstream tokens(tokenFile, std::ios::app);
    tokens << vncToken << ": localhost:" << vncPort << "\n";
    tokens.close();

    return result["vnc"]["token"] = vncToken;
}

bool VMOperations::startVM(virDomainPtr domain) {
    fprintf(stdout, "Step 7/7: Starting VM...\n");
    
    if (virDomainCreate(domain) < 0) {
        virErrorPtr err = virGetLastError();
        if (err) {
            fprintf(stderr, "   ❌ Failed to start domain: %s\n", err->message);
        }
        return false;
    }
    
    fprintf(stdout, "   ✅ VM started successfully\n\n");
    return true;
}

virDomainPtr VMOperations::defineVM(const std::string& xml) {
    fprintf(stdout, "📝 Step 6/7: Defining VM in libvirt...\n");
    
    virDomainPtr domain = virDomainDefineXML(conn, xml.c_str());
    if (!domain) {
        virErrorPtr err = virGetLastError();
        if (err) {
            fprintf(stderr, "   ❌ Failed to define domain: %s\n", err->message);
        }
        return nullptr;
    }
    
    fprintf(stdout, "   ✅ VM defined in libvirt\n");
    return domain;
}


// ==========================================
// HELPER METHODS
// ==========================================

void VMOperations::printConfiguration(const json& vmParams) {
    fprintf(stdout, "\n📋 Configuration:\n");
    fprintf(stdout, "   Hostname: %s\n", vmParams["hostname"].get<std::string>().c_str());
    fprintf(stdout, "   Memory: %d MB\n", vmParams["memory"].get<int>());
    fprintf(stdout, "   vCPUs: %d\n", vmParams["vcpus"].get<int>());
    fprintf(stdout, "   Disk: %d GB\n", vmParams["disk"].get<int>());
    fprintf(stdout, "   Username: %s\n", vmParams.value("username", "ubuntu").c_str());
    fprintf(stdout, "   Auth: %s\n", vmParams.value("authMethod", "password").c_str());
    fprintf(stdout, "\n");
}

// ==========================================
// MAIN DEPLOYMENT METHOD
// ==========================================

bool VMOperations::deployVM(const json& vmParams) {
    try {
    
        // Select optimal host
        auto hostSelection = selectOptimalHost(vmParams);
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
        
        // Validation phase
        if (!validateConnection()) return false;
        if (!validateInputParameters(vmParams)) return false;
        
        std::string hostname = vmParams["hostname"];
        std::string baseImageId = vmParams.value("baseImage", "");

        if (!validateVMNameAvailability(hostname)) return false;
        if (!validateRemoteDirectories(remoteExec)) return false;
        if (!validateRemoteTools(remoteExec)) return false;
        if (!validateBaseImage(remoteExec, baseImageId)) return false;
        if (!validateDiskSpace(remoteExec, vmParams["disk"])) return false;
        if (!validateNetwork()) return false;
        
        // Print configuration
        printConfiguration(vmParams);
        
        // Create cloud-init configuration
        auto cloudInitConfig = createCloudInitConfig(vmParams);

        // Get base image ID from parameters (default to first available if not specified)
     
        // If no image specified, try to auto-select
        if (baseImageId.empty()) {
            BaseImageManager imageManager(&remoteExec);
            auto imagesList = imageManager.listImages();
            
            if (imagesList["count"].get<int>() == 0) {
                fprintf(stderr, "❌ No base images available on target host\n");
                return false;
            }
            
            // Use first available image
            baseImageId = imagesList["images"][0]["id"];
            fprintf(stdout, "ℹ️  No base image specified, using: %s\n", 
                    imagesList["images"][0]["displayName"].get<std::string>().c_str());
        }
        
        // Handle password hashing if needed
        if (vmParams.value("authMethod", "password") == "password" && 
            vmParams.contains("password") && !vmParams["password"].get<std::string>().empty()) {
            
            std::string hashedPassword = hashPassword(remoteExec, vmParams["password"]);
            if (hashedPassword.empty()) {
                fprintf(stderr, "   ❌ Failed to generate password hash on target host\n");
                return false;
            }
            
            // Insert password into user-data
            size_t pos = cloudInitConfig.userData.find("    shell: /bin/bash\n");
            if (pos != std::string::npos) {
                std::string passwordSection = "    passwd: " + hashedPassword + "\n" +
                                             "    lock_passwd: false\n";
                cloudInitConfig.userData.insert(pos + 24, passwordSection);
            }
        }
        
        // Deployment phase
        if (!writeCloudInitFiles(remoteExec, hostname, cloudInitConfig)) return false;
        if (!createCloudInitISO(remoteExec, hostname)) return false;
        if (!copyBaseImage(remoteExec, hostname, baseImageId)) return false;
        if (!resizeDisk(remoteExec, hostname, vmParams["disk"])) return false;
        
        // Create and start VM
        fprintf(stdout, "Step 5/7: Creating VM definition...\n");
        std::string xml = generateDomainXML(vmParams);
        fprintf(stdout, "   ✅ VM definition created\n");
        
        virDomainPtr domain = defineVM(xml);
        if (!domain) return false;
        
        bool started = startVM(domain);
        virDomainFree(domain);
        
        return started;
        
    } catch (const std::exception& e) {
        fprintf(stderr, "\n❌ Exception during deployment: %s\n", e.what());
        return false;
    }
}

bool VMOperations::destroyVM(const std::string& name) {
    if (!conn) return false;
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) return false;
    
    int result = virDomainDestroy(domain);
    virDomainFree(domain);
    
    return result >= 0;
}

bool VMOperations::rebootVM(const std::string& name) {
    if (!conn) return false;
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) return false;
    
    int result = virDomainReboot(domain, 0);
    virDomainFree(domain);
    
    return result >= 0;
}

bool VMOperations::pauseVM(const std::string& name) {
    if (!conn) return false;
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) return false;
    
    int result = virDomainSuspend(domain);
    virDomainFree(domain);
    
    return result >= 0;
}

bool VMOperations::resumeVM(const std::string& name) {
    if (!conn) return false;
    
    virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
    if (!domain) return false;
    
    int result = virDomainResume(domain);
    virDomainFree(domain);
    
    return result >= 0;
}

json VMOperations::getVNCInfo(const std::string& name) {
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
    
    // Check if VM is running
    virDomainInfo info;
    if (virDomainGetInfo(domain, &info) < 0 || info.state != VIR_DOMAIN_RUNNING) {
        result["error"] = "VM is not running";
        virDomainFree(domain);
        return result;
    }
    
    // Get VNC port from domain XML
    char* xmlDesc = virDomainGetXMLDesc(domain, 0);
    if (!xmlDesc) {
        result["error"] = "Failed to get VM configuration";
        virDomainFree(domain);
        return result;
    }
    
    std::string xml(xmlDesc);
    free(xmlDesc);
    
    // Parse VNC port from XML
    // Look for: <graphics type='vnc' port='5900' .../>
    std::regex vncRegex("<graphics type='vnc' port='([0-9]+)'");
    std::smatch match;
    
    int vncPort = -1;
    if (std::regex_search(xml, match, vncRegex)) {
        vncPort = std::stoi(match[1].str());
    }
    
    virDomainFree(domain);
    
    if (vncPort == -1) {
        result["error"] = "VNC not configured for this VM";
        return result;
    }
    
    // Get hostname
    char* hostname = virConnectGetHostname(conn);
    std::string host = hostname ? std::string(hostname) : "localhost";
    if (hostname) free(hostname);
    
    result["success"] = true;
    result["vnc"] = {
        {"host", host},
        {"port", vncPort},
        {"display", vncPort - 5900},
        {"websocketUrl", "ws://" + host + ":6080"},
        {"token", ""},  
        {"password", ""}  
    };
    
    return result;
}

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
    
    virDomainInterfacePtr *ifaces = NULL;
    int ifaces_count = 0;
    
    // Try to get IPs from DHCP leases first 
    ifaces_count = virDomainInterfaceAddresses(domain, &ifaces, 
                                               VIR_DOMAIN_INTERFACE_ADDRESSES_SRC_LEASE, 0);
    
    // If LEASE source fails, try AGENT 
    if (ifaces_count < 0) {
        ifaces_count = virDomainInterfaceAddresses(domain, &ifaces,
                                                   VIR_DOMAIN_INTERFACE_ADDRESSES_SRC_AGENT, 0);
    }
    
    // If both fail, try ARP 
    if (ifaces_count < 0) {
        ifaces_count = virDomainInterfaceAddresses(domain, &ifaces,
                                                   VIR_DOMAIN_INTERFACE_ADDRESSES_SRC_ARP, 0);
    }
    
    if (ifaces_count < 0) {
        result["error"] = "Failed to get IP addresses. Make sure VM is running and has network connectivity.";
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
            
            // Mark if we found at least one IP
            if (addr->addr && strlen(addr->addr) > 0) {
                foundIP = true;
            }
        }
        
        iface["addrs"] = addrs;
        interfaces.push_back(iface);
        
        // Free the interface
        virDomainInterfaceFree(ifaces[i]);
    }
    
    free(ifaces);
    virDomainFree(domain);
    
    if (!foundIP) {
        result["error"] = "No IP addresses found. VM may still be booting.";
        return result;
    }
    
    result["success"] = true;
    result["interfaces"] = interfaces;
    
    // Extract primary IP for convenience (first IPv4 address found)
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
        std::string deleteCmd = "rm -f \"" + diskPath + "\"";
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

// ========================================
// DELETE METHODS
// ========================================

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