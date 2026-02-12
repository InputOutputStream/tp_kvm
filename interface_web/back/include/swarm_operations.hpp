#ifndef SWARM_OPERATIONS_HPP
#define SWARM_OPERATIONS_HPP

#include <libvirt/libvirt.h>
#include <string>
#include <vector>
#include "json.hpp"
#include "vm_operations.hpp"
#include "remote_executor.hpp"
#include "network_manager.hpp"
#include "host_manager.hpp"  

using json = nlohmann::json;

struct SwarmNode {
    std::string vmName;
    std::string role;  // "manager" or "worker"
    std::string ip;
    std::string status;
    std::string host;
};

struct SwarmCluster {
    std::string clusterId;
    std::string clusterName;
    std::string owner;
    std::string networkName;
    std::string subnet;
    std::vector<SwarmNode> nodes;
    std::string managerToken;
    std::string workerToken;
    std::string managerIP;
    time_t created;
    std::string status;
};

class SwarmOperations {
private:
    virConnectPtr conn;
    VMOperations* vmOps;
    NetworkManager* networkMgr;
    RemoteExec::RemoteExecutor *remoteExec;
    HostManager *hostManager;
    std::string clustersConfigFile;
    std::map<std::string, SwarmCluster> clusters;
    
    void loadClusters();
    bool saveClusters();
    
    // Cloud-init generation for Docker installation
    std::string generateDockerCloudInit(const std::string& hostname, 
                                       const std::string& username,
                                       const std::string& role);
    
    // Swarm setup commands
    json initializeSwarmOnManager(const std::string& vmName, const std::string& managerIP);
    json joinWorkerToSwarm(const std::string& vmName, const std::string& workerIP,
                          const std::string& managerIP, const std::string& token);
    
    json selectSwarmHost(const std::string& role, const json& nodeConfig);
   
    // Utility
    std::string generateClusterId();
    json waitForVMReady(const std::string& vmName, int maxWaitSeconds = 120);
    json getVMIP(const std::string& vmName);
    
public:
    SwarmOperations(virConnectPtr connection, VMOperations* vmOperations, 
                   NetworkManager* networkManager, RemoteExec::RemoteExecutor *remoteE, HostManager *hostMgr);
    ~SwarmOperations();
    
    json createSwarmClusterWithHosts(const std::string& clusterName, const std::string& owner,
                                    int numManagers, int numWorkers);
   
    // Cluster management
    json createSwarmCluster(const std::string& clusterName, const std::string& owner,
                           int numManagers, int numWorkers);
    json getClusterInfo(const std::string& clusterId);
    json listClusters(const std::string& owner = "");
    json deleteCluster(const std::string& clusterId);
    json createCluster(const json& clusterConfig);
    
    // Node management
    json addWorkerNode(const std::string& clusterId);
    json removeNode(const std::string& clusterId, const std::string& vmName);
    
    // Service management
    json deployService(const std::string& clusterId, const json& serviceConfig);
    json listServices(const std::string& clusterId);
    json getServiceInfo(const std::string& clusterId, const std::string& serviceName);
    json deleteService(const std::string& clusterId, const std::string& serviceName);
};

#endif // SWARM_OPERATIONS_HPP
