#include "../include/host_manager.hpp"
#include "../include/utils.hpp"
#include "../include/remote_executor.hpp"
#include <fstream>
#include <sys/stat.h>
#include <iostream>

HostManager::HostManager() 
    : configFile("/var/lib/thoth-cloud/hosts.json") {
    loadHostsFromConfig();
}

HostManager::~HostManager() {
    for (auto& [id, host] : hosts) {
        if (host.conn) {
            virConnectClose(host.conn);
        }
    }
}

void HostManager::loadHostsFromConfig() {
    std::ifstream file(configFile);
    if (file.is_open()) {
        try {
            json config;
            file >> config;
            
            for (const auto& hostData : config["hosts"]) {
                std::string uri = hostData["uri"];
                addHost(uri);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error loading hosts config: " << e.what() << std::endl;
        }
    }
}

bool HostManager::saveHostsToConfig() {
    mkdir("/var/lib/thoth-cloud", 0755);
    
    json config;
    config["hosts"] = json::array();
    
    for (const auto& [id, host] : hosts) {
        config["hosts"].push_back({
            {"id", id},
            {"uri", host.uri},
            {"hostname", host.hostname}
        });
    }
    
    std::ofstream file(configFile);
    if (!file.is_open()) return false;
    
    file << config.dump(2);
    return true;
}

bool HostManager::addHost(const std::string& uri) {
    virConnectPtr conn = virConnectOpen(uri.c_str());
    if (!conn) {
        std::cerr << "Failed to connect to: " << uri << std::endl;
        return false;
    }
    
    char* hostname = virConnectGetHostname(conn);
    if (!hostname) {
        virConnectClose(conn);
        return false;
    }
    
    std::string hostId = std::string(hostname);
    
    HostInfo host;
    host.id = hostId;
    host.uri = uri;
    host.hostname = hostname;
    host.isRemote = (uri.find("ssh") != std::string::npos);
    host.active = true;
    host.conn = conn;
    
    free(hostname);
    
    updateHostResources(host);
    hosts[hostId] = host;
    
    saveHostsToConfig();
    
    std::cout << "✅ Added host: " << hostId << " (" << uri << ")" << std::endl;
    return true;
}

bool HostManager::updateHostResources(HostInfo& host) {
    if (!host.conn) return false;
    
    virNodeInfo nodeInfo;
    if (virNodeGetInfo(host.conn, &nodeInfo) < 0) {
        return false;
    }
    
    host.totalMemory = nodeInfo.memory;
    host.totalCPUs = nodeInfo.cpus;
    
    // Get free memory
    virNodeMemoryStatsPtr params = nullptr;
    int nparams = 0;
    
    if (virNodeGetMemoryStats(host.conn, VIR_NODE_MEMORY_STATS_ALL_CELLS,
                              nullptr, &nparams, 0) == 0 && nparams > 0) {
        params = (virNodeMemoryStatsPtr)malloc(sizeof(*params) * nparams);
        if (virNodeGetMemoryStats(host.conn, VIR_NODE_MEMORY_STATS_ALL_CELLS,
                                  params, &nparams, 0) == 0) {
            for (int i = 0; i < nparams; i++) {
                if (strcmp(params[i].field, VIR_NODE_MEMORY_STATS_FREE) == 0) {
                    host.availableMemory = params[i].value;
                }
            }
        }
        free(params);
    }
    
    // Get disk space using remote executor
    RemoteExec::RemoteExecutor exec(host.conn);
    host.availableDisk = exec.getAvailableDiskSpace("/var/lib/libvirt/images");
    host.totalDisk = host.availableDisk; 
    
    // Get VM counts
    virDomainPtr* domains;
    int numDomains = virConnectListAllDomains(host.conn, &domains, 0);
    if (numDomains >= 0) {
        host.totalVMs = numDomains;
        host.activeVMs = 0;
        
        for (int i = 0; i < numDomains; i++) {
            virDomainInfo info;
            if (virDomainGetInfo(domains[i], &info) == 0) {
                if (info.state == VIR_DOMAIN_RUNNING) {
                    host.activeVMs++;
                    host.availableCPUs -= info.nrVirtCpu;
                    host.availableMemory -= info.memory;
                }
            }
            virDomainFree(domains[i]);
        }
        free(domains);
    }
    
    host.availableCPUs = host.totalCPUs; // Simplified cause should track actual usage, willupdate it later
    host.cpuUsage = (host.totalCPUs - host.availableCPUs) * 100.0 / host.totalCPUs;
    host.memoryUsage = (host.totalMemory - host.availableMemory) * 100.0 / host.totalMemory;
    
    return true;
}

std::string HostManager::findBestHost(int requiredMemory, int requiredCPU, long long requiredDisk) {
    std::string bestHost;
    double bestScore = -1;
    
    for (auto& [id, host] : hosts) {
        if (!host.active) continue;
        
        updateHostResources(host);
        
        // Check if host has enough resources
        if (host.availableMemory < (unsigned long)(requiredMemory * 1024) ||
            host.availableCPUs < requiredCPU ||
            host.availableDisk < requiredDisk) {
            continue;
        }
        
        // Score: prefer hosts with more free resources
        double score = (host.availableMemory / (double)host.totalMemory) * 0.4 +
                      (host.availableCPUs / (double)host.totalCPUs) * 0.4 +
                      (host.availableDisk / (double)host.totalDisk) * 0.2;
        
        if (score > bestScore) {
            bestScore = score;
            bestHost = id;
        }
    }
    
    return bestHost;
}

json HostManager::checkResourceAvailability(int memory, int cpu, long long disk) {
    json result;
    result["available"] = false;
    result["hosts"] = json::array();
    
    for (auto& [id, host] : hosts) {
        if (!host.active) continue;
        
        updateHostResources(host);
        
        bool canAccommodate = 
            host.availableMemory >= (unsigned long)(memory * 1024) &&
            host.availableCPUs >= cpu &&
            host.availableDisk >= disk;
        
        json hostInfo = {
            {"id", id},
            {"hostname", host.hostname},
            {"canAccommodate", canAccommodate},
            {"availableMemory", host.availableMemory / 1024}, // MB
            {"availableCPUs", host.availableCPUs},
            {"availableDisk", host.availableDisk / (1024*1024*1024)} // GB
        };
        
        result["hosts"].push_back(hostInfo);
        
        if (canAccommodate) {
            result["available"] = true;
        }
    }
    
    return result;
}

json HostManager::getHostStats(const std::string& hostId) {
    json result;
    result["success"] = false;
    
    auto it = hosts.find(hostId);
    if (it == hosts.end()) {
        result["error"] = "Host not found";
        return result;
    }
    
    HostInfo& host = it->second;
    updateHostResources(host);
    
    result["success"] = true;
    result["host"] = {
        {"id", host.id},
        {"uri", host.uri},
        {"hostname", host.hostname},
        {"active", host.active},
        {"totalMemory", host.totalMemory / 1024}, // MB
        {"availableMemory", host.availableMemory / 1024},
        {"totalCPUs", host.totalCPUs},
        {"availableCPUs", host.availableCPUs},
        {"totalDisk", host.totalDisk / (1024*1024*1024)}, // GB
        {"availableDisk", host.availableDisk / (1024*1024*1024)},
        {"activeVMs", host.activeVMs},
        {"totalVMs", host.totalVMs},
        {"cpuUsage", host.cpuUsage},
        {"memoryUsage", host.memoryUsage}
    };
    
    return result;
}

json HostManager::getAllHostsStats() {
    json result;
    result["success"] = true;
    result["hosts"] = json::array();
    
    for (auto& [id, host] : hosts) {
        json stats = getHostStats(id);
        if (stats["success"].get<bool>()) {
            result["hosts"].push_back(stats["host"]);
        }
    }
    
    return result;
}

virConnectPtr HostManager::getConnection(const std::string& hostId) {
    auto it = hosts.find(hostId);
    if (it == hosts.end()) return nullptr;
    return it->second.conn;
}

void HostManager::refreshAllHosts() {
    for (auto& [id, host] : hosts) {
        updateHostResources(host);
    }
}

json HostManager::listHosts() {
    json result;
    result["success"] = true;
    result["hosts"] = json::array();
    
    for (const auto& [id, host] : hosts) {
        result["hosts"].push_back({
            {"id", id},
            {"uri", host.uri},
            {"hostname", host.hostname},
            {"active", host.active}
        });
    }
    
    return result;
}

bool HostManager::removeHost(const std::string& hostId) {
    auto it = hosts.find(hostId);
    if (it == hosts.end()) return false;
    
    if (it->second.conn) {
        virConnectClose(it->second.conn);
    }
    
    hosts.erase(it);
    saveHostsToConfig();
    return true;
}

HostInfo* HostManager::getHost(const std::string& hostId) {
    auto it = hosts.find(hostId);
    if (it == hosts.end()) return nullptr;
    return &it->second;
}