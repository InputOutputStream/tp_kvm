#include <iostream>
#include "../include/httplib.h"
#include "../include/vm_operations.hpp"
#include "../include/host_manager.hpp"
#include "../include/routes.hpp"
#include "../include/cors.hpp"
#include "../include/utils.hpp"
#include "../include/config_manager.hpp"
#include "../include/host_manager.hpp"
#include "../include/resource_logger.hpp"
#include "../include/paas_operations.hpp"
#include "../include/swarm_operations.hpp"
#include "../include/flavor_manager.hpp"
#include "../include/network_manager.hpp"
#include "../include/baseimage_manager.hpp"

using namespace httplib;


int main() {
    ConfigManager config;
    ResourceLogger logger;
    logger.logSystemEvent(LogLevel::INFO, "Starting THOTH CLOUD server");
    
    // Initialize host manager
    HostManager hostManager;
    
    // Load hosts from config
    auto hosts = config.getList("HOSTS");
    if (hosts.empty()) {
        logger.logSystemEvent(LogLevel::ERROR, "Empty Host list File");
    }
    
    for (const auto& host : hosts) {
        if (hostManager.addHost(host)) {
            logger.logSystemEvent(LogLevel::INFO, "Added host: " + host);
        } else {
            logger.logSystemEvent(LogLevel::ERROR, "Failed to add host: " + host);
        }
    }

    auto hostList = hostManager.listHosts();
    if (hostList["hosts"].empty()) {
        std::cerr << "No hosts available!" << std::endl;
        return 1;
    }
    
    // Get primary connection
    std::string primaryHostId = hostList["hosts"][0]["id"];
    virConnectPtr primaryConn = hostManager.getConnection(primaryHostId);
    
    // Initialize in correct order (NetworkManager before VMOperations)
    NetworkManager networkMgr(primaryConn);
    RemoteExec::RemoteExecutor remoteExec(primaryConn);
    
    // Now initialize components that depend on networkMgr
    UserOperations userOps(primaryConn, &networkMgr);
    VMOperations vmOps(primaryConn, &hostManager, &networkMgr);
    BaseImageManager imageMgr(&remoteExec);
    FlavorManager flavorMgr;  
    PaaSOperations paasOps(primaryConn, &remoteExec, &hostManager);
    SwarmOperations swarmOps(primaryConn, &vmOps, &networkMgr, &remoteExec, &hostManager);

    // Initialize routes
    APIRoutes apiRoutes(&vmOps, &hostManager, &userOps, &paasOps, 
                       &swarmOps, &logger, &flavorMgr, &networkMgr, &imageMgr);
    
    // Create server
    Server svr;
    cors::setupMiddleware(svr);
    apiRoutes.setup(svr);
    
    int port = config.getInt("API_PORT", 3000);
    
    std::cout << "Server started on http://localhost:" << port << std::endl;
    logger.logSystemEvent(LogLevel::INFO, "Server listening on port " + 
                         std::to_string(port));
    
    svr.listen("0.0.0.0", port);
    
    logger.logSystemEvent(LogLevel::INFO, "Server shutdown");
    return 0;
}