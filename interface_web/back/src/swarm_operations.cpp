#include "../include/swarm_operations.hpp"
#include "../include/utils.hpp"
#include <fstream>

SwarmOperations::SwarmOperations(virConnectPtr connection) 
    : conn(connection), swarmConfigFile("/var/lib/thoth-cloud/swarm.json") {}

json SwarmOperations::initSwarm(const std::string& managerVM) {
    json result;
    result["success"] = false;
    
    // Get manager IP
    std::string getIPCmd = "virsh domifaddr " + managerVM + 
                          " | grep -oP '(\\d+\\.){3}\\d+' | head -1";
    std::string managerIP = execCommand(getIPCmd);
    managerIP.erase(managerIP.find_last_not_of("\n\r") + 1);
    
    if (managerIP.empty()) {
        result["error"] = "Could not get manager IP";
        return result;
    }
    
    // Initialize swarm
    std::string initCmd = "ssh root@" + managerIP + 
                         " 'docker swarm init --advertise-addr " + managerIP + "'";
    std::string output = execCommand(initCmd);
    
    if (output.find("Swarm initialized") == std::string::npos) {
        result["error"] = "Failed to initialize swarm: " + output;
        return result;
    }
    
    // Get join token
    std::string tokenCmd = "ssh root@" + managerIP + 
                          " 'docker swarm join-token worker -q'";
    std::string token = execCommand(tokenCmd);
    token.erase(token.find_last_not_of("\n\r") + 1);
    
    result["success"] = true;
    result["managerIP"] = managerIP;
    result["workerToken"] = token;
    
    return result;
}

json SwarmOperations::joinSwarm(const std::string& workerVM, 
                               const std::string& token, 
                               const std::string& managerIP) {
    json result;
    result["success"] = false;
    
    // Get worker IP
    std::string getIPCmd = "virsh domifaddr " + workerVM + 
                          " | grep -oP '(\\d+\\.){3}\\d+' | head -1";
    std::string workerIP = execCommand(getIPCmd);
    workerIP.erase(workerIP.find_last_not_of("\n\r") + 1);
    
    if (workerIP.empty()) {
        result["error"] = "Could not get worker IP";
        return result;
    }
    
    // Join swarm
    std::string joinCmd = "ssh root@" + workerIP + 
                         " 'docker swarm join --token " + token + 
                         " " + managerIP + ":2377'";
    std::string output = execCommand(joinCmd);
    
    if (output.find("joined a swarm") == std::string::npos) {
        result["error"] = "Failed to join swarm: " + output;
        return result;
    }
    
    result["success"] = true;
    result["workerIP"] = workerIP;
    
    return result;
}

json SwarmOperations::listSwarmNodes() {
    json result;
    result["success"] = false;
    
    std::string cmd = "docker node ls --format '{{.ID}}\t{{.Hostname}}\t{{.Status}}\t{{.Availability}}\t{{.ManagerStatus}}'";
    std::string output = execCommand(cmd);
    
    result["nodes"] = json::array();
    std::istringstream stream(output);
    std::string line;
    
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        
        std::istringstream lineStream(line);
        std::string id, hostname, status, availability, managerStatus;
        
        std::getline(lineStream, id, '\t');
        std::getline(lineStream, hostname, '\t');
        std::getline(lineStream, status, '\t');
        std::getline(lineStream, availability, '\t');
        std::getline(lineStream, managerStatus, '\t');
        
        result["nodes"].push_back({
            {"id", id},
            {"hostname", hostname},
            {"status", status},
            {"availability", availability},
            {"isManager", !managerStatus.empty()}
        });
    }
    
    result["success"] = true;
    return result;
}

json SwarmOperations::deployService(const json& serviceConfig) {
    json result;
    result["success"] = false;
    
    std::string name = serviceConfig["name"];
    std::string image = serviceConfig["image"];
    int replicas = serviceConfig.value("replicas", 1);
    
    std::stringstream cmd;
    cmd << "docker service create --name " << name
        << " --replicas " << replicas
        << " " << image;
    
    if (serviceConfig.contains("ports")) {
        for (const auto& port : serviceConfig["ports"]) {
            cmd << " -p " << port.get<std::string>();
        }
    }
    
    std::string output = execCommand(cmd.str());
    
    if (output.empty() || output.find("error") != std::string::npos) {
        result["error"] = "Failed to deploy service";
        return result;
    }
    
    result["success"] = true;
    result["serviceId"] = output;
    
    return result;
}