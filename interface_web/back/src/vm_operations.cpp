#include "../include/vm_operations.hpp"
#include "../include/utils.hpp"
#include "../include/validation.hpp"
#include "../include/remote_executor.hpp"

#include <regex>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include <libvirt/virterror.h>

VMOperations::VMOperations(virConnectPtr connection) : conn(connection) {}

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



bool VMOperations::deployVM(const json& vmParams) {
    // Create remote executor
    RemoteExec::RemoteExecutor remoteExec(conn);
    
    fprintf(stdout, "📡 Target Host: %s\n\n", remoteExec.getHostInfo().c_str());
    
    // ==========================================
    // STEP 1: VALIDATE CONNECTION
    // ==========================================
    auto connResult = Validation::SystemValidator::checkLibvirtConnection(conn);
    if (!connResult.valid) {
        fprintf(stderr, "❌ %s\n", connResult.error.c_str());
        return false;
    }
    fprintf(stdout, "✅ Libvirt connection verified\n");
    
    // ==========================================
    // STEP 2: VALIDATE INPUT PARAMETERS
    // ==========================================
    fprintf(stdout, "\n🔍 Validating input parameters...\n");
    
    auto validationResult = Validation::Validator::validateDeploymentParams(vmParams);
    if (!validationResult.valid) {
        fprintf(stderr, "❌ Validation failed: %s\n", validationResult.error.c_str());
        return false;
    }
    
    // Show warnings
    for (const auto& warning : validationResult.warnings) {
        fprintf(stdout, "⚠️  Warning: %s\n", warning.c_str());
    }
    
    fprintf(stdout, "✅ Input parameters validated\n");
    
    // Extract parameters
    std::string hostname = vmParams["hostname"];
    std::string actualHostname = vmParams["owner"].get<std::string>() + "-" + hostname;

    int memory = vmParams["memory"];
    int vcpus = vmParams["vcpus"];
    int disk = vmParams["disk"];
    std::string username = vmParams.value("username", "ubuntu");
    std::string authMethod = vmParams.value("authMethod", "password");
    std::string password = vmParams.value("password", "");
    std::string sshKey = vmParams.value("sshKey", "");
    
    // ==========================================
    // STEP 3: CHECK VM NAME AVAILABILITY
    // ==========================================
    fprintf(stdout, "\n🔍 Checking VM name availability...\n");
    
    auto nameResult = Validation::SystemValidator::checkVMNameAvailable(conn, hostname);
    if (!nameResult.valid) {
        fprintf(stderr, "❌ %s\n", nameResult.error.c_str());
        fprintf(stderr, "   Suggestion: Choose a different hostname or delete the existing VM\n");
        return false;
    }
    fprintf(stdout, "✅ VM name '%s' is available\n", hostname.c_str());
    
    // ==========================================
    // STEP 4: CHECK REQUIRED DIRECTORIES (REMOTE)
    // ==========================================
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
    
    // ==========================================
    // STEP 5: CHECK REQUIRED TOOLS (REMOTE)
    // ==========================================
    fprintf(stdout, "\n🔍 Checking required tools on target host...\n");
    
    std::vector<std::string> requiredTools = {
        "qemu-img",
        "genisoimage",
        "mkpasswd"
    };
    
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
    
    // ==========================================
    // STEP 6: VALIDATE BASE IMAGE (REMOTE)
    // ==========================================
    fprintf(stdout, "\n🔍 Validating base image on target host...\n");
    
    std::string baseImagePath = "/var/lib/libvirt/images/baseimg/ubuntu-22.04-server-cloudimg-amd64.img";
    
    if (!remoteExec.fileExists(baseImagePath)) {
        fprintf(stderr, "❌ Base image not found on target host: %s\n", baseImagePath.c_str());
        fprintf(stderr, "\n📥 On the target host, download the base image:\n");
        fprintf(stderr, "   cd /var/lib/libvirt/images/baseimg\n");
        fprintf(stderr, "   sudo wget https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-amd64.img \\\n");
        fprintf(stderr, "        -O ubuntu-22.04-server-cloudimg-amd64.img\n");
        fprintf(stderr, "\nOr run the setup script on the target host:\n");
        fprintf(stderr, "   sudo bash setup-base-images.sh\n");
        return false;
    }
    
    if (!remoteExec.isValidDiskImage(baseImagePath)) {
        fprintf(stderr, "❌ Base image is corrupted or invalid: %s\n", baseImagePath.c_str());
        fprintf(stderr, "   Re-download the image on the target host\n");
        return false;
    }
    
    fprintf(stdout, "✅ Base image is valid: %s\n", baseImagePath.c_str());
    
    // ==========================================
    // STEP 7: CHECK DISK SPACE (REMOTE)
    // ==========================================
    fprintf(stdout, "\n🔍 Checking available disk space on target host...\n");
    
    long long requiredBytes = (long long)disk * 1024 * 1024 * 1024;  // Convert GB to bytes
    requiredBytes += 1024 * 1024 * 1024;  // Add 1GB buffer for cloud-init ISO, etc.
    
    long long availableBytes = remoteExec.getAvailableDiskSpace("/var/lib/libvirt/images");
    
    if (availableBytes < 0) {
        fprintf(stdout, "⚠️  Could not verify disk space. Proceeding with deployment...\n");
    } else if (availableBytes < requiredBytes) {
        fprintf(stderr, "❌ Insufficient disk space on target host.\n");
        fprintf(stderr, "   Required: %.2f GB\n", requiredBytes / (1024.0*1024.0*1024.0));
        fprintf(stderr, "   Available: %.2f GB\n", availableBytes / (1024.0*1024.0*1024.0));
        return false;
    } else {
        fprintf(stdout, "✅ Sufficient disk space available on target host\n");
        fprintf(stdout, "   Available: %.2f GB\n", availableBytes / (1024.0*1024.0*1024.0));
        
        // Warn if less than 10GB free after allocation
        long long remainingBytes = availableBytes - requiredBytes;
        if (remainingBytes < 10LL * 1024 * 1024 * 1024) {
            fprintf(stdout, "⚠️  Warning: Less than 10GB will remain after allocation\n");
        }
    }
    
    // ==========================================
    // STEP 8: CHECK NETWORK
    // ==========================================
    fprintf(stdout, "\n🔍 Checking default network...\n");
    
    auto networkResult = Validation::SystemValidator::checkNetworkAvailable(conn, "default");
    if (!networkResult.valid) {
        fprintf(stderr, "❌ %s\n", networkResult.error.c_str());
        return false;
    }
    fprintf(stdout, "✅ Network 'default' is active on target host\n");
    
    // ==========================================
    // STEP 9: BEGIN DEPLOYMENT
    // ==========================================
    fprintf(stdout, "📋 Configuration:\n");
    fprintf(stdout, "   Hostname: %s\n", hostname.c_str());
    fprintf(stdout, "   Memory: %d MB\n", memory);
    fprintf(stdout, "   vCPUs: %d\n", vcpus);
    fprintf(stdout, "   Disk: %d GB\n", disk);
    fprintf(stdout, "   Username: %s\n", username.c_str());
    fprintf(stdout, "   Auth: %s\n", authMethod.c_str());
    fprintf(stdout, "\n");
    
    try {
        // Paths on the target host
        std::string diskPath = "/var/lib/libvirt/images/" + hostname + ".qcow2";
        std::string cloudInitPath = "/var/lib/libvirt/images/cloud-init-iso/" + hostname + "-cloudinit.iso";
        
        // Step 1: Create cloud-init configuration
        fprintf(stdout, "📝 Step 1/7: Creating cloud-init configuration...\n");
        
        std::string cloudInitDir = "/tmp/cloudinit-" + hostname;
        
        // Create directory on target host
        auto mkdirResult = remoteExec.execute("mkdir -p " + cloudInitDir);
        if (!mkdirResult.success()) {
            fprintf(stderr, "   ❌ Failed to create temp directory on target host\n");
            return false;
        }
        
        // Create meta-data file
        std::stringstream metaData;
        metaData << "instance-id: " << hostname << "\n"
                 << "local-hostname: " << hostname << "\n";
        
        // Create user-data file
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
        
        if (authMethod == "password" && !password.empty()) {
            // Generate password hash on target host
            std::string hashCmd = "mkpasswd --method=SHA-512 --rounds=4096 '" + password + "'";
            auto hashResult = remoteExec.execute(hashCmd);
            
            if (!hashResult.success()) {
                fprintf(stderr, "   ❌ Failed to generate password hash on target host\n");
                return false;
            }
            
            std::string hashedPassword = hashResult.output;
            hashedPassword.erase(hashedPassword.find_last_not_of("\n\r") + 1);
            
            userData << "    passwd: " << hashedPassword << "\n"
                     << "    lock_passwd: false\n";
        } else if (authMethod == "ssh-key" && !sshKey.empty()) {
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
        
        // Write files to target host using echo and cat
        std::string metaDataContent = metaData.str();
        std::string userDataContent = userData.str();
        
        // Escape single quotes in content
        auto escapeSingleQuotes = [](const std::string& str) {
            std::string escaped = str;
            size_t pos = 0;
            while ((pos = escaped.find("'", pos)) != std::string::npos) {
                escaped.replace(pos, 1, "'\\''");
                pos += 4;
            }
            return escaped;
        };
        
        std::string writeMetaCmd = "cat > " + cloudInitDir + "/meta-data << 'EOF'\n" + metaDataContent + "\nEOF";
        std::string writeUserCmd = "cat > " + cloudInitDir + "/user-data << 'EOF'\n" + userDataContent + "\nEOF";
        
        auto writeMetaResult = remoteExec.execute(writeMetaCmd);
        auto writeUserResult = remoteExec.execute(writeUserCmd);
        
        if (!writeMetaResult.success() || !writeUserResult.success()) {
            fprintf(stderr, "   ❌ Failed to write cloud-init files on target host\n");
            return false;
        }
        
        fprintf(stdout, "   ✅ Cloud-init configuration created\n");
        
        // Step 2: Create cloud-init ISO
        fprintf(stdout, "📝 Step 2/7: Creating cloud-init ISO...\n");
        
        std::string createIsoCmd = "genisoimage -output " + cloudInitPath + 
                                   " -volid cidata -joliet -rock " + 
                                   cloudInitDir + "/user-data " + 
                                   cloudInitDir + "/meta-data 2>&1";
        auto isoResult = remoteExec.execute(createIsoCmd);
        
        if (!isoResult.success()) {
            fprintf(stderr, "   ❌ Failed to create cloud-init ISO: %s\n", isoResult.output.c_str());
            return false;
        }
        
        fprintf(stdout, "   ✅ Cloud-init ISO created\n");
        
        // Clean up temp directory
        remoteExec.execute("rm -rf " + cloudInitDir);
        
        // Step 3: Copy base image
        fprintf(stdout, "📝 Step 3/7: Copying base cloud image...\n");
        
        std::string copyCmd = "cp " + baseImagePath + " " + diskPath;
        auto copyResult = remoteExec.execute(copyCmd);
        
        if (!copyResult.success()) {
            fprintf(stderr, "   ❌ Failed to copy base image: %s\n", copyResult.output.c_str());
            return false;
        }
        
        fprintf(stdout, "   ✅ Base image copied\n");
        
        // Step 4: Resize disk
        fprintf(stdout, "📝 Step 4/7: Resizing disk to %dGB...\n", disk);
        
        std::string resizeCmd = "qemu-img resize " + diskPath + " " + std::to_string(disk) + "G";
        auto resizeResult = remoteExec.execute(resizeCmd);
        
        if (!resizeResult.success()) {
            fprintf(stderr, "   ❌ Failed to resize disk: %s\n", resizeResult.output.c_str());
            return false;
        }
        
        fprintf(stdout, "   ✅ Disk resized\n");
        
        // Step 5: Create domain XML
        fprintf(stdout, "📝 Step 5/7: Creating VM definition...\n");
        
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
                  << "      <source network='default'/>"
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
                  << "    <graphics type='vnc' port='-1' autoport='yes' listen='0.0.0.0'/>"
                  << "  </devices>"
                  << "</domain>";
        
        std::string xml = xmlConfig.str();
        
        fprintf(stdout, "   ✅ VM definition created\n");
        
        // Step 6: Define the domain
        fprintf(stdout, "📝 Step 6/7: Defining VM in libvirt...\n");
        
        virDomainPtr domain = virDomainDefineXML(conn, xml.c_str());
        if (!domain) {
            virErrorPtr err = virGetLastError();
            if (err) {
                fprintf(stderr, "   ❌ Failed to define domain: %s\n", err->message);
            }
            return false;
        }
        
        fprintf(stdout, "   ✅ VM defined in libvirt\n");
        
        // Step 7: Start the VM
        fprintf(stdout, "📝 Step 7/7: Starting VM...\n");
        
        if (virDomainCreate(domain) < 0) {
            virErrorPtr err = virGetLastError();
            if (err) {
                fprintf(stderr, "   ❌ Failed to start domain: %s\n", err->message);
            }
            virDomainFree(domain);
            return false;
        }
        
        
        virDomainFree(domain);
        
        return true;
        
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
    
    char* xmlDesc = virDomainGetXMLDesc(domain, 0);
    std::string xml(xmlDesc);
    free(xmlDesc);
    virDomainFree(domain);
    
    std::regex portRegex("<graphics type='vnc' port='(\\d+)'");
    std::smatch match;
    
    if (std::regex_search(xml, match, portRegex) && match[1].str() != "-1") {
        int port = std::stoi(match[1].str());
        std::string display = ":" + std::to_string(port - 5900);
        
        result["success"] = true;
        result["display"] = display;
        result["port"] = port;
        result["host"] = "localhost";
    } else {
        result["error"] = "VNC not configured or VM not running";
    }
    
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
    
    // If LEASE source fails, try AGENT (requires qemu-guest-agent in VM)
    if (ifaces_count < 0) {
        ifaces_count = virDomainInterfaceAddresses(domain, &ifaces,
                                                   VIR_DOMAIN_INTERFACE_ADDRESSES_SRC_AGENT, 0);
    }
    
    // If both fail, try ARP (less reliable)
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
    
    for (int i = 0; i < ifaces_count; i++) {
        json iface;
        iface["name"] = ifaces[i]->name;
        iface["hwaddr"] = ifaces[i]->hwaddr ? ifaces[i]->hwaddr : "";
        
        json addrs = json::array();
        for (int j = 0; j < ifaces[i]->naddrs; j++) {
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
        if (diskPath.find(".iso") == std::string::npos || 
            diskPath.find("cloud-init") != std::string::npos) {
            diskPaths.push_back(diskPath);
            fprintf(stdout, "Found disk: %s\n", diskPath.c_str());
        }
        
        ++iter;
    }
    
    return diskPaths;
}

bool VMOperations::deleteDiskFiles(const std::vector<std::string>& diskPaths) {
    if (diskPaths.empty()) {
        fprintf(stdout, "No disk files to delete\n");
        return true;
    }
    
    bool allSuccess = true;
    
    for (const auto& diskPath : diskPaths) {
        // Check if file exists
        struct stat buffer;
        if (stat(diskPath.c_str(), &buffer) != 0) {
            fprintf(stdout, "Disk file does not exist (already deleted?): %s\n", diskPath.c_str());
            continue;
        }
        
        fprintf(stdout, "Deleting disk file: %s\n", diskPath.c_str());
        
        if (unlink(diskPath.c_str()) != 0) {
            fprintf(stderr, "Failed to delete disk file: %s (error: %s)\n", 
                    diskPath.c_str(), strerror(errno));
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

json VMOperations::deleteVM(const std::string& name, bool removeDisks) {
    json result;
    result["success"] = false;
    result["steps"] = json::array();
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    fprintf(stdout, "\n========================================\n");
    fprintf(stdout, "Starting deletion process for VM: %s\n", name.c_str());
    fprintf(stdout, "Remove disks: %s\n", removeDisks ? "YES" : "NO");
    fprintf(stdout, "========================================\n\n");
    
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
        // Continue anyway, as this is not critical
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
