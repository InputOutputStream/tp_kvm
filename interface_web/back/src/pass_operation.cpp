#include "../include/paas_operations.hpp"
#include "../include/utils.hpp"
#include <iostream>
#include <sstream>
#include <fstream>

PaaSOperations::PaaSOperations(virConnectPtr connection) : conn(connection) {}

bool PaaSOperations::dockerImageExists(const std::string& imageName) {
    std::string cmd = "docker images -q " + imageName + " 2>/dev/null";
    std::string result = execCommand(cmd);
    return !result.empty();
}

bool PaaSOperations::pullDockerImage(const std::string& imageName) {
    std::cout << "Pulling Docker image: " << imageName << std::endl;
    std::string cmd = "docker pull " + imageName + " 2>&1";
    std::string result = execCommand(cmd);
    
    // Check if pull was successful
    return result.find("Downloaded") != std::string::npos || 
           result.find("up to date") != std::string::npos ||
           dockerImageExists(imageName);
}

std::string PaaSOperations::generateDockerComposeFile(const json& appConfig) {
    std::stringstream compose;
    
    compose << "version: '3.8'\n\n";
    compose << "services:\n";
    compose << "  " << appConfig["id"].get<std::string>() << ":\n";
    compose << "    image: " << appConfig["dockerImage"].get<std::string>() << "\n";
    compose << "    container_name: " << appConfig["name"].get<std::string>() << "\n";
    compose << "    restart: unless-stopped\n";
    
    // Add ports
    if (appConfig.contains("ports") && appConfig["ports"].is_array()) {
        compose << "    ports:\n";
        for (const auto& port : appConfig["ports"]) {
            compose << "      - \"" << port.get<std::string>() << "\"\n";
        }
    }
    
    // Add volumes
    compose << "    volumes:\n";
    compose << "      - " << appConfig["id"].get<std::string>() << "_data:/data\n";
    
    // Add environment variables if needed
    if (appConfig.contains("environment") && appConfig["environment"].is_object()) {
        compose << "    environment:\n";
        for (auto& [key, value] : appConfig["environment"].items()) {
            compose << "      " << key << ": " << value.get<std::string>() << "\n";
        }
    }
    
    // Add networks
    compose << "    networks:\n";
    compose << "      - paas_network\n\n";
    
    // Define volumes
    compose << "volumes:\n";
    compose << "  " << appConfig["id"].get<std::string>() << "_data:\n\n";
    
    // Define networks
    compose << "networks:\n";
    compose << "  paas_network:\n";
    compose << "    driver: bridge\n";
    
    return compose.str();
}

json PaaSOperations::deployApplication(const json& appConfig) {
    json result;
    result["success"] = false;
    
    if (!appConfig.contains("id") || !appConfig.contains("dockerImage")) {
        result["error"] = "Missing required fields: id or dockerImage";
        return result;
    }
    
    std::string appId = appConfig["id"];
    std::string dockerImage = appConfig["dockerImage"];
    
    std::cout << "Deploying application: " << appId << std::endl;
    
    // Step 1: Check if Docker is available
    std::string dockerCheck = execCommand("which docker");
    if (dockerCheck.empty()) {
        result["error"] = "Docker is not installed or not in PATH";
        return result;
    }
    
    // Step 2: Check if docker-compose is available
    std::string composeCheck = execCommand("which docker-compose");
    if (composeCheck.empty()) {
        result["error"] = "docker-compose is not installed or not in PATH";
        return result;
    }
    
    // Step 3: Pull Docker image if not exists
    if (!dockerImageExists(dockerImage)) {
        std::cout << "Image not found locally, pulling..." << std::endl;
        if (!pullDockerImage(dockerImage)) {
            result["error"] = "Failed to pull Docker image: " + dockerImage;
            return result;
        }
    }
    
    // Step 4: Create compose directory
    std::string composeDir = "/var/lib/thoth-paas/" + appId;
    std::string mkdirCmd = "mkdir -p " + composeDir;
    execCommand(mkdirCmd);
    
    // Step 5: Generate docker-compose.yml
    std::string composeContent = generateDockerComposeFile(appConfig);
    std::string composeFile = composeDir + "/docker-compose.yml";
    
    std::ofstream file(composeFile);
    if (!file.is_open()) {
        result["error"] = "Failed to create docker-compose.yml file";
        return result;
    }
    file << composeContent;
    file.close();
    
    std::cout << "Created docker-compose.yml at: " << composeFile << std::endl;
    
    // Step 6: Start the application
    std::string startCmd = "cd " + composeDir + " && docker-compose up -d 2>&1";
    std::string startResult = execCommand(startCmd);
    
    if (startResult.find("error") != std::string::npos || 
        startResult.find("Error") != std::string::npos) {
        result["error"] = "Failed to start application: " + startResult;
        return result;
    }
    
    result["success"] = true;
    result["message"] = "Application deployed successfully";
    result["composeFile"] = composeFile;
    result["output"] = startResult;
    
    return result;
}

json PaaSOperations::listApplications() {
    json result;
    result["success"] = false;
    
    // Get running containers with label for PaaS apps
    std::string cmd = "docker ps --format '{{.Names}}\t{{.Status}}\t{{.Ports}}' 2>&1";
    std::string output = execCommand(cmd);
    
    if (output.find("error") != std::string::npos) {
        result["error"] = "Failed to list containers";
        return result;
    }
    
    json apps = json::array();
    std::istringstream stream(output);
    std::string line;
    
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        
        std::istringstream lineStream(line);
        std::string name, status, ports;
        
        std::getline(lineStream, name, '\t');
        std::getline(lineStream, status, '\t');
        std::getline(lineStream, ports, '\t');
        
        json app = {
            {"name", name},
            {"status", status},
            {"ports", ports},
            {"running", status.find("Up") != std::string::npos}
        };
        
        apps.push_back(app);
    }
    
    result["success"] = true;
    result["applications"] = apps;
    
    return result;
}

json PaaSOperations::getApplicationStatus(const std::string& appId) {
    json result;
    result["success"] = false;
    
    std::string cmd = "docker ps -a --filter name=" + appId + 
                     " --format '{{.Status}}' 2>&1";
    std::string status = execCommand(cmd);
    
    if (status.empty()) {
        result["error"] = "Application not found";
        return result;
    }
    
    result["success"] = true;
    result["status"] = status;
    result["running"] = status.find("Up") != std::string::npos;
    
    return result;
}

bool PaaSOperations::stopApplication(const std::string& appId) {
    std::string composeDir = "/var/lib/thoth-paas/" + appId;
    std::string cmd = "cd " + composeDir + " && docker-compose down 2>&1";
    std::string result = execCommand(cmd);
    
    return result.find("error") == std::string::npos;
}

bool PaaSOperations::startApplication(const std::string& appId) {
    std::string composeDir = "/var/lib/thoth-paas/" + appId;
    std::string cmd = "cd " + composeDir + " && docker-compose up -d 2>&1";
    std::string result = execCommand(cmd);
    
    return result.find("error") == std::string::npos;
}

bool PaaSOperations::deleteApplication(const std::string& appId) {
    // Stop and remove containers
    std::string composeDir = "/var/lib/thoth-paas/" + appId;
    std::string stopCmd = "cd " + composeDir + " && docker-compose down -v 2>&1";
    execCommand(stopCmd);
    
    // Remove compose directory
    std::string rmCmd = "rm -rf " + composeDir;
    execCommand(rmCmd);
    
    return true;
}

json PaaSOperations::getApplicationLogs(const std::string& appId, int lines) {
    json result;
    result["success"] = false;
    
    std::string cmd = "docker logs --tail " + std::to_string(lines) + " " + appId + " 2>&1";
    std::string logs = execCommand(cmd);
    
    if (logs.empty()) {
        result["error"] = "No logs available or container not found";
        return result;
    }
    
    result["success"] = true;
    result["logs"] = logs;
    
    return result;
}