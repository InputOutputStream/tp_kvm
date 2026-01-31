#include "../include/network_manager.hpp"
#include <libvirt/virterror.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <iostream>
#include <regex>
#include <random>
#include <chrono>

NetworkManager::NetworkManager(virConnectPtr connection) 
    : conn(connection), networksConfigFile("/var/lib/thoth-cloud/networks.json") {
    loadNetworksConfig();
}

NetworkManager::~NetworkManager() {
    saveNetworksConfig();
}

void NetworkManager::loadNetworksConfig() {
    std::ifstream file(networksConfigFile);
    if (file.is_open()) {
        // Check if file is empty
        file.seekg(0, std::ios::end);
        if (file.tellg() == 0) {
            // File is empty, initialize with default structure
            networksConfig = json::object();
            networksConfig["userNetworks"] = json::array();
            networksConfig["swarmNetworks"] = json::array();
            networksConfig["usedSubnets"] = json::array();
            file.close();
            return;
        }
        
        file.seekg(0, std::ios::beg); // Reset to beginning
        try {
            file >> networksConfig;
            
            // Ensure all required fields exist
            if (!networksConfig.contains("userNetworks")) {
                networksConfig["userNetworks"] = json::array();
            }
            if (!networksConfig.contains("swarmNetworks")) {
                networksConfig["swarmNetworks"] = json::array();
            }
            if (!networksConfig.contains("usedSubnets")) {
                networksConfig["usedSubnets"] = json::array();
            }
            
            // Migrate old format if needed
            if (networksConfig.contains("userNetworks") && 
                !networksConfig["userNetworks"].is_array()) {
                json oldUserNetworks = networksConfig["userNetworks"];
                networksConfig["userNetworks"] = json::array();
                
                for (auto& [username, network] : oldUserNetworks.items()) {
                    if (network.is_object()) {
                        network["owner"] = username;
                        network["type"] = "user";
                        if (!network.contains("networkId")) {
                            network["networkId"] = generateNetworkId();
                        }
                        if (!network.contains("displayName")) {
                            network["displayName"] = "Network 1";
                        }
                        networksConfig["userNetworks"].push_back(network);
                    }
                }
                saveNetworksConfig();
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error loading networks config: " << e.what() << std::endl;
            networksConfig = json::object();
            networksConfig["userNetworks"] = json::array();
            networksConfig["swarmNetworks"] = json::array();
            networksConfig["usedSubnets"] = json::array();
        }
        file.close();
    } else {
        networksConfig = json::object();
        networksConfig["userNetworks"] = json::array();
        networksConfig["swarmNetworks"] = json::array();
        networksConfig["usedSubnets"] = json::array();
    }
}

bool NetworkManager::saveNetworksConfig() {
    mkdir("/var/lib/thoth-cloud", 0755);
    
    std::ofstream file(networksConfigFile);
    if (!file.is_open()) {
        std::cerr << "Failed to save networks config" << std::endl;
        return false;
    }
    
    file << networksConfig.dump(2);
    file.close();
    return true;
}

std::string NetworkManager::generateNetworkId() {
    // Generate unique network ID using timestamp + random
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    return "net_" + std::to_string(timestamp) + "_" + std::to_string(dis(gen));
}

std::string NetworkManager::generateSubnet() {
    // Generate subnet using 10.x.y.0/24 range for user networks
    // and 172.16-31.x.0/24 for swarm networks
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Try user network range first (10.100-255.x.0/24)
    std::uniform_int_distribution<> x_dist(100, 255);
    std::uniform_int_distribution<> y_dist(0, 255);
    
    int attempts = 0;
    while (attempts < 1000) {
        int x = x_dist(gen);
        int y = y_dist(gen);
        std::string subnet = "10." + std::to_string(x) + "." + std::to_string(y) + ".0";
        
        if (isSubnetAvailable(subnet)) {
            return subnet;
        }
        attempts++;
    }
    
    // Fallback: try swarm range (172.16-31.x.0/24)
    std::uniform_int_distribution<> x2_dist(16, 31);
    attempts = 0;
    while (attempts < 1000) {
        int x = x2_dist(gen);
        int y = y_dist(gen);
        std::string subnet = "172." + std::to_string(x) + "." + std::to_string(y) + ".0";
        
        if (isSubnetAvailable(subnet)) {
            return subnet;
        }
        attempts++;
    }
    
    std::cerr << "Failed to generate available subnet" << std::endl;
    return "";
}

bool NetworkManager::isSubnetAvailable(const std::string& subnet) {
    for (const auto& usedSubnet : networksConfig["usedSubnets"]) {
        if (usedSubnet.get<std::string>() == subnet) {
            return false;
        }
    }
    return true;
}




int NetworkManager::getUserNetworkCount(const std::string& username) {
    int count = 0;
    if (!networksConfig.contains("userNetworks") || !networksConfig["userNetworks"].is_array()) {
        return 0;
    }
    
    for (const auto& network : networksConfig["userNetworks"]) {
        if (network.contains("owner") && network["owner"] == username) {
            count++;
        }
    }
    return count;
}

int NetworkManager::getSwarmNetworkCount(const std::string& owner) {
    int count = 0;
    if (!networksConfig.contains("swarmNetworks") || !networksConfig["swarmNetworks"].is_array()) {
        return 0;
    }
    
    for (const auto& network : networksConfig["swarmNetworks"]) {
        if (network.contains("owner") && network["owner"] == owner) {
            count++;
        }
    }
    return count;
}

std::string NetworkManager::sanitizeNetworkName(const std::string& name) {
    // Remove special characters, keep only alphanumeric, dash, underscore
    std::regex pattern("[^a-zA-Z0-9_-]");
    std::string sanitized = std::regex_replace(name, pattern, "_");
    
    // Limit length
    if (sanitized.length() > 30) {
        sanitized = sanitized.substr(0, 30);
    }
    
    return sanitized;
}

bool NetworkManager::bridgeExists(const std::string& bridgeName) {
    std::string cmd = "ip link show " + bridgeName + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;
    
    char buffer[128];
    bool exists = false;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        if (strstr(buffer, bridgeName.c_str()) != nullptr) {
            exists = true;
            break;
        }
    }
    pclose(pipe);
    return exists;
}

std::string NetworkManager::generateUserNetworkXML(const std::string& networkId,
                                                   const std::string& displayName,
                                                   const std::string& subnet) {
    std::stringstream xml;
    // std::string networkName = "user_" + networkId;
    std::string networkName = displayName + networkId;

    // Extract subnet parts (10.x.y.0 -> 10.x.y)
    std::regex subnetRegex(R"((\d+\.\d+\.\d+)\.\d+)");
    std::smatch match;
    std::string baseIP;
    
    if (std::regex_match(subnet, match, subnetRegex)) {
        baseIP = match[1].str();
    }

    // Generate unique bridge name
    std::string bridgeName;
    int attempt = 0;
    do {
        std::string suffix = std::to_string(std::hash<std::string>{}(networkId + std::to_string(attempt)));
        bridgeName = "vbr" + suffix.substr(0, 8);
        attempt++;
    } while (bridgeName.length() > 15 && attempt < 10); // Linux bridge max 15 chars
    
    if (bridgeName.length() > 15) {
        bridgeName = bridgeName.substr(0, 15);
    }
 
    xml << "<network>\n"
        << "  <name>" << networkName << "</name>\n"
        << "  <forward mode='nat'/>\n"
        << "  <bridge name='" << bridgeName << "' stp='on' delay='0'/>\n"
        << "  <ip address='" << baseIP << ".1' netmask='255.255.255.0'>\n"
        << "    <dhcp>\n"
        << "      <range start='" << baseIP << ".10' end='" << baseIP << ".250'/>\n"
        << "    </dhcp>\n"
        << "  </ip>\n"
        << "</network>";
    
    return xml.str();
}

std::string NetworkManager::generateSwarmNetworkXML(const std::string& networkId,
                                                    const std::string& displayName,
                                                    const std::string& subnet) {
    std::stringstream xml;
    //    std::string networkName = "swarm_" + networkId;
    std::string networkName = displayName + networkId;
    std::regex subnetRegex(R"((\d+\.\d+\.\d+)\.\d+)");
    std::smatch match;
    std::string baseIP;
    
    if (std::regex_match(subnet, match, subnetRegex)) {
        baseIP = match[1].str();
    }
    
    std::string bridgeName = "vbrs" + networkId.substr(networkId.length() - 6);

    xml << "<network>\n"
        << "  <name>" << networkName << "</name>\n"
        << "  <forward mode='nat'/>\n"
        << "  <bridge name='" << bridgeName << "' stp='on' delay='0'/>\n"
        << "  <ip address='" << baseIP << ".1' netmask='255.255.255.0'>\n"
        << "    <dhcp>\n"
        << "      <range start='" << baseIP << ".10' end='" << baseIP << ".250'/>\n"
        << "    </dhcp>\n"
        << "  </ip>\n"
        << "</network>";
    
    return xml.str();
}

json NetworkManager::createUserNetwork(const std::string& username, const std::string& networkName) {
    json result;
    result["success"] = false;
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    // Check network limit
    int currentCount = getUserNetworkCount(username);
    if (currentCount >= MAX_USER_NETWORKS) {
        result["error"] = "Maximum network limit reached (" + std::to_string(MAX_USER_NETWORKS) + ")";
        result["currentCount"] = currentCount;
        result["maxAllowed"] = MAX_USER_NETWORKS;
        return result;
    }
    
    // Generate unique network ID
    std::string networkId = generateNetworkId();
    
    // Create display name
    std::string displayName;
    if (networkName.empty()) {
        displayName = "Network " + std::to_string(currentCount + 1);
    } else {
        displayName = sanitizeNetworkName(networkName);
    }
    
    // Generate subnet
    std::string subnet = generateSubnet();
    if (subnet.empty()) {
        result["error"] = "Failed to generate available subnet";
        return result;
    }
    
    // Generate XML
    std::string xml = generateUserNetworkXML(networkId, displayName, subnet);
    
    // Libvirt network name
    std::string libvirtNetworkName = "user_" + networkId;
    
    // Define network
    virNetworkPtr network = virNetworkDefineXML(conn, xml.c_str());
    if (!network) {
        result["error"] = "Failed to define network";
        return result;
    }
    
    // Set autostart
    virNetworkSetAutostart(network, 1);
    
    // Start network
    if (virNetworkCreate(network) < 0) {
        virNetworkUndefine(network);
        virNetworkFree(network);
        result["error"] = "Failed to start network";
        return result;
    }
    
    // Save to config
    json networkInfo = {
        {"networkId", networkId},
        {"networkName", libvirtNetworkName},
        {"displayName", displayName},
        {"subnet", subnet},
        {"owner", username},
        {"type", "user"},
        {"created", std::time(nullptr)},
        {"active", true}
    };
    
    networksConfig["userNetworks"].push_back(networkInfo);
    networksConfig["usedSubnets"].push_back(subnet);
    saveNetworksConfig();
    
    virNetworkFree(network);
    
    result["success"] = true;
    result["networkId"] = networkId;
    result["networkName"] = libvirtNetworkName;
    result["displayName"] = displayName;
    result["subnet"] = subnet;
    result["message"] = "User network created successfully";
    
    return result;
}

json NetworkManager::createSwarmNetwork(const std::string& clusterName, const std::string& owner) {
    json result;
    result["success"] = false;
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    // Check network limit
    int currentCount = getSwarmNetworkCount(owner);
    if (currentCount >= MAX_SWARM_NETWORKS) {
        result["error"] = "Maximum swarm network limit reached";
        return result;
    }
    
    // Generate unique network ID
    std::string networkId = generateNetworkId();
    
    // Sanitize cluster name for display
    std::string displayName = "Swarm: " + sanitizeNetworkName(clusterName);
    
    // Generate subnet
    std::string subnet = generateSubnet();
    if (subnet.empty()) {
        result["error"] = "Failed to generate available subnet";
        return result;
    }
    
    std::string xml = generateSwarmNetworkXML(networkId, displayName, subnet);
    std::string libvirtNetworkName = "swarm_" + networkId;
    
    virNetworkPtr network = virNetworkDefineXML(conn, xml.c_str());
    if (!network) {
        result["error"] = "Failed to define swarm network";
        return result;
    }
    
    virNetworkSetAutostart(network, 1);
    
    if (virNetworkCreate(network) < 0) {
        virNetworkUndefine(network);
        virNetworkFree(network);
        result["error"] = "Failed to start swarm network";
        return result;
    }
    
    json networkInfo = {
        {"networkId", networkId},
        {"networkName", libvirtNetworkName},
        {"displayName", displayName},
        {"clusterName", clusterName},
        {"subnet", subnet},
        {"owner", owner},
        {"type", "swarm"},
        {"created", std::time(nullptr)},
        {"active", true}
    };
    
    networksConfig["swarmNetworks"].push_back(networkInfo);
    networksConfig["usedSubnets"].push_back(subnet);
    saveNetworksConfig();
    
    virNetworkFree(network);
    
    result["success"] = true;
    result["networkId"] = networkId;
    result["networkName"] = libvirtNetworkName;
    result["displayName"] = displayName;
    result["subnet"] = subnet;
    result["message"] = "Swarm network created successfully";
    
    return result;
}

json NetworkManager::getUserNetwork(const std::string& networkId) {
    json result;
    result["success"] = false;
    
    for (const auto& network : networksConfig["userNetworks"]) {
        if (network["networkId"] == networkId) {
            result["success"] = true;
            result["network"] = network;
            return result;
        }
    }
    
    result["error"] = "Network not found";
    return result;
}

json NetworkManager::getUserNetworks(const std::string& username) {
    json result;
    result["success"] = true;
    result["networks"] = json::array();
    result["count"] = 0;
    
    for (const auto& network : networksConfig["userNetworks"]) {
        if (network["owner"] == username) {
            result["networks"].push_back(network);
        }
    }
    
    result["count"] = result["networks"].size();
    result["maxAllowed"] = MAX_USER_NETWORKS;
    
    return result;
}

json NetworkManager::getSwarmNetwork(const std::string& networkId) {
    json result;
    result["success"] = false;
    
    for (const auto& network : networksConfig["swarmNetworks"]) {
        if (network["networkId"] == networkId) {
            result["success"] = true;
            result["network"] = network;
            return result;
        }
    }
    
    result["error"] = "Swarm network not found";
    return result;
}

bool NetworkManager::deleteUserNetwork(const std::string& networkId, 
                                      const std::string& username) {
    // Find network in config
    auto& userNetworks = networksConfig["userNetworks"];
    
    int index = -1;
    virNetworkPtr network = nullptr;
    std::string networkName;
    
    for (size_t i = 0; i < userNetworks.size(); i++) {
        if (userNetworks[i]["networkId"] == networkId && 
            userNetworks[i]["owner"] == username) {
            index = i;
            networkName = userNetworks[i]["networkName"];
            break;
        }
    }
    
    if (index == -1) {
        std::cerr << "Network not found or not owned by user" << std::endl;
        return false;
    }
    
    // Get libvirt network
    network = virNetworkLookupByName(conn, networkName.c_str());
    if (network) {
        // Stop network if running
        if (virNetworkIsActive(network) == 1) {
            std::cout << "Stopping network: " << networkName << std::endl;
            if (virNetworkDestroy(network) < 0) {
                std::cerr << "Warning: Failed to destroy network" << std::endl;
            }
        }
        
        // Remove network definition
        std::cout << "Removing network definition: " << networkName << std::endl;
        if (virNetworkUndefine(network) < 0) {
            virErrorPtr err = virGetLastError();
            std::cerr << "Failed to undefine network: " 
                     << (err ? err->message : "unknown error") << std::endl;
            virNetworkFree(network);
            return false;
        }
        
        virNetworkFree(network);
        std::cout << "Network deleted from libvirt successfully" << std::endl;
    } else {
        std::cerr << "Warning: Network not found in libvirt (may be already deleted)" << std::endl;
    }

    // Remove from config
    userNetworks.erase(index);
    
    // Remove subnet from used list
    std::string subnet = userNetworks[index]["subnet"];
    auto& usedSubnets = networksConfig["usedSubnets"];
    for (size_t i = 0; i < usedSubnets.size(); i++) {
        if (usedSubnets[i] == subnet) {
            usedSubnets.erase(i);
            break;
        }
    }
    
    saveNetworksConfig();
    return true;
}

bool NetworkManager::deleteSwarmNetwork(const std::string& networkId) {
    auto& swarmNetworks = networksConfig["swarmNetworks"];
    
    int index = -1;
    virNetworkPtr network = nullptr;
    std::string networkName;
    
    for (size_t i = 0; i < swarmNetworks.size(); i++) {
        if (swarmNetworks[i]["networkId"] == networkId) {
            index = i;
            networkName = swarmNetworks[i]["networkName"];
            break;
        }
    }
    
    if (index == -1) {
        std::cerr << "Swarm network not found" << std::endl;
        return false;
    }
    
    // Get libvirt network
    network = virNetworkLookupByName(conn, networkName.c_str());
    if (network) {
        if (virNetworkIsActive(network) == 1) {
            virNetworkDestroy(network);
        }
        
        if (virNetworkUndefine(network) < 0) {
            virErrorPtr err = virGetLastError();
            std::cerr << "Failed to undefine swarm network: " 
                     << (err ? err->message : "unknown error") << std::endl;
            virNetworkFree(network);
            return false;
        }
        
        virNetworkFree(network);
    }
    
    
    // Remove from config
    std::string subnet = swarmNetworks[index]["subnet"];
    swarmNetworks.erase(index);
    
    // Remove subnet from used list
    auto& usedSubnets = networksConfig["usedSubnets"];
    for (size_t i = 0; i < usedSubnets.size(); i++) {
        if (usedSubnets[i] == subnet) {
            usedSubnets.erase(i);
            break;
        }
    }
    
    saveNetworksConfig();
    return true;
}


json NetworkManager::listAllNetworks() {
    json result;
    result["success"] = true;
    result["userNetworks"] = json::object();
    result["swarmNetworks"] = json::object();
    
    // Group user networks by owner
    for (const auto& network : networksConfig["userNetworks"]) {
        std::string owner = network["owner"];
        if (!result["userNetworks"].contains(owner)) {
            result["userNetworks"][owner] = json::array();
        }
        result["userNetworks"][owner].push_back(network);
    }
    
    // Group swarm networks by cluster
    for (const auto& network : networksConfig["swarmNetworks"]) {
        std::string clusterId = network.value("networkId", "unknown");
        result["swarmNetworks"][clusterId] = network;
    }
    
    return result;
}

json NetworkManager::listUserNetworks(const std::string& username) {
    json result;
    result["success"] = true;
    result["networks"] = json::array();
    
    // List user's own networks
    for (const auto& network : networksConfig["userNetworks"]) {
        if (network["owner"] == username) {
            result["networks"].push_back(network);
        }
    }
    
    // Also list swarm networks owned by this user
    for (const auto& network : networksConfig["swarmNetworks"]) {
        if (network["owner"] == username) {
            result["networks"].push_back(network);
        }
    }
    
    result["count"] = result["networks"].size();
    
    return result;
}

bool NetworkManager::isNetworkActive(const std::string& networkName) {
    if (!conn) return false;
    
    virNetworkPtr network = virNetworkLookupByName(conn, networkName.c_str());
    if (!network) return false;
    
    int active = virNetworkIsActive(network);
    virNetworkFree(network);
    
    return active == 1;
}

json NetworkManager::getNetworkInfo(const std::string& networkId) {
    json result;
    result["success"] = false;
    
    // Search in user networks
    for (const auto& network : networksConfig["userNetworks"]) {
        if (network["networkId"] == networkId) {
            result["success"] = true;
            result["network"] = network;
            
            if (conn) {
                std::string netName = network["networkName"];
                virNetworkPtr net = virNetworkLookupByName(conn, netName.c_str());
                if (net) {
                    result["network"]["active"] = (virNetworkIsActive(net) == 1);
                    
                    char* bridgeName = virNetworkGetBridgeName(net);
                    if (bridgeName) {
                        result["network"]["bridge"] = bridgeName;
                        free(bridgeName);
                    }
                    
                    virNetworkFree(net);
                }
            }
            
            return result;
        }
    }
    
    // Search in swarm networks
    for (const auto& network : networksConfig["swarmNetworks"]) {
        if (network["networkId"] == networkId) {
            result["success"] = true;
            result["network"] = network;
            
            if (conn) {
                std::string netName = network["networkName"];
                virNetworkPtr net = virNetworkLookupByName(conn, netName.c_str());
                if (net) {
                    result["network"]["active"] = (virNetworkIsActive(net) == 1);
                    
                    char* bridgeName = virNetworkGetBridgeName(net);
                    if (bridgeName) {
                        result["network"]["bridge"] = bridgeName;
                        free(bridgeName);
                    }
                    
                    virNetworkFree(net);
                }
            }
            
            return result;
        }
    }
    
    result["error"] = "Network not found";
    return result;
}

std::string NetworkManager::getNetworkIdByName(const std::string& networkName) {
    // Search in user networks
    for (const auto& network : networksConfig["userNetworks"]) {
        if (network["networkName"] == networkName) {
            return network["networkId"];
        }
    }
    
    // Search in swarm networks
    for (const auto& network : networksConfig["swarmNetworks"]) {
        if (network["networkName"] == networkName) {
            return network["networkId"];
        }
    }
    
    return "";
}

bool NetworkManager::updateNetwork(const std::string& networkId, const json& updates) {
    // Find and update in user networks
    for (auto& network : networksConfig["userNetworks"]) {
        if (network["networkId"] == networkId) {
            if (updates.contains("displayName")) {
                network["displayName"] = sanitizeNetworkName(updates["displayName"]);
            }
            saveNetworksConfig();
            return true;
        }
    }
    
    // Find and update in swarm networks
    for (auto& network : networksConfig["swarmNetworks"]) {
        if (network["networkId"] == networkId) {
            if (updates.contains("displayName")) {
                network["displayName"] = sanitizeNetworkName(updates["displayName"]);
            }
            saveNetworksConfig();
            return true;
        }
    }
    
    return false;
}