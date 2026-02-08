#include "../include/swarm_operations.hpp"
#include "../include/network_manager.hpp"
#include "../include/utils.hpp"
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <sys/stat.h>

SwarmOperations::SwarmOperations(virConnectPtr connection, VMOperations* vmOperations,
                                NetworkManager* networkManager, RemoteExec::RemoteExecutor *remoteE, HostManager *hostMgr)
    : conn(connection), vmOps(vmOperations), networkMgr(networkManager), remoteExec(remoteE), hostManager(hostMgr),
      clustersConfigFile("/var/lib/thoth-cloud/swarm_clusters.json") {
    loadClusters();
}

SwarmOperations::~SwarmOperations() {
    saveClusters();
}

// ============================================================================
// PERSISTENCE LAYER - Cluster State Management
// ============================================================================

void SwarmOperations::loadClusters() {
    std::ifstream file(clustersConfigFile);
    if (file.is_open()) {
        try {
            json data;
            file >> data;
            
            for (const auto& item : data["clusters"]) {
                SwarmCluster cluster;
                cluster.clusterId = item["clusterId"];
                cluster.clusterName = item["clusterName"];
                cluster.owner = item["owner"];
                cluster.networkName = item["networkName"];
                cluster.subnet = item["subnet"];
                cluster.managerToken = item.value("managerToken", "");
                cluster.workerToken = item.value("workerToken", "");
                cluster.managerIP = item.value("managerIP", "");
                cluster.created = item["created"];
                cluster.status = item.value("status", "unknown");
                
                for (const auto& nodeData : item["nodes"]) {
                    SwarmNode node;
                    node.vmName = nodeData["vmName"];
                    node.role = nodeData["role"];
                    node.ip = nodeData.value("ip", "");
                    node.status = nodeData.value("status", "unknown");
                    cluster.nodes.push_back(node);
                }
                
                clusters[cluster.clusterId] = cluster;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error loading clusters: " << e.what() << std::endl;
        }
        file.close();
    }
}

bool SwarmOperations::saveClusters() {
    mkdir("/var/lib/thoth-cloud", 0755);
    
    json data;
    data["clusters"] = json::array();
    
    for (const auto& [id, cluster] : clusters) {
        json clusterData = {
            {"clusterId", cluster.clusterId},
            {"clusterName", cluster.clusterName},
            {"owner", cluster.owner},
            {"networkName", cluster.networkName},
            {"subnet", cluster.subnet},
            {"managerToken", cluster.managerToken},
            {"workerToken", cluster.workerToken},
            {"managerIP", cluster.managerIP},
            {"created", cluster.created},
            {"status", cluster.status},
            {"nodes", json::array()}
        };
        
        for (const auto& node : cluster.nodes) {
            clusterData["nodes"].push_back({
                {"vmName", node.vmName},
                {"role", node.role},
                {"ip", node.ip},
                {"status", node.status}
            });
        }
        
        data["clusters"].push_back(clusterData);
    }
    
    std::ofstream file(clustersConfigFile);
    if (!file.is_open()) return false;
    
    file << data.dump(2);
    file.close();
    return true;
}

// ============================================================================
// DOCKER SWARM ORCHESTRATION - Core Swarm Operations
// ============================================================================

json SwarmOperations::initializeSwarmOnManager(const std::string& vmName, const std::string& managerIP) {
    json result;
    result["success"] = false;
    
    std::cout << "Initializing swarm on manager: " << vmName << " (" << managerIP << ")" << std::endl;
    
    // Initialize swarm
    std::string initCmd = "ssh centos@" + managerIP + 
                         " 'docker swarm init --advertise-addr " + managerIP + "'";
    auto initResult = remoteExec->execute(initCmd);
    
    if (!initResult.success()) {
        result["error"] = "Failed to initialize swarm: " + initResult.output;
        return result;
    }
    
    // Get manager join token
    std::string managerTokenCmd = "ssh centos@" + managerIP + 
                                 " 'docker swarm join-token manager -q'";
    auto managerTokenResult = remoteExec->execute(managerTokenCmd);
    
    if (!managerTokenResult.success()) {
        result["error"] = "Failed to get manager token";
        return result;
    }
    
    // Get worker join token
    std::string workerTokenCmd = "ssh centos@" + managerIP + 
                                " 'docker swarm join-token worker -q'";
    auto workerTokenResult = remoteExec->execute(workerTokenCmd);
    
    if (!workerTokenResult.success()) {
        result["error"] = "Failed to get worker token";
        return result;
    }
    
    // Trim whitespace from tokens
    std::string managerToken = managerTokenResult.output;
    std::string workerToken = workerTokenResult.output;
    managerToken.erase(managerToken.find_last_not_of(" \n\r\t") + 1);
    workerToken.erase(workerToken.find_last_not_of(" \n\r\t") + 1);
    
    result["success"] = true;
    result["managerToken"] = managerToken;
    result["workerToken"] = workerToken;
    
    return result;
}

json SwarmOperations::joinWorkerToSwarm(const std::string& vmName, const std::string& workerIP,
                                       const std::string& managerIP, const std::string& token) {
    json result;
    result["success"] = false;
    
    std::cout << "Joining worker to swarm: " << vmName << " (" << workerIP << ")" << std::endl;
    
    std::string joinCmd = "ssh centos@" + workerIP + 
                         " 'docker swarm join --token " + token + 
                         " " + managerIP + ":2377'";
    
    auto joinResult = remoteExec->execute(joinCmd);
    
    if (!joinResult.success()) {
        result["error"] = "Failed to join worker: " + joinResult.output;
        return result;
    }
    
    result["success"] = true;
    return result;
}

// ============================================================================
// DOCKER SWARM SERVICE MANAGEMENT
// ============================================================================

json SwarmOperations::deployService(const std::string& clusterId, const json& serviceConfig) {
    json result;
    result["success"] = false;
    
    auto it = clusters.find(clusterId);
    if (it == clusters.end()) {
        result["error"] = "Cluster not found";
        return result;
    }
    
    const SwarmCluster& cluster = it->second;
    
    if (cluster.managerIP.empty()) {
        result["error"] = "Cluster has no manager IP";
        return result;
    }
    
    // Build docker service create command
    std::string serviceName = serviceConfig["name"];
    std::string image = serviceConfig["image"];
    int replicas = serviceConfig.value("replicas", 1);
    
    std::stringstream serviceCmd;
    serviceCmd << "ssh centos@" << cluster.managerIP << " 'docker service create";
    serviceCmd << " --name " << serviceName;
    serviceCmd << " --replicas " << replicas;
    
    // Add port mappings if specified
    if (serviceConfig.contains("ports")) {
        for (const auto& port : serviceConfig["ports"]) {
            serviceCmd << " -p " << port.get<std::string>();
        }
    }
    
    // Add environment variables if specified
    if (serviceConfig.contains("env")) {
        for (const auto& [key, value] : serviceConfig["env"].items()) {
            serviceCmd << " -e " << key << "=" << value.get<std::string>();
        }
    }
    
    // Add volumes if specified
    if (serviceConfig.contains("volumes")) {
        for (const auto& volume : serviceConfig["volumes"]) {
            serviceCmd << " --mount " << volume.get<std::string>();
        }
    }
    
    serviceCmd << " " << image << "'";
    
    auto deployResult = remoteExec->execute(serviceCmd.str());
    
    if (!deployResult.success()) {
        result["error"] = "Failed to deploy service: " + deployResult.output;
        return result;
    }
    
    result["success"] = true;
    result["service"] = serviceName;
    result["message"] = "Service deployed successfully";
    
    return result;
}

json SwarmOperations::listServices(const std::string& clusterId) {
    json result;
    result["success"] = false;
    
    auto it = clusters.find(clusterId);
    if (it == clusters.end()) {
        result["error"] = "Cluster not found";
        return result;
    }
    
    const SwarmCluster& cluster = it->second;
    
    std::string listCmd = "ssh centos@" + cluster.managerIP + 
                         " 'docker service ls --format \"{{json .}}\"'";
    
    auto listResult = remoteExec->execute(listCmd);
    
    if (!listResult.success()) {
        result["error"] = "Failed to list services: " + listResult.output;
        return result;
    }
    
    result["success"] = true;
    result["services"] = json::array();
    
    // Parse JSON output line by line
    std::istringstream stream(listResult.output);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            try {
                result["services"].push_back(json::parse(line));
            } catch (...) {
                // Skip invalid JSON lines
            }
        }
    }
    
    return result;
}

json SwarmOperations::selectSwarmHost(const std::string& role, const json& nodeConfig) {
    json result;
    result["success"] = false;
    
    if (!hostManager) {
        result["error"] = "Host manager not available";
        return result;
    }
    
    // Swarm node resource requirements
    int memory = 1024;  // 1GB for Swarm nodes 
    int cpu = 1;        // 1 vCPUs
    long long disk = 5LL * 1024 * 1024 * 1024;  // 5GB
    
    // Managers need slightly more resources
    if (role == "manager") {
        memory = 4096;  // 4GB for managers
        cpu = 2;
    }
    
    std::cout << "\n=== Selecting Host for Swarm " << role << " ===" << std::endl;
    std::cout << "Required resources: " << memory << "MB RAM, " << cpu << " vCPUs, " 
              << (disk / (1024*1024*1024)) << "GB disk" << std::endl;
    
    // Check resource availability first
    json availability = hostManager->checkResourceAvailability(memory, cpu, disk);
    
    if (!availability["available"].get<bool>()) {
        result["error"] = "No suitable host found for Swarm " + role;
        result["details"] = availability;
        
        std::cout << "❌ No hosts with sufficient resources available!" << std::endl;
        std::cout << "Available hosts status:" << std::endl;
        for (const auto& host : availability["hosts"]) {
            std::cout << "  - " << host["hostname"].get<std::string>() 
                      << ": canAccommodate=" << host["canAccommodate"].get<bool>()
                      << ", availableMemory=" << host["availableMemory"].get<int>() << "MB"
                      << ", availableCPUs=" << host["availableCPUs"].get<int>()
                      << ", availableDisk=" << host["availableDisk"].get<long long>() << "GB"
                      << std::endl;
        }
        
        return result;
    }
    
    // Use BEST_FIT strategy for Swarm nodes (to minimize waste)
    std::string selectedHost = hostManager->findBestHost(
        memory, 
        cpu, 
        disk, 
        HostSelectionStrategy::LEAST_USED
    );
    
    if (selectedHost.empty()) {
        result["error"] = "Could not select optimal host for " + role;
        std::cout << "❌ findBestHost returned empty string!" << std::endl;
        return result;
    }
    
    std::cout << "✅ Selected host: " << selectedHost << std::endl;
    
    result["success"] = true;
    result["host"] = selectedHost;
    result["resources"] = {
        {"memory", memory},
        {"cpu", cpu},
        {"disk", disk}
    };
    
    return result;
}

json SwarmOperations::createSwarmClusterWithHosts(const std::string& clusterName, 
                                                 const std::string& owner,
                                                 int numManagers, int numWorkers) {
    json result;
    result["success"] = false;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Creating Docker Swarm Cluster with Host Selection" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Step 1: Select hosts for managers
    std::vector<std::string> managerHosts;
    std::cout << "Step 1: Selecting hosts for manager nodes..." << std::endl;
    
    for (int i = 0; i < numManagers; i++) {
        json hostSelection = selectSwarmHost("manager", {});
        if (!hostSelection["success"].get<bool>()) {
            result["error"] = "Failed to select host for manager " + std::to_string(i+1) + 
                            ": " + hostSelection["error"].get<std::string>();
            return result;
        }
        
        std::string host = hostSelection["host"];
        managerHosts.push_back(host);
        std::cout << "  ✅ Manager " << (i+1) << " assigned to: " << host << std::endl;
    }
    
    // Step 2: Select hosts for workers
    std::vector<std::string> workerHosts;
    std::cout << "\nStep 2: Selecting hosts for worker nodes..." << std::endl;
    
    for (int i = 0; i < numWorkers; i++) {
        json hostSelection = selectSwarmHost("worker", {});
        if (!hostSelection["success"].get<bool>()) {
            result["error"] = "Failed to select host for worker " + std::to_string(i+1) + 
                            ": " + hostSelection["error"].get<std::string>();
            return result;
        }
        
        std::string host = hostSelection["host"];
        workerHosts.push_back(host);
        std::cout << "  ✅ Worker " << (i+1) << " assigned to: " << host << std::endl;
    }
    
    // Step 3: Create cluster structure
    SwarmCluster cluster;
    cluster.clusterId = generateClusterId();
    cluster.clusterName = clusterName;
    cluster.owner = owner;
    cluster.created = std::time(nullptr);
    cluster.status = "creating";
    
    // Create network on primary manager's host
    std::string primaryHost = managerHosts[0];
    virConnectPtr primaryConn = hostManager->getConnection(primaryHost);
    
    if (!primaryConn) {
        result["error"] = "Failed to get connection to primary host: " + primaryHost;
        return result;
    }
    
    // Create NetworkManager for the primary host
    NetworkManager primaryNetworkMgr(primaryConn);
    auto networkResult = primaryNetworkMgr.createSwarmNetwork(clusterName, owner);
    
    if (!networkResult["success"].get<bool>()) {
        result["error"] = "Failed to create network: " + networkResult["error"].get<std::string>();
        return result;
    }
    
    std::string networkName = networkResult["networkName"];
    std::string subnet = networkResult["subnet"];
    cluster.networkName = networkName;
    cluster.subnet = subnet;
    
    std::cout << "  ✅ Network created: " << networkName << " (" << subnet << ")" << std::endl;
    
    // Step 4: Deploy manager nodes on selected hosts
    std::cout << "\nStep 3: Deploying manager nodes..." << std::endl;
    for (int i = 0; i < numManagers; i++) {
        std::string nodeName = clusterName + "-manager-" + std::to_string(i + 1);
        std::cout << "  Deploying " << nodeName << " on " << managerHosts[i] << "..." << std::endl;
        
        // Get connection for this host
        std::cout << "----------------------------------" << std::endl;
        std::cout << "Manager Hosts: " << managerHosts[0] << std::endl;

        virConnectPtr hostConn = hostManager->getConnection(managerHosts[i]);
        if (!hostConn) {
            result["error"] = "Failed to connect to host: " + managerHosts[i];
            return result;
        }
        
        // Deploy VM on specific host
        json vmConfig = {
            {"hostname", nodeName},
            {"vcpus", 2},
            {"memory", 2048},
            {"disk", 10},
            {"username", "debian12"},
            {"authMethod", "password"},
            {"password", "swarm123"},
            {"owner", owner},
            {"baseImage", "debian-12-genericcloud-amd64"},
            {"network", networkName},
            {"targetHost", managerHosts[i]}  // Specify target host
        };
        
        // Need to update VMOperations to support deployment to specific hosts
        bool deployed = vmOps->deployVM(vmConfig);
        if (!deployed) {
            result["error"] = "Failed to deploy manager node: " + nodeName;
            return result;
        }
        
        SwarmNode node;
        node.vmName = nodeName;
        node.role = "manager";
        node.host = managerHosts[i];  // Store which host the node is on
        node.status = "provisioning";
        cluster.nodes.push_back(node);
    }
    
    for (int i = 0; i < numWorkers; i++) {
        std::string nodeName = clusterName + "-worker-" + std::to_string(i + 1);
        std::cout << "  Deploying " << nodeName << " on " << managerHosts[i] << "..." << std::endl;
        
        // Get connection for this host
        virConnectPtr hostConn = hostManager->getConnection(managerHosts[i]);
        if (!hostConn) {
            result["error"] = "Failed to connect to host: " + managerHosts[i];
            return result;
        }
        
        // Deploy VM on specific host
        json vmConfig = {
            {"hostname", nodeName},
            {"vcpus", 1},
            {"memory", 2096},
            {"disk", 10},
            {"username", "debian12"},
            {"authMethod", "password"},
            {"password", "swarm123"},
            {"owner", owner},
            {"baseImage", "debian-12-genericcloud-amd64"},
            {"network", networkName},
            {"targetHost", managerHosts[i]}  // Specify target host
        };
        
        bool deployed = vmOps->deployVM(vmConfig);
        if (!deployed) {
            result["error"] = "Failed to deploy manager node: " + nodeName;
            return result;
        }
        
        SwarmNode node;
        node.vmName = nodeName;
        node.role = "worker";
        node.host = managerHosts[i];  // Store which host the node is on
        node.status = "provisioning";
        cluster.nodes.push_back(node);
    }

    // Save cluster info
    cluster.status = "ready";
    clusters[cluster.clusterId] = cluster;
    saveClusters();
    
    result["success"] = true;
    result["clusterId"] = cluster.clusterId;
    result["networkName"] = networkName;
    result["subnet"] = subnet;
    result["nodes"] = json::array();
    
    for (const auto& node : cluster.nodes) {
        result["nodes"].push_back({
            {"name", node.vmName},
            {"role", node.role},
            {"host", node.host},
            {"status", node.status}
        });
    }
    
    return result;
}

json SwarmOperations::getServiceInfo(const std::string& clusterId, const std::string& serviceName) {
    json result;
    result["success"] = false;
    
    auto it = clusters.find(clusterId);
    if (it == clusters.end()) {
        result["error"] = "Cluster not found";
        return result;
    }
    
    const SwarmCluster& cluster = it->second;
    
    std::string inspectCmd = "ssh centos@" + cluster.managerIP + 
                            " 'docker service inspect " + serviceName + "'";
    
    auto inspectResult = remoteExec->execute(inspectCmd);
    
    if (!inspectResult.success()) {
        result["error"] = "Failed to get service info: " + inspectResult.output;
        return result;
    }
    
    try {
        result["success"] = true;
        result["service"] = json::parse(inspectResult.output);
    } catch (const std::exception& e) {
        result["error"] = "Failed to parse service info: " + std::string(e.what());
    }
    
    return result;
}

json SwarmOperations::deleteService(const std::string& clusterId, const std::string& serviceName) {
    json result;
    result["success"] = false;
    
    auto it = clusters.find(clusterId);
    if (it == clusters.end()) {
        result["error"] = "Cluster not found";
        return result;
    }
    
    const SwarmCluster& cluster = it->second;
    
    std::string deleteCmd = "ssh centos@" + cluster.managerIP + 
                           " 'docker service rm " + serviceName + "'";
    
    auto deleteResult = remoteExec->execute(deleteCmd);
    
    if (!deleteResult.success()) {
        result["error"] = "Failed to delete service: " + deleteResult.output;
        return result;
    }
    
    result["success"] = true;
    result["message"] = "Service deleted successfully";
    
    return result;
}

// ============================================================================
// NODE MANAGEMENT - Add/Remove Swarm Nodes
// ============================================================================

json SwarmOperations::addWorkerNode(const std::string& clusterId) {
    json result;
    result["success"] = false;
    
    auto it = clusters.find(clusterId);
    if (it == clusters.end()) {
        result["error"] = "Cluster not found";
        return result;
    }
    
    SwarmCluster& cluster = it->second;
    
    if (cluster.workerToken.empty() || cluster.managerIP.empty()) {
        result["error"] = "Cluster not properly initialized";
        return result;
    }
    
    // Generate unique worker name
    int workerNum = 1;
    for (const auto& node : cluster.nodes) {
        if (node.role == "worker") workerNum++;
    }
    std::string workerName = cluster.clusterName + "-worker-" + std::to_string(workerNum);
    
    // Deploy VM using VMOperations
    json vmConfig = {
        {"hostname", workerName},
        {"vcpus", 2},
        {"memory", 2048},
        {"disk", 20},
        {"username", "centos"},
        {"authMethod", "password"},
        {"password", "swarm123"},
        {"owner", cluster.owner},
        {"network", cluster.networkName}
    };
    
    bool deployed = vmOps->deployVM(vmConfig);
    if (!deployed) {
        result["error"] = "Failed to deploy worker VM";
        return result;
    }
    
    // Wait for VM to be ready and get IP
    auto readyResult = waitForVMReady(workerName);
    if (!readyResult["success"].get<bool>()) {
        result["error"] = "Worker VM failed to become ready";
        return result;
    }
    
    std::string workerIP = readyResult["ip"];
    
    // Join worker to swarm
    auto joinResult = joinWorkerToSwarm(workerName, workerIP, cluster.managerIP, cluster.workerToken);
    if (!joinResult["success"].get<bool>()) {
        result["error"] = joinResult["error"];
        return result;
    }
    
    // Add to cluster
    SwarmNode node;
    node.vmName = workerName;
    node.role = "worker";
    node.ip = workerIP;
    node.status = "active";
    cluster.nodes.push_back(node);
    
    saveClusters();
    
    result["success"] = true;
    result["node"] = {
        {"name", workerName},
        {"ip", workerIP},
        {"role", "worker"}
    };
    
    return result;
}

json SwarmOperations::removeNode(const std::string& clusterId, const std::string& vmName) {
    json result;
    result["success"] = false;
    
    auto it = clusters.find(clusterId);
    if (it == clusters.end()) {
        result["error"] = "Cluster not found";
        return result;
    }
    
    SwarmCluster& cluster = it->second;
    
    // Find the node
    auto nodeIt = std::find_if(cluster.nodes.begin(), cluster.nodes.end(),
                              [&vmName](const SwarmNode& n) { return n.vmName == vmName; });
    
    if (nodeIt == cluster.nodes.end()) {
        result["error"] = "Node not found in cluster";
        return result;
    }
    
    // Get node ID from swarm
    std::string getNodeIdCmd = "ssh centos@" + cluster.managerIP + 
                               " 'docker node ls --filter name=" + vmName + 
                               " --format \"{{.ID}}\"'";
    auto nodeIdResult = remoteExec->execute(getNodeIdCmd);
    
    if (nodeIdResult.success() && !nodeIdResult.output.empty()) {
        std::string nodeId = nodeIdResult.output;
        nodeId.erase(nodeId.find_last_not_of(" \n\r\t") + 1);
        
        // Remove from swarm
        std::string removeCmd = "ssh centos@" + cluster.managerIP + 
                               " 'docker node rm --force " + nodeId + "'";
        remoteExec->execute(removeCmd);
    }
    
    // Delete the VM
    vmOps->deleteVM(vmName, true);
    
    // Remove from cluster
    cluster.nodes.erase(nodeIt);
    saveClusters();
    
    result["success"] = true;
    result["message"] = "Node removed successfully";
    
    return result;
}

// ============================================================================
// VM INFRASTRUCTURE HELPERS - Delegated to VMOperations
// ============================================================================

std::string SwarmOperations::generateDockerCloudInit(const std::string& hostname,
                                                    const std::string& username,
                                                    const std::string& role) {
    std::stringstream cloudInit;
    
    cloudInit << "#cloud-config\n"
              << "hostname: " << hostname << "\n"
              << "fqdn: " << hostname << ".local\n"
              << "manage_etc_hosts: true\n\n"
              << "users:\n"
              << "  - name: " << username << "\n"
              << "    sudo: ALL=(ALL) NOPASSWD:ALL\n"
              << "    groups: users, admin, docker\n"
              << "    shell: /bin/bash\n\n"
              << "package_update: true\n"
              << "packages:\n"
              << "  - apt-transport-https\n"
              << "  - ca-certificates\n"
              << "  - curl\n"
              << "  - gnupg\n"
              << "  - lsb-release\n"
              << "  - qemu-guest-agent\n\n"
              << "runcmd:\n"
              << "  # Install Docker\n"
              << "  - curl -fsSL https://get.docker.com -o /tmp/get-docker.sh\n"
              << "  - sh /tmp/get-docker.sh\n"
              << "  - systemctl enable docker\n"
              << "  - systemctl start docker\n"
              << "  - usermod -aG docker " << username << "\n\n"
              << "  # Open required ports for Docker Swarm\n"
              << "  - ufw allow 2377/tcp   # cluster management\n"
              << "  - ufw allow 7946/tcp   # node communication\n"
              << "  - ufw allow 7946/udp   # node communication\n"
              << "  - ufw allow 4789/udp   # overlay network\n"
              << "  - ufw --force enable\n\n"
              << "  # Start qemu-guest-agent\n"
              << "  - systemctl enable qemu-guest-agent\n"
              << "  - systemctl start qemu-guest-agent\n\n"
              << "  # Mark as ready\n"
              << "  - echo 'Docker Swarm node ready (" << role << ")' > /var/log/swarm-node-ready\n\n"
              << "power_state:\n"
              << "  mode: reboot\n"
              << "  timeout: 30\n"
              << "  condition: true\n";
    
    return cloudInit.str();
}

json SwarmOperations::waitForVMReady(const std::string& vmName, int maxWaitSeconds) {
    json result;
    result["success"] = false;
    
    std::cout << "Waiting for " << vmName << " to be ready..." << std::endl;
    
    int waited = 0;
    while (waited < maxWaitSeconds) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        waited += 5;
        
        // Delegate to VMOperations
        auto status = vmOps->getVMStatus(vmName);
        if (!status["success"].get<bool>() || !status["running"].get<bool>()) {
            continue;
        }
        
        auto ipResult = vmOps->getIP(vmName);
        if (ipResult["success"].get<bool>()) {
            std::cout << "  ✅ " << vmName << " is ready (IP: " 
                     << ipResult["primaryIP"].get<std::string>() << ")" << std::endl;
            result["success"] = true;
            result["ip"] = ipResult["primaryIP"];
            return result;
        }
        
        std::cout << "  ⏳ Still waiting... (" << waited << "s/" << maxWaitSeconds << "s)" << std::endl;
    }
    
    result["error"] = "Timeout waiting for VM to be ready";
    return result;
}

json SwarmOperations::getVMIP(const std::string& vmName) {
    return vmOps->getIP(vmName);
}

// ============================================================================
// CLUSTER LIFECYCLE - High-Level Operations
// ============================================================================

std::string SwarmOperations::generateClusterId() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return "swarm_" + std::to_string(now);
}

json SwarmOperations::createCluster(const json& clusterConfig) {
    json result;
    result["success"] = false;
    
    std::string clusterName = clusterConfig["name"];
    std::string owner = clusterConfig["owner"];
    
    // Get manager and worker IPs from config (for existing VMs)
    std::vector<std::string> managerIPs = clusterConfig["managers"];
    std::vector<std::string> workerIPs = clusterConfig.value("workers", 
                                         std::vector<std::string>());
    
    if (managerIPs.empty()) {
        result["error"] = "At least one manager required";
        return result;
    }
    
    // Initialize swarm on first manager
    std::string managerIP = managerIPs[0];
    std::string initCmd = "docker swarm init --advertise-addr " + managerIP;
    auto initResult = remoteExec->execute(initCmd);
    
    if (!initResult.success()) {
        result["error"] = "Failed to initialize swarm: " + initResult.output;
        return result;
    }
    
    // Get join tokens
    auto managerToken = remoteExec->execute("docker swarm join-token manager -q");
    auto workerToken = remoteExec->execute("docker swarm join-token worker -q");
    
    // Join additional managers
    for (size_t i = 1; i < managerIPs.size(); i++) {
        std::string joinCmd = "docker swarm join --token " + 
                            managerToken.output + " " + managerIP + ":2377";
        remoteExec->execute(joinCmd);
    }
    
    // Join workers
    for (const auto& workerIP : workerIPs) {
        std::string joinCmd = "docker swarm join --token " + 
                            workerToken.output + " " + managerIP + ":2377";
        remoteExec->execute(joinCmd);
    }
    
    result["success"] = true;
    result["managerIP"] = managerIP;
    result["managerToken"] = managerToken.output;
    result["workerToken"] = workerToken.output;
    
    return result;
}

json SwarmOperations::createSwarmCluster(const std::string& clusterName, const std::string& owner,
                                        int numManagers, int numWorkers) {
    json result;
    result["success"] = false;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Creating Docker Swarm Cluster" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Step 1: Create cluster structure
    SwarmCluster cluster;
    cluster.clusterId = generateClusterId();
    cluster.clusterName = clusterName;
    cluster.owner = owner;
    cluster.created = std::time(nullptr);
    cluster.status = "creating";
    
    // Step 2: Create dedicated network
    std::cout << "Step 1: Creating dedicated network..." << std::endl;
    auto networkResult = networkMgr->createSwarmNetwork(clusterName, owner);
    if (!networkResult["success"].get<bool>()) {
        result["error"] = "Failed to create network: " + networkResult["error"].get<std::string>();
        return result;
    }
    
    std::string networkName = networkResult["networkName"];
    std::string subnet = networkResult["subnet"];
    cluster.networkName = networkName;
    cluster.subnet = subnet;
    
    std::cout << "  ✅ Network created: " << networkName << " (" << subnet << ")" << std::endl;
    
    // Step 3: Deploy manager nodes
    std::cout << "\nStep 2: Deploying manager nodes..." << std::endl;
    for (int i = 0; i < numManagers; i++) {
        std::string nodeName = clusterName + "-manager-" + std::to_string(i + 1);
        std::cout << "  Deploying " << nodeName << "..." << std::endl;
        
        json vmConfig = {
            {"hostname", nodeName},
            {"vcpus", 2},
            {"memory", 2048},
            {"disk", 20},
            {"username", "centos"},
            {"authMethod", "password"},
            {"password", "swarm123"},
            {"owner", owner},
            {"network", networkName}
        };
        
        bool deployed = vmOps->deployVM(vmConfig);
        if (!deployed) {
            result["error"] = "Failed to deploy manager node: " + nodeName;
            return result;
        }
        
        SwarmNode node;
        node.vmName = nodeName;
        node.role = "manager";
        node.status = "provisioning";
        cluster.nodes.push_back(node);
    }
    
    // Step 4: Deploy worker nodes
    std::cout << "\nStep 3: Deploying worker nodes..." << std::endl;
    for (int i = 0; i < numWorkers; i++) {
        std::string nodeName = clusterName + "-worker-" + std::to_string(i + 1);
        std::cout << "  Deploying " << nodeName << "..." << std::endl;
        
        json vmConfig = {
            {"hostname", nodeName},
            {"vcpus", 2},
            {"memory", 2048},
            {"disk", 20},
            {"username", "centos"},
            {"authMethod", "password"},
            {"password", "swarm123"},
            {"owner", owner},
            {"network", networkName}
        };
        
        bool deployed = vmOps->deployVM(vmConfig);
        if (!deployed) {
            result["error"] = "Failed to deploy worker node: " + nodeName;
            return result;
        }
        
        SwarmNode node;
        node.vmName = nodeName;
        node.role = "worker";
        node.status = "provisioning";
        cluster.nodes.push_back(node);
    }
    
    // Step 5: Wait for all nodes to be ready
    std::cout << "\nStep 4: Waiting for all nodes to be ready..." << std::endl;
    for (auto& node : cluster.nodes) {
        auto readyResult = waitForVMReady(node.vmName);
        if (!readyResult["success"].get<bool>()) {
            result["error"] = "Node " + node.vmName + " failed to become ready";
            result["warning"] = "Cluster partially created - manual intervention required";
            cluster.status = "partial";
            clusters[cluster.clusterId] = cluster;
            saveClusters();
            return result;
        }
        
        node.ip = readyResult["ip"];
        node.status = "ready";
    }
    
    // Step 6: Get manager IP (first manager)
    cluster.managerIP = cluster.nodes[0].ip;
    
    // Save cluster
    cluster.status = "ready";
    clusters[cluster.clusterId] = cluster;
    saveClusters();
    
    // Build result with instructions
    result["success"] = true;
    result["clusterId"] = cluster.clusterId;
    result["networkName"] = networkName;
    result["subnet"] = subnet;
    result["nodes"] = json::array();
    
    for (const auto& node : cluster.nodes) {
        result["nodes"].push_back({
            {"name", node.vmName},
            {"role", node.role},
            {"ip", node.ip},
            {"status", node.status}
        });
    }
    
    // Generate setup instructions
    std::stringstream instructions;
    instructions << "\n========================================\n";
    instructions << "Docker Swarm Cluster Created!\n";
    instructions << "========================================\n\n";
    instructions << "Cluster ID: " << cluster.clusterId << "\n";
    instructions << "Network: " << networkName << " (" << subnet << ")\n\n";
    instructions << "Next Steps:\n\n";
    instructions << "1. Initialize Swarm on manager:\n";
    instructions << "   SSH to " << cluster.nodes[0].vmName << " (" << cluster.managerIP << ")\n";
    instructions << "   Run: docker swarm init --advertise-addr " << cluster.managerIP << "\n\n";
    instructions << "2. Get worker join token:\n";
    instructions << "   Run: docker swarm join-token worker\n\n";
    instructions << "3. Join worker nodes:\n";
    for (size_t i = numManagers; i < cluster.nodes.size(); i++) {
        instructions << "   SSH to " << cluster.nodes[i].vmName << " (" << cluster.nodes[i].ip << ")\n";
        instructions << "   Run the join command from step 2\n\n";
    }
    instructions << "4. Verify cluster:\n";
    instructions << "   Run: docker node ls\n\n";
    
    result["instructions"] = instructions.str();
    result["message"] = "Swarm cluster created successfully! Follow the instructions to complete setup.";
    
    std::cout << instructions.str() << std::endl;
    
    return result;
}

json SwarmOperations::listClusters(const std::string& owner) {
    json result;
    result["success"] = true;
    result["clusters"] = json::array();
    
    for (const auto& [id, cluster] : clusters) {
        if (!owner.empty() && cluster.owner != owner) {
            continue;
        }
        
        json clusterInfo = {
            {"clusterId", cluster.clusterId},
            {"clusterName", cluster.clusterName},
            {"owner", cluster.owner},
            {"networkName", cluster.networkName},
            {"subnet", cluster.subnet},
            {"nodeCount", cluster.nodes.size()},
            {"status", cluster.status},
            {"created", cluster.created}
        };
        
        int managerCount = 0, workerCount = 0;
        for (const auto& node : cluster.nodes) {
            if (node.role == "manager") managerCount++;
            else workerCount++;
        }
        
        clusterInfo["managers"] = managerCount;
        clusterInfo["workers"] = workerCount;
        
        result["clusters"].push_back(clusterInfo);
    }
    
    return result;
}

json SwarmOperations::getClusterInfo(const std::string& clusterId) {
    json result;
    result["success"] = false;
    
    auto it = clusters.find(clusterId);
    if (it == clusters.end()) {
        result["error"] = "Cluster not found";
        return result;
    }
    
    const SwarmCluster& cluster = it->second;
    
    result["success"] = true;
    result["cluster"] = {
        {"clusterId", cluster.clusterId},
        {"clusterName", cluster.clusterName},
        {"owner", cluster.owner},
        {"networkName", cluster.networkName},
        {"subnet", cluster.subnet},
        {"managerIP", cluster.managerIP},
        {"status", cluster.status},
        {"created", cluster.created},
        {"nodes", json::array()}
    };
    
    for (const auto& node : cluster.nodes) {
        result["cluster"]["nodes"].push_back({
            {"vmName", node.vmName},
            {"role", node.role},
            {"ip", node.ip},
            {"status", node.status}
        });
    }
    
    return result;
}

json SwarmOperations::deleteCluster(const std::string& clusterId) {
    json result;
    result["success"] = false;
    
    auto it = clusters.find(clusterId);
    if (it == clusters.end()) {
        result["error"] = "Cluster not found";
        return result;
    }
    
    SwarmCluster& cluster = it->second;
    
    std::cout << "Deleting cluster: " << cluster.clusterName << std::endl;
    
    // Delete all VMs (delegated to VMOperations)
    for (const auto& node : cluster.nodes) {
        std::cout << "  Deleting VM: " << node.vmName << std::endl;
        vmOps->deleteVM(node.vmName, true);
    }
    
    // Delete network (delegated to NetworkManager)
    std::cout << "  Deleting network: " << cluster.networkName << std::endl;
    networkMgr->deleteSwarmNetwork(cluster.clusterName);
    
    // Remove from clusters
    clusters.erase(it);
    saveClusters();
    
    result["success"] = true;
    result["message"] = "Cluster deleted successfully";
    
    return result;
}