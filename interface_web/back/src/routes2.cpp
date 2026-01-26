

#include "../include/routes.hpp"
#include "../include/utils.hpp"
#include "../include/user_operations.hpp"
#include "../include/paas_operations.hpp"
#include "../include/swarm_operations.hpp"
#include "../include/host_manager.hpp"
#include "../include/json.hpp"
#include "../include/flavor_manager.hpp"
#include "../include/network_manager.hpp"
#include "../include/baseimage_manager.hpp"
#include <sstream>


UserContext getUserContext(const httplib::Request& req, UserOperations* userOps);

using json = nlohmann::json;


APIRoutes::APIRoutes(VMOperations* operations, HostManager* mgr, UserOperations* user_operations,
    PaaSOperations *paas, SwarmOperations *swarm, 
    ResourceLogger* log, FlavorManager *flMgr, 
    NetworkManager *ntMgr, BaseImageManager* biMgr) 
    : vmOps(operations), manager(mgr), userOps(user_operations), paasOps(paas), swarmOps(swarm), Rlogger(log), 
    networkMgr(ntMgr),flavorMgr(flMgr), baseImageManager(biMgr){}

// Flavors
void APIRoutes::handleListFlavors(const httplib::Request& req, httplib::Response& res){
    json result;
    result = flavorMgr->getAllFlavors(true);
    res.set_content(result.dump(), "application/json");
} 

// Base images  
void APIRoutes::handleListImages(const httplib::Request& req, httplib::Response& res){
    json result;
    result = baseImageManager->listImages();
    res.set_content(result.dump(), "application/json");
}

// Networks
void APIRoutes::handleListAllNetworks(const httplib::Request& req, httplib::Response& res){
    auto userCtx = getUserContext(req, userOps);
    
    json result;
    if (userCtx.isAdmin) {
        json result;
        result = networkMgr->listAllNetworks();
        res.set_content(result.dump(), "application/json");
    } else {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
}


void APIRoutes::handleListUserNetworks(const httplib::Request& req, httplib::Response& res){
    std::string userID = req.matches[1];
    json result;
    result = networkMgr->listUserNetworks(userID);
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleCreateNetwork(const httplib::Request& req, httplib::Response& res){
    std::string userID = req.matches[1];
    json result;
    result = networkMgr->createUserNetwork(userID);
    res.set_content(result.dump(), "application/json");    
}

// Swarm clusters
void APIRoutes::handleCreateCluster(const httplib::Request& req, httplib::Response& res){
    json result;
    json body;

    try {
        body = json::parse(req.body);
   
    } catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        res.status = 400;
        json error = {
            {"success", false}, 
            {"error", "Invalid JSON: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
        return;
    }

    std::string owner = body["user"];
    std::string clusterName = body["clusterName"];
    int numManagers = body["numManagers"];
    int numWorkers = body["numWorkers"];

    result = swarmOps->createSwarmCluster(clusterName, owner, numManagers, numWorkers);
    res.set_content(result.dump(), "application/json");    
}

void APIRoutes::handleListClusters(const httplib::Request& req, httplib::Response& res){
    std::string userID = req.matches[1];
    json result;
    result = swarmOps->listClusters(userID);
    res.set_content(result.dump(), "application/json");    
}

void APIRoutes::handleGetCluster(const httplib::Request& req, httplib::Response& res){
    std::string custerId = req.matches[1];
    json result;
    result = swarmOps->getClusterInfo(custerId);
    res.set_content(result.dump(), "application/json");    
}
void APIRoutes::handleDeleteCluster(const httplib::Request& req, httplib::Response& res){
    std::string custerId = req.matches[1];
    json result;
    result = swarmOps->deleteCluster(custerId);
    res.set_content(result.dump(), "application/json");    
}

// Host strategy
void APIRoutes::handleSetHostSelectionStrategy(const httplib::Request& req, httplib::Response& res){
    json body;
    try {
        body = json::parse(req.body);
   
    } catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        res.status = 400;
        json error = {
            {"success", false}, 
            {"error", "Invalid JSON: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
        return;
    }

    int strat = body["HostSelectionStrategy"];
    bool success = manager->setSelectionStrategy(strat);
    json result = {{"success", success}};
    res.set_content(result.dump(), "application/json");
}