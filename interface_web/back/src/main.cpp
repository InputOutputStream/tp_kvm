#include <iostream>
#include "../include/httplib.h"
#include "../include/libvirt_manager.hpp"
#include "../include/vm_operations.hpp"
#include "../include/host_manager.hpp"
#include "../include/routes.hpp"
#include "../include/cors.hpp"
#include "../include/utils.hpp"
#include "../include/definitions.hpp"
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

    FlavorManager flavorMgr;
    ConfigManager config;
    ResourceLogger logger;
    logger.logSystemEvent(LogLevel::INFO, "Starting THOTH CLOUD server");
    
    // Initialize host manager
    HostManager hostManager;
    
    // Load hosts from config
    auto hosts = config.getList("HOSTS");
    if (hosts.empty()) {
        logger.logSystemEvent(LogLevel::ERROR, "Empty Host list File: ");
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
    
    // Initialize operations
    std::string primaryHostId = hostList["hosts"][0]["id"];
    virConnectPtr primaryConn = hostManager.getConnection(primaryHostId);
    
    VMOperations vmOps(primaryConn, &hostManager);
    NetworkManager networkMgr(primaryConn);
    RemoteExec::RemoteExecutor remoteExec(primaryConn);
    BaseImageManager imageMgr(&remoteExec);
    FlavorManager flvMgr;
    UserOperations userOps(primaryConn);
    PaaSOperations paasOps(primaryConn);
    SwarmOperations swarmOps(primaryConn, &vmOps, &networkMgr);

    // Initialize routes with all dependencies
    APIRoutes apiRoutes(&vmOps, &hostManager, &userOps, &paasOps, 
        &swarmOps, &logger, &flvMgr, &networkMgr, &imageMgr);
    
    // Create server
    Server svr;
    cors::setupMiddleware(svr);
    apiRoutes.setup(svr);
    
    int port = config.getInt("API_PORT", 3000);
    
    std::cout << "Server started on http://localhost:" << port << std::endl;
    logger.logSystemEvent(LogLevel::INFO, "Server listening on port " + std::to_string(port));
    
    svr.listen("0.0.0.0", port);
    
    logger.logSystemEvent(LogLevel::INFO, "Server shutdown");
    return 0;
}