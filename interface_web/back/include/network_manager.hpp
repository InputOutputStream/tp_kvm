#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include <libvirt/libvirt.h>
#include <string>
#include <map>
#include <vector>

#include "json.hpp"

using json = nlohmann::json;

// Structure to represent a network
struct NetworkInfo {
    std::string networkId;      // Unique identifier
    std::string networkName;    // Libvirt network name
    std::string displayName;    // User-friendly name
    std::string subnet;
    std::string owner;
    std::string type;          // "user" or "swarm"
    time_t created;
    bool active;
};

class NetworkManager {
private:
    virConnectPtr conn;
    std::string networksConfigFile;
    json networksConfig;
    
    // Configuration limits
    const int MAX_USER_NETWORKS = 10;
    const int MAX_SWARM_NETWORKS = 20;
    
    // Load/Save network configurations
    void loadNetworksConfig();
    bool saveNetworksConfig();
    
    // Generate network XML
    std::string generateUserNetworkXML(const std::string& networkId, 
                                      const std::string& displayName,
                                      const std::string& subnet);
    std::string generateSwarmNetworkXML(const std::string& networkId, 
                                       const std::string& displayName,
                                       const std::string& subnet);
    
    // Helper methods
    std::string generateSubnet();
    std::string generateNetworkId();
    bool isSubnetAvailable(const std::string& subnet);
    int getUserNetworkCount(const std::string& username);
    int getSwarmNetworkCount(const std::string& owner);
    std::string sanitizeNetworkName(const std::string& name);
    bool bridgeExists(const std::string& bridgeName);
    bool swarmNetworkNameExists(const std::string& networkName);

public:
    NetworkManager(virConnectPtr connection);
    ~NetworkManager();
    
    // User network management
    json createUserNetwork(const std::string& username, const std::string& networkName = "");
    json getUserNetwork(const std::string& networkId);
    json getUserNetworks(const std::string& username);
    bool deleteUserNetwork(const std::string& networkId, const std::string& username);
    bool networkNameExists(const std::string& username, const std::string& networkName);

    // Swarm cluster network management
    json createSwarmNetwork(const std::string& clusterName, const std::string& owner);
    json getSwarmNetwork(const std::string& networkId);
    bool deleteSwarmNetwork(const std::string& networkId);
    
    // List all networks
    json listAllNetworks();
    json listUserNetworks(const std::string& username);
    
    // Network utilities
    bool isNetworkActive(const std::string& networkName);
    json getNetworkInfo(const std::string& networkId);
    bool updateNetwork(const std::string& networkId, const json& updates);
    
    // Get network by libvirt name
    std::string getNetworkIdByName(const std::string& networkName);
};

#endif // NETWORK_MANAGER_HPP