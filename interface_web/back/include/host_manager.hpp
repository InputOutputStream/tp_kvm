#ifndef HOST_MANAGER_HPP
#define HOST_MANAGER_HPP

#include <libvirt/libvirt.h>
#include "json.hpp"
#include <vector>
#include <string>
#include <map>

using json = nlohmann::json;

struct HostInfo {
    std::string id;
    std::string uri;
    std::string hostname;
    bool isRemote;
    bool active;
    virConnectPtr conn;
    
    // Resources
    unsigned long totalMemory;    // KB
    unsigned long availableMemory;
    int totalCPUs;
    int availableCPUs;
    long long totalDisk;         // bytes
    long long availableDisk;
    
    // Stats
    int activeVMs;
    int totalVMs;
    double cpuUsage;
    double memoryUsage;
};

enum class HostSelectionStrategy {
    LEAST_USED,      // Host with most free resources (default)
    ROUND_ROBIN,     // Rotate between hosts
    BEST_FIT,         // Host that best matches requested resources
    UNKNOWN
};

class HostManager {
private:
    std::map<std::string, HostInfo> hosts;
    std::string configFile;
    HostSelectionStrategy strategy;
    size_t roundRobinIndex;

    
    void loadHostsFromConfig();
    bool saveHostsToConfig();
    bool updateHostResources(HostInfo& host);
    
public:
    HostManager();
    ~HostManager();
    
    // Host management
    bool addHost(const std::string& uri);
    bool removeHost(const std::string& hostId);
    json listHosts();
    HostInfo* getHost(const std::string& hostId);
    std::string getSelectionStrategy() const;
    bool setSelectionStrategy(int strat);

    std::string findBestHost(int requiredMemory, int requiredCPU, 
                            long long requiredDisk, 
                            HostSelectionStrategy customStrategy = HostSelectionStrategy::LEAST_USED);


    // Resource queries
    json getHostStats(const std::string& hostId);
    json getAllHostsStats();
        
    // Check if resources available across all hosts
    json checkResourceAvailability(int memory, int cpu, long long disk);
    
    // Get connection for specific host
    virConnectPtr getConnection(const std::string& hostId);
    
    // Refresh all hosts
    void refreshAllHosts();

private:
    std::string selectByLeastUsed(int mem, int cpu, long long disk);
    std::string selectByRoundRobin(int mem, int cpu, long long disk);
    std::string selectByBestFit(int mem, int cpu, long long disk);
};

#endif