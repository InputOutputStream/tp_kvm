#include "../include/vm_operations.hpp"
#include "../include/utils.hpp"
#include <regex>
#include <sstream>
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
    
    json vms = json::array();
    
    for (int i = 0; i < numDomains; i++) {
        const char* name = virDomainGetName(domains[i]);
        virDomainInfo info;
        virDomainGetInfo(domains[i], &info);
        
        int id = virDomainGetID(domains[i]);
        std::string state = getStateString(info.state);
        bool isRunning = (info.state == VIR_DOMAIN_RUNNING);
        
        json vm = {
            {"id", id},
            {"name", name},
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
    return result;
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
    if (!conn) {
        return false;
    }

    try {
        // Extract parameters
        std::string hostname = vmParams["hostname"];
        int memory = vmParams["memory"];  // in MB
        int vcpus = vmParams["vcpus"];
        int disk = vmParams["disk"];  // in GB
        
        std::string diskPath = "/var/lib/libvirt/images/" + hostname + ".qcow2";
        
        // Get the default storage pool
        virStoragePoolPtr pool = virStoragePoolLookupByName(conn, "default");
        if (!pool) {
            fprintf(stderr,"Failed to find default storage pool\n");
            return false;
        }
        
        // Make sure pool is active
        if (virStoragePoolIsActive(pool) != 1) {
            if (virStoragePoolCreate(pool, 0) < 0) {
                fprintf(stderr, "Failed to activate storage pool\n");
                virStoragePoolFree(pool);
                return false;
            }
        }
        
        // Create storage volume XML
        std::stringstream volXml;
        volXml << "<volume>"
               << "  <name>" << hostname << ".qcow2</name>"
               << "  <capacity unit='G'>" << disk << "</capacity>"
               << "  <target>"
               << "    <format type='qcow2'/>"
               << "    <permissions>"
               << "      <mode>0644</mode>"
               << "    </permissions>"
               << "  </target>"
               << "</volume>";
        
        // Create the volume
        virStorageVolPtr vol = virStorageVolCreateXML(pool, volXml.str().c_str(), 0);
        if (!vol) {
            virErrorPtr err = virGetLastError();
            if (err) {
                fprintf(stderr, "Failed to create storage volume: %s", err->message);
            }
            virStoragePoolFree(pool);
            return false;
        }
        
        // Get the actual path of the created volume
        char* path = virStorageVolGetPath(vol);
        if (path) {
            diskPath = path;
            free(path);
        }
        
        virStorageVolFree(vol);
        virStoragePoolFree(pool);
        
        fprintf(stderr, "Created disk image: %s", diskPath.c_str());
        // Create domain XML configuration
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
                  << "    <interface type='network'>"
                  << "      <source network='default'/>"
                  << "      <model type='virtio'/>"
                  << "    </interface>"
                  << "    <console type='pty'>"
                  << "      <target type='serial' port='0'/>"
                  << "    </console>"
                  << "    <graphics type='vnc' port='-1' autoport='yes' listen='0.0.0.0'/>"
                  << "  </devices>"
                  << "</domain>";
        
        std::string xml = xmlConfig.str();
        
        // Define the domain
        virDomainPtr domain = virDomainDefineXML(conn, xml.c_str());
        if (!domain) {
            virErrorPtr err = virGetLastError();
            if (err) {
                fprintf(stderr, "Failed to define domain: %s", err->message);
            }
            return false;
        }
        
        // Start the VM
        if (virDomainCreate(domain) < 0) {
            virErrorPtr err = virGetLastError();
            if (err) {
                fprintf(stderr, "Failed to start domain: %s", err->message);
            }
            virDomainFree(domain);
            return false;
        }
        fprintf(stderr, "VM %s", hostname.c_str());
        fprintf(stderr, " deployed and started successfully \n");
        
        virDomainFree(domain);
        return true;
        
    } catch (const std::exception& e) {
        fprintf(stderr, "Exception in deployVM: %s", e.what());
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