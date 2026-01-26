#include "../include/network_manager.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <iostream>
#include <regex>

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
        try {
            file >> networksConfig;
        } catch (const std::exception& e) {
            std::cerr << "Error loading networks config: " << e.what() << std::endl;
            networksConfig = json::object();
            networksConfig["userNetworks"] = json::object();
            networksConfig["swarmNetworks"] = json::object();
            networksConfig["usedSubnets"] = json::array();
        }
        file.close();
    } else {
        networksConfig = json::object();
        networksConfig["userNetworks"] = json::object();
        networksConfig["swarmNetworks"] = json::object();
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

std::string NetworkManager::generateSubnet(const std::string& username) {
    // Generate subnet based on hash of username
    // Use 10.x.y.0/24 range
    std::hash<std::string> hasher;
    size_t hash = hasher(username);
    
    int x = 100 + (hash % 155);  // 10.100-255.x.0/24
    int y = (hash / 256) % 256;
    
    std::string subnet = "10." + std::to_string(x) + "." + std::to_string(y) + ".0";
    
    // Check if already used
    int attempt = 0;
    while (!isSubnetAvailable(subnet) && attempt < 100) {
        y = (y + 1) % 256;
        subnet = "10." + std::to_string(x) + "." + std::to_string(y) + ".0";
        attempt++;
    }
    
    return subnet;
}

std::string NetworkManager::generateSwarmSubnet(const std::string& clusterName) {
    // Use 172.16-31.x.0/24 for swarm networks
    std::hash<std::string> hasher;
    size_t hash = hasher(clusterName);
    
    int x = 16 + (hash % 16);  // 172.16-31.x.0/24
    int y = (hash / 256) % 256;
    
    std::string subnet = "172." + std::to_string(x) + "." + std::to_string(y) + ".0";
    
    int attempt = 0;
    while (!isSubnetAvailable(subnet) && attempt < 100) {
        y = (y + 1) % 256;
        subnet = "172." + std::to_string(x) + "." + std::to_string(y) + ".0";
        attempt++;
    }
    
    return subnet;
}

bool NetworkManager::isSubnetAvailable(const std::string& subnet) {
    for (const auto& usedSubnet : networksConfig["usedSubnets"]) {
        if (usedSubnet.get<std::string>() == subnet) {
            return false;
        }
    }
    return true;
}

std::string NetworkManager::generateUserNetworkXML(const std::string& username, const std::string& subnet) {
    std::stringstream xml;
    std::string networkName = "user_" + username;
    
    // Extract subnet parts (10.x.y.0 -> 10.x.y)
    std::regex subnetRegex(R"((\d+\.\d+\.\d+)\.\d+)");
    std::smatch match;
    std::string baseIP;
    
    if (std::regex_match(subnet, match, subnetRegex)) {
        baseIP = match[1].str();
    }
    
    xml << "<network>\n"
        << "  <name>" << networkName << "</name>\n"
        << "  <forward mode='nat'/>\n"
        << "  <bridge name='virbr_" << username << "' stp='on' delay='0'/>\n"
        << "  <ip address='" << baseIP << ".1' netmask='255.255.255.0'>\n"
        << "    <dhcp>\n"
        << "      <range start='" << baseIP << ".10' end='" << baseIP << ".250'/>\n"
        << "    </dhcp>\n"
        << "  </ip>\n"
        << "</network>";
    
    return xml.str();
}

std::string NetworkManager::generateSwarmNetworkXML(const std::string& clusterName, const std::string& subnet) {
    std::stringstream xml;
    std::string networkName = "swarm_" + clusterName;
    
    std::regex subnetRegex(R"((\d+\.\d+\.\d+)\.\d+)");
    std::smatch match;
    std::string baseIP;
    
    if (std::regex_match(subnet, match, subnetRegex)) {
        baseIP = match[1].str();
    }
    
    xml << "<network>\n"
        << "  <name>" << networkName << "</name>\n"
        << "  <forward mode='nat'/>\n"
        << "  <bridge name='virbr_swarm_" << clusterName.substr(0, 8) << "' stp='on' delay='0'/>\n"
        << "  <ip address='" << baseIP << ".1' netmask='255.255.255.0'>\n"
        << "    <dhcp>\n"
        << "      <range start='" << baseIP << ".10' end='" << baseIP << ".250'/>\n"
        << "    </dhcp>\n"
        << "  </ip>\n"
        << "</network>";
    
    return xml.str();
}

json NetworkManager::createUserNetwork(const std::string& username) {
    json result;
    result["success"] = false;
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    // Check if network already exists
    std::string networkName = "user_" + username;
    virNetworkPtr existingNet = virNetworkLookupByName(conn, networkName.c_str());
    if (existingNet) {
        virNetworkFree(existingNet);
        result["error"] = "Network already exists for user";
        result["networkName"] = networkName;
        return result;
    }
    
    // Generate subnet
    std::string subnet = generateSubnet(username);
    
    // Generate XML
    std::string xml = generateUserNetworkXML(username, subnet);
    
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
        virNetworkFree(network);
        result["error"] = "Failed to start network";
        return result;
    }
    
    // Save to config
    networksConfig["userNetworks"][username] = {
        {"networkName", networkName},
        {"subnet", subnet},
        {"created", std::time(nullptr)}
    };
    networksConfig["usedSubnets"].push_back(subnet);
    saveNetworksConfig();
    
    virNetworkFree(network);
    
    result["success"] = true;
    result["networkName"] = networkName;
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
    
    std::string networkName = "swarm_" + clusterName;
    virNetworkPtr existingNet = virNetworkLookupByName(conn, networkName.c_str());
    if (existingNet) {
        virNetworkFree(existingNet);
        result["error"] = "Swarm network already exists";
        result["networkName"] = networkName;
        return result;
    }
    
    std::string subnet = generateSwarmSubnet(clusterName);
    std::string xml = generateSwarmNetworkXML(clusterName, subnet);
    
    virNetworkPtr network = virNetworkDefineXML(conn, xml.c_str());
    if (!network) {
        result["error"] = "Failed to define swarm network";
        return result;
    }
    
    virNetworkSetAutostart(network, 1);
    
    if (virNetworkCreate(network) < 0) {
        virNetworkFree(network);
        result["error"] = "Failed to start swarm network";
        return result;
    }
    
    networksConfig["swarmNetworks"][clusterName] = {
        {"networkName", networkName},
        {"subnet", subnet},
        {"owner", owner},
        {"created", std::time(nullptr)}
    };
    networksConfig["usedSubnets"].push_back(subnet);
    saveNetworksConfig();
    
    virNetworkFree(network);
    
    result["success"] = true;
    result["networkName"] = networkName;
    result["subnet"] = subnet;
    result["message"] = "Swarm network created successfully";
    
    return result;
}

json NetworkManager::getUserNetwork(const std::string& username) {
    json result;
    result["success"] = false;
    
    if (networksConfig["userNetworks"].contains(username)) {
        result["success"] = true;
        result["network"] = networksConfig["userNetworks"][username];
    } else {
        result["error"] = "No network found for user";
    }
    
    return result;
}

json NetworkManager::getSwarmNetwork(const std::string& clusterName) {
    json result;
    result["success"] = false;
    
    if (networksConfig["swarmNetworks"].contains(clusterName)) {
        result["success"] = true;
        result["network"] = networksConfig["swarmNetworks"][clusterName];
    } else {
        result["error"] = "No network found for cluster";
    }
    
    return result;
}

bool NetworkManager::deleteUserNetwork(const std::string& username) {
    if (!conn) return false;
    
    std::string networkName = "user_" + username;
    virNetworkPtr network = virNetworkLookupByName(conn, networkName.c_str());
    
    if (!network) return false;
    
    // Stop network if active
    if (virNetworkIsActive(network)) {
        virNetworkDestroy(network);
    }
    
    // Undefine network
    virNetworkUndefine(network);
    virNetworkFree(network);
    
    // Remove from config
    if (networksConfig["userNetworks"].contains(username)) {
        std::string subnet = networksConfig["userNetworks"][username]["subnet"];
        
        // Remove subnet from used list
        auto& usedSubnets = networksConfig["usedSubnets"];
        for (size_t i = 0; i < usedSubnets.size(); i++) {
            if (usedSubnets[i] == subnet) {
                usedSubnets.erase(i);
                break;
            }
        }
        
        networksConfig["userNetworks"].erase(username);
        saveNetworksConfig();
    }
    
    return true;
}

bool NetworkManager::deleteSwarmNetwork(const std::string& clusterName) {
    if (!conn) return false;
    
    std::string networkName = "swarm_" + clusterName;
    virNetworkPtr network = virNetworkLookupByName(conn, networkName.c_str());
    
    if (!network) return false;
    
    if (virNetworkIsActive(network)) {
        virNetworkDestroy(network);
    }
    
    virNetworkUndefine(network);
    virNetworkFree(network);
    
    if (networksConfig["swarmNetworks"].contains(clusterName)) {
        std::string subnet = networksConfig["swarmNetworks"][clusterName]["subnet"];
        
        auto& usedSubnets = networksConfig["usedSubnets"];
        for (size_t i = 0; i < usedSubnets.size(); i++) {
            if (usedSubnets[i] == subnet) {
                usedSubnets.erase(i);
                break;
            }
        }
        
        networksConfig["swarmNetworks"].erase(clusterName);
        saveNetworksConfig();
    }
    
    return true;
}

json NetworkManager::listAllNetworks() {
    json result;
    result["success"] = true;
    result["userNetworks"] = networksConfig["userNetworks"];
    result["swarmNetworks"] = networksConfig["swarmNetworks"];
    
    return result;
}

json NetworkManager::listUserNetworks(const std::string& username) {
    json result;
    result["success"] = true;
    result["networks"] = json::array();
    
    if (networksConfig["userNetworks"].contains(username)) {
        result["networks"].push_back(networksConfig["userNetworks"][username]);
    }
    
    // Also list swarm networks owned by this user
    for (auto& [clusterName, networkInfo] : networksConfig["swarmNetworks"].items()) {
        if (networkInfo["owner"] == username) {
            result["networks"].push_back(networkInfo);
        }
    }
    
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

json NetworkManager::getNetworkInfo(const std::string& networkName) {
    json result;
    result["success"] = false;
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    virNetworkPtr network = virNetworkLookupByName(conn, networkName.c_str());
    if (!network) {
        result["error"] = "Network not found";
        return result;
    }
    
    char* bridgeName = virNetworkGetBridgeName(network);
    int active = virNetworkIsActive(network);
    int autostart = 0;
    virNetworkGetAutostart(network, &autostart);
    
    result["success"] = true;
    result["name"] = networkName;
    result["bridge"] = bridgeName ? bridgeName : "";
    result["active"] = (active == 1);
    result["autostart"] = (autostart == 1);
    
    if (bridgeName) free(bridgeName);
    virNetworkFree(network);
    
    return result;
}