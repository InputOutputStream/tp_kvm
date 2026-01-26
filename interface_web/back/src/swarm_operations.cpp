#include "../include/swarm_operations.hpp"
#include "../include/utils.hpp"
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <sys/stat.h>

SwarmOperations::SwarmOperations(virConnectPtr connection, VMOperations* vmOperations,
                                NetworkManager* networkManager)
    : conn(connection), vmOps(vmOperations), networkMgr(networkManager),
      clustersConfigFile("/var/lib/thoth-cloud/swarm_clusters.json") {
    loadClusters();
}

SwarmOperations::~SwarmOperations() {
    saveClusters();
}

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

std::string SwarmOperations::generateClusterId() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return "swarm_" + std::to_string(now);
}

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
        
        // Check if VM is running
        auto status = vmOps->getVMStatus(vmName);
        if (!status["success"].get<bool>() || !status["running"].get<bool>()) {
            continue;
        }
        
        // Try to get IP
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

json SwarmOperations::createSwarmCluster(const std::string& clusterName, const std::string& owner,
                                        int numManagers, int numWorkers) {
    json result;
    result["success"] = false;
    
    // Validate: managers must be odd number (1, 3, 5, etc.)
    if (numManagers % 2 == 0) {
        result["error"] = "Number of managers must be odd (1, 3, 5, etc.) for quorum";
        return result;
    }
    
    if (numManagers < 1 || numWorkers < 0) {
        result["error"] = "Invalid number of nodes";
        return result;
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Creating Docker Swarm Cluster" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Cluster: " << clusterName << std::endl;
    std::cout << "Owner: " << owner << std::endl;
    std::cout << "Managers: " << numManagers << std::endl;
    std::cout << "Workers: " << numWorkers << std::endl;
    std::cout << std::endl;
    
    // Step 1: Create dedicated network for cluster
    std::cout << "Step 1: Creating dedicated network..." << std::endl;
    auto networkResult = networkMgr->createSwarmNetwork(clusterName, owner);
    if (!networkResult["success"].get<bool>()) {
        result["error"] = "Failed to create cluster network";
        return result;
    }
    
    std::string networkName = networkResult["networkName"];
    std::string subnet = networkResult["subnet"];
    std::cout << "  ✅ Network created: " << networkName << " (" << subnet << ")" << std::endl;
    
    // Step 2: Create cluster object
    SwarmCluster cluster;
    cluster.clusterId = generateClusterId();
    cluster.clusterName = clusterName;
    cluster.owner = owner;
    cluster.networkName = networkName;
    cluster.subnet = subnet;
    cluster.created = std::time(nullptr);
    cluster.status = "creating";
    
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
            {"username", "ubuntu"},
            {"authMethod", "password"},
            {"password", "swarm123"},  // Should be configurable
            {"owner", owner},
            {"network", networkName}
        };
        
        // Note: You'll need to modify deployVM to support custom cloud-init
        // For now, this is a placeholder
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
            {"username", "ubuntu"},
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
    
    // Delete all VMs
    for (const auto& node : cluster.nodes) {
        std::cout << "  Deleting VM: " << node.vmName << std::endl;
        vmOps->deleteVM(node.vmName, true);
    }
    
    // Delete network
    std::cout << "  Deleting network: " << cluster.networkName << std::endl;
    networkMgr->deleteSwarmNetwork(cluster.clusterName);
    
    // Remove from clusters
    clusters.erase(it);
    saveClusters();
    
    result["success"] = true;
    result["message"] = "Cluster deleted successfully";
    
    return result;
}
