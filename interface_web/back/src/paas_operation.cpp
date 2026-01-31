#include "../include/paas_operations.hpp"
#include "../include/utils.hpp"
#include "../include/remote_executor.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <fstream>

PaaSOperations::PaaSOperations(virConnectPtr connection, RemoteExec::RemoteExecutor *remoteExec, HostManager *hostMgr)
 : conn(connection), remoteExecutor(remoteExec), hostManager(hostMgr) {}

bool PaaSOperations::dockerImageExists(const std::string& imageName) {
    std::string cmd = "sudo docker images -q " + imageName + " 2>/dev/null";
    auto result = remoteExecutor->execute(cmd);
    // Fixed: Image exists if command succeeded AND output is not empty
    return result.success() && !result.output.empty();
}

bool PaaSOperations::pullDockerImage(const std::string& imageName) {
    std::cout << "Pulling Docker image: " << imageName << std::endl;
    std::string cmd = "sudo docker pull " + imageName + " 2>&1";
    auto result = remoteExecutor->execute(cmd);
    
    // Check if pull was successful
    return result.output.find("Downloaded") != std::string::npos || 
           result.output.find("up to date") != std::string::npos ||
           dockerImageExists(imageName);
}

json PaaSOperations::selectPaaSHost(const json& appConfig) {
    json result;
    result["success"] = false;
    
    if (!hostManager) {
        result["error"] = "Host manager not available";
        return result;
    }
    
    // Default resource requirements for PaaS applications
    int memory = appConfig.value("memory", 1024); // 1GB default
    int cpu = appConfig.value("cpu", 1);         // 1 vCPU default
    long long disk = appConfig.value("disk", 1024 * 1024 * 1024); // 1GB default
    
    // Check resource availability
    json availability = hostManager->checkResourceAvailability(memory, cpu, disk);
    
    if (!availability["available"].get<bool>()) {
        result["error"] = "No suitable host found for PaaS deployment";
        result["details"] = availability;
        return result;
    }
    
    // Use host selection strategy
    std::string selectedHost = hostManager->findBestHost(memory, cpu, disk);
    
    if (selectedHost.empty()) {
        result["error"] = "Could not select optimal host";
        return result;
    }
    
    result["success"] = true;
    result["host"] = selectedHost;
    result["resources"] = {
        {"memory", memory},
        {"cpu", cpu},
        {"disk", disk}
    };
    
    return result;
}

std::string PaaSOperations::generateDockerComposeFile(const json& appConfig) {
    std::stringstream compose;
    
    compose << "version: \"3.8\"\n\n";
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

    // Select host for deployment
    if (hostManager) {
        auto hostSelection = selectPaaSHost(appConfig);
        if (!hostSelection["success"].get<bool>()) {
            result["error"] = "Host selection failed: " + hostSelection["error"].get<std::string>();
            return result;
        }
        
        std::string selectedHost = hostSelection["host"];
        result["selectedHost"] = selectedHost;
        
        // Update remote executor to use selected host
        virConnectPtr hostConn = hostManager->getConnection(selectedHost);
        if (hostConn) {
            RemoteExec::RemoteExecutor* hostExecutor = new RemoteExec::RemoteExecutor(hostConn);
            // Use host-specific executor for deployment
        }
    }

    try{ 
        // Step 1: Check if Docker is available
        std::string dockerCheck = remoteExecutor->execute("which docker").output;
        if (dockerCheck.empty()) {
            result["error"] = "Docker is not installed or not in PATH";
            return result;
        }
   

        // Step 2: Check if docker-compose is available
        auto res = remoteExecutor->execute("which docker-compose");
        std::string composeCheck = res.output;

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
        auto mkdirResult = remoteExecutor->execute(mkdirCmd);
        if (!mkdirResult.success()) {
            result["error"] = "Failed to create directory: " + composeDir;
            return result;
       }
   
        // Step 5: Generate docker-compose.yml and write to remote host
        std::string composeContent = generateDockerComposeFile(appConfig);
        std::string composeFile = composeDir + "/docker-compose.yml";

        // Use heredoc for safe multi-line content writing
        std::string writeCmd = "cat > " + composeFile + " << 'EOFCOMPOSE'\n" + 
                            composeContent + "\nEOFCOMPOSE";

        // Write file using echo on remote host
        auto writeResult = remoteExecutor->execute(writeCmd);
        if (!writeResult.success()) {
            result["error"] = "Failed to create docker-compose.yml file";
            return result;
        }
        
        std::cout << "Created docker-compose.yml at: " << composeFile << std::endl;
        
        // Step 6: Start the application
        std::string startCmd = "cd " + composeDir + " && sudo docker-compose up -d 2>&1";
        auto startResult = remoteExecutor->execute(startCmd);
        
        // More specific error detection
        if (!startResult.success() || 
            startResult.output.find("ERROR") != std::string::npos || 
            startResult.output.find("failed") != std::string::npos) {
            result["error"] = "Failed to start application: " + startResult.output;
            return result;
        }
        
        result["success"] = true;
        result["message"] = "Application deployed successfully";
        result["composeFile"] = composeFile;
        result["output"] = startResult.output;
        
        return result;
     }catch (const std::exception& e){
        fprintf(stderr, "%s\n",e.what());
        result["error"] = e.what();
        return result;
    }
}

// ==========================================
// PAAS DEPLOYMENT
// ==========================================

json PaaSOperations::deployApplicationEnhanced(const json& appConfig) {
    json result;
    result["success"] = false;
    
    if (!appConfig.contains("id")) {
        result["error"] = "Missing required field: id";
        return result;
    }
    
    std::string appId = appConfig["id"];
    
    std::cout << "Deploying application: " << appId << std::endl;
    
    // Check if Docker is available
    std::string dockerCheck = remoteExecutor->execute("which docker").output;
    if (dockerCheck.empty()) {
        result["error"] = "Docker is not installed or not in PATH";
        return result;
    }
    
    // Create compose directory
    std::string composeDir = "/var/lib/thoth-paas/" + appId;
    std::string mkdirCmd = "mkdir -p " + composeDir;
    auto mkdirResult = remoteExecutor->execute(mkdirCmd);
    if (!mkdirResult.success()) {
        result["error"] = "Failed to create directory: " + composeDir;
        return result;
    }
    
    // Get compose content
    std::string composeContent;
    if (appConfig.contains("composeContent")) {
        // Use provided compose content
        composeContent = appConfig["composeContent"];
    } else if (appConfig.contains("dockerImage")) {
        // Generate compose from config
        composeContent = generateDockerComposeFile(appConfig);
    } else {
        result["error"] = "No docker image or compose content provided";
        return result;
    }
    
    // Write docker-compose.yml to remote host
    std::string composeFile = composeDir + "/docker-compose.yml";

    // Use heredoc for safe writing
    std::string writeCmd = "cat > " + composeFile + " << 'EOFCOMPOSE'\n" + 
                        composeContent + "\nEOFCOMPOSE";
    auto writeResult = remoteExecutor->execute(writeCmd);
    if (!writeResult.success()) {
        result["error"] = "Failed to create docker-compose.yml file";
        return result;
    }

    std::cout << "Created docker-compose.yml at: " << composeFile << std::endl;

    // Pull images first
    std::string pullCmd = "cd " + composeDir + " && sudo docker-compose pull 2>&1";
    remoteExecutor->execute(pullCmd);
    
    // Start the application
    std::string startCmd = "cd " + composeDir + " && sudo docker-compose up -d 2>&1";
    auto startResult = remoteExecutor->execute(startCmd);
    
    // More specific error detection
    if (!startResult.success() || 
        startResult.output.find("ERROR") != std::string::npos || 
        startResult.output.find("failed") != std::string::npos) {
        result["error"] = "Failed to start application: " + startResult.output;
        return result;
    }
    
    result["success"] = true;
    result["message"] = "Application deployed successfully";
    result["composeFile"] = composeFile;
    result["output"] = startResult.output;
    
    return result;
}




json PaaSOperations::listApplications() {
    json result;
    result["success"] = false;
    
    // Get running containers with label for PaaS apps
    std::string cmd = "sudo docker ps --format '{{.Names}}\t{{.Status}}\t{{.Ports}}' 2>&1";
    auto execResult = remoteExecutor->execute(cmd);
    
    // Check if command execution failed
    if (!execResult.success()) {
        result["error"] = "Failed to execute docker ps command";
        return result;
    }
    
    std::string output = execResult.output;
    
    // Check for actual Docker errors (not just the word "error" in output)
    if (output.find("Cannot connect to the Docker daemon") != std::string::npos ||
        output.find("permission denied") != std::string::npos) {
        result["error"] = "Docker daemon not accessible: " + output;
        return result;
    }
    
    json apps = json::array();
    std::istringstream stream(output);
    std::string line;
    
    while (std::getline(stream, line)) {
        // Skip empty lines and warning messages
        if (line.empty() || 
            line.find("Warning:") != std::string::npos ||
            line.find("sudo:") != std::string::npos) {
            continue;
        }
        
        // Parse tab-delimited output
        std::istringstream lineStream(line);
        std::string name, status, ports;
        
        if (!std::getline(lineStream, name, '\t')) continue;
        std::getline(lineStream, status, '\t');  // May be empty
        std::getline(lineStream, ports, '\t');   // May be empty
        
        // Trim whitespace
        name.erase(0, name.find_first_not_of(" \t\r\n"));
        name.erase(name.find_last_not_of(" \t\r\n") + 1);
        
        if (!name.empty()) {
            json app = {
                {"name", name},
                {"status", status},
                {"ports", ports},
                {"running", status.find("Up") != std::string::npos}
            };
            
            apps.push_back(app);
        }
    }
    
    result["success"] = true;
    result["applications"] = apps;
    
    return result;
}

json PaaSOperations::getApplicationStatus(const std::string& appId) {
    json result;
    result["success"] = false;
    
    std::string cmd = "sudo docker ps -a --filter name=" + appId + 
                     " --format '{{.Status}}' 2>&1";
    auto execResult = remoteExecutor->execute(cmd);
    
    if (!execResult.success()) {
        result["error"] = "Failed to query application status";
        return result;
    }
    
    std::string status = execResult.output;
    
    // Trim whitespace
    status.erase(0, status.find_first_not_of(" \t\r\n"));
    status.erase(status.find_last_not_of(" \t\r\n") + 1);
    
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
    std::string cmd = "cd " + composeDir + " && sudo docker-compose down 2>&1";
    auto result = remoteExecutor->execute(cmd);
    
    return result.success() && 
           result.output.find("ERROR") == std::string::npos;
}

bool PaaSOperations::startApplication(const std::string& appId) {
    std::string composeDir = "/var/lib/thoth-paas/" + appId;
    std::string cmd = "cd " + composeDir + " && sudo docker-compose up -d 2>&1";
    auto result = remoteExecutor->execute(cmd);
    
    return result.success() && 
           result.output.find("ERROR") == std::string::npos;
}

bool PaaSOperations::deleteApplication(const std::string& appId) {
    // Stop and remove containers
    std::string composeDir = "/var/lib/thoth-paas/" + appId;
    std::string stopCmd = "cd " + composeDir + " && sudo docker-compose down -v 2>&1";
    remoteExecutor->execute(stopCmd);
    
    // Remove compose directory
    std::string rmCmd = "rm -rf " + composeDir;
    auto result = remoteExecutor->execute(rmCmd);
    
    return result.success();
}

json PaaSOperations::getApplicationLogs(const std::string& appId, int lines) {
    json result;
    result["success"] = false;
    
    std::string cmd = "sudo docker logs --tail " + std::to_string(lines) + " " + appId + " 2>&1";
    auto execResult = remoteExecutor->execute(cmd);
    
    if (!execResult.success()) {
        result["error"] = "Failed to retrieve logs";
        return result;
    }
    
    std::string logs = execResult.output;
    
    if (logs.find("No such container") != std::string::npos) {
        result["error"] = "Container not found";
        return result;
    }
    
    result["success"] = true;
    result["logs"] = logs;
    
    return result;
}


RemoteExec::RemoteExecutor::ExecResult  PaaSOperations::getAppStats(std::string appID)
{
     // Get stats using docker stats --no-stream
    std::string cmd = "sudo docker stats --no-stream --format "
                     "\"{{.CPUPerc}}\t{{.MemUsage}}\t{{.NetIO}}\t{{.BlockIO}}\" " 
                     + appID;
    auto result = remoteExecutor->execute(cmd);
   
    return result;
}


//===========================
//     Office Cloud         =
//===========================

json PaaSOperations::deployOnlyOffice(const std::string& username) {
    json result;
    result["success"] = false;
    
    std::string containerName = "onlyoffice-" + username;
    std::string dataVolume = "onlyoffice-data-" + username;
    
    // Check if already exists
    std::string checkCmd = "docker ps -a --filter name=" + containerName + 
                          " --format '{{.Names}}'";
    auto checkResult = remoteExecutor->execute(checkCmd);
    
    if (!checkResult.output.empty()) {
        // Container exists, just start it
        std::string startCmd = "docker start " + containerName;
        remoteExecutor->execute(startCmd);
        
        result["success"] = true;
        result["message"] = "OnlyOffice instance started";
        result["url"] = "http://YOUR_HOST:8080"; // Configure based on your setup
        return result;
    }
    
    // Create volume for persistence
    std::string createVolumeCmd = "docker volume create " + dataVolume;
    remoteExecutor->execute(createVolumeCmd);
    
    // Deploy OnlyOffice container
    std::stringstream dockerCmd;
    dockerCmd << "docker run -d "
              << "--name " << containerName << " "
              << "-p 8080:80 "  // Bind to unique port per user
              << "-v " << dataVolume << ":/var/www/onlyoffice/Data "
              << "-e JWT_ENABLED=false "  // For simplicity; enable in production
              << "--restart unless-stopped "
              << "onlyoffice/documentserver:latest";
    
    auto deployResult = remoteExecutor->execute(dockerCmd.str());
    
    if (!deployResult.success()) {
        result["error"] = "Failed to deploy OnlyOffice: " + deployResult.output;
        return result;
    }
    
    // Wait for container to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    result["success"] = true;
    result["message"] = "OnlyOffice deployed successfully";
    result["url"] = "http://YOUR_HOST:8080";
    result["containerName"] = containerName;
    result["dataVolume"] = dataVolume;
    
    return result;
}

json PaaSOperations::getOnlyOfficeURL(const std::string& username) {
    json result;
    result["success"] = false;
    
    std::string containerName = "onlyoffice-" + username;
    
    // Check if running
    std::string checkCmd = "docker ps --filter name=" + containerName + 
                          " --format '{{.Status}}'";
    auto checkResult = remoteExecutor->execute(checkCmd);
    
    if (checkResult.output.empty()) {
        result["error"] = "OnlyOffice instance not running";
        return result;
    }
    
    result["success"] = true;
    result["url"] = "http://YOUR_HOST:8080";
    result["running"] = checkResult.output.find("Up") != std::string::npos;
    
    return result;
}

int PaaSOperations::getNextAvailablePort() {
    // Start from 8080
    for (int port = 8080; port < 9000; port++) {
        std::string checkCmd = "docker ps --format '{{.Ports}}' | grep -q ':" + 
                              std::to_string(port) + "->'";
        auto result = remoteExecutor->execute(checkCmd);
        if (result.exitCode != 0) {  // Port not in use
            return port;
        }
    }
    return -1;
}

bool PaaSOperations::deleteOnlyOffice(const std::string& username) {
    std::string containerName = "onlyoffice-" + username;
    std::string dataVolume = "onlyoffice-data-" + username;
    
    // Stop and remove container
    std::string stopCmd = "docker stop " + containerName + " && docker rm " + containerName;
    remoteExecutor->execute(stopCmd);
    
    std::string rmVolumeCmd = "docker volume rm " + dataVolume;
    remoteExecutor->execute(rmVolumeCmd);
    
    return true;
}