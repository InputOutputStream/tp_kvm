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

std::string VMOperations::getStateString(int state) {
    const char* states[] = {"no state", "running", "blocked", "paused", 
                           "shutdown", "shut off", "crashed", "pmsuspended"};
    if (state >= 0 && state < 8) {
        return states[state];
    }
    return "unknown";
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

std::string VMOperations::getFirstNetworkInterface(virDomainPtr domain) {
    char* xmlDesc = virDomainGetXMLDesc(domain, 0);
    if (!xmlDesc) {
        return "";
    }
    
    std::string xml(xmlDesc);
    free(xmlDesc);
    
    // Look for target dev in XML: <target dev='vnet0'/> or <target dev='macvtap0'/>
    std::regex targetRegex("<target dev='([^']+)'");
    std::smatch match;
    
    if (std::regex_search(xml, match, targetRegex)) {
        return match[1].str();
    }
    
    return "";
}

json VMOperations::getVMStatsInternal(virDomainPtr domain, const std::string& vmName) {
    json stats;
    
    virDomainInfo info;
    if (virDomainGetInfo(domain, &info) < 0) {
        stats["error"] = "Failed to get domain info";
        return stats;
    }
    
    stats["cpuCount"] = info.nrVirtCpu;
    stats["memory"] = info.memory / 1024; // MB
    
    // CPU usage calculation with mutex
    virDomainInfo newInfo;
    if (virDomainGetInfo(domain, &newInfo) == 0) {
        long long currentTime = getCurrentTimeMs();
        unsigned long long currentCpuTime = newInfo.cpuTime;
        
        std::lock_guard<std::mutex> lock(statsCacheMutex);  // LOCK HERE
        
        auto it = statsCache.find(vmName);
        if (it != statsCache.end()) {
            long long timeDiff = currentTime - it->second.timestamp;
            unsigned long long cpuDiff = currentCpuTime - it->second.cpuTime;
            
            if (timeDiff > 0) {
                double cpuUsage = (cpuDiff * 100.0) / (timeDiff * 1000000.0 * info.nrVirtCpu);
                stats["cpuUsage"] = cpuUsage;
            }
        }
        
        statsCache[vmName] = {currentCpuTime, currentTime};
    }
    
    return stats;
}

std::vector<std::string> VMOperations::getDiskPaths(virDomainPtr domain) {
    std::vector<std::string> paths;
    
    char* xmlDesc = virDomainGetXMLDesc(domain, 0);
    if (!xmlDesc) {
        std::cerr << "Failed to get domain XML" << std::endl;
        return paths;
    }
    
    std::string xml(xmlDesc);
    free(xmlDesc);  // CRITICAL: Free immediately after copying
    
    // Extract disk paths from XML
    std::regex diskRegex("<source [^>]*file='([^']+)'");
    std::smatch match;
    std::string::const_iterator searchStart(xml.cbegin());
    
    while (std::regex_search(searchStart, xml.cend(), match, diskRegex)) {
        paths.push_back(match[1].str());
        searchStart = match.suffix().first;
    }
    
    return paths;
}

bool VMOperations::validateXML(const std::string& xml) {
    // Basic XML validation
    if (xml.empty()) return false;
    if (xml.find("<domain") == std::string::npos) return false;
    if (xml.find("</domain>") == std::string::npos) return false;
    
    // Check for suspicious content
    if (xml.find("<script") != std::string::npos) return false;
    if (xml.find("javascript:") != std::string::npos) return false;
    
    return true;
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
              << "    <graphics type='vnc' port='-1' autoport='yes' listen='0.0.0.0'>\n"
              << "      <listen type='address' address='0.0.0.0'/>\n"
              << "    </graphics>\n"
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
