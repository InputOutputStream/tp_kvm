#include <iostream>
#include "../include/httplib.h"
#include "../include/vm_operations.hpp"
#include "../include/host_manager.hpp"
#include "../include/routes.hpp"
#include "../include/cors.hpp"
#include "../include/utils.hpp"
#include "../include/config_manager.hpp"
#include "../include/resource_logger.hpp"
#include "../include/paas_operations.hpp"
#include "../include/swarm_operations.hpp"
#include "../include/isolation_utils.hpp"
#include "../include/network_proxy_service.hpp"
#include "../include/flavor_manager.hpp"
#include "../include/network_manager.hpp"
#include "../include/baseimage_manager.hpp"
#include "../include/vnc_handler.hpp"

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
        logger.logSystemEvent(LogLevel::CRITICAL, "No hosts available - cannot start");
        return 1;
    }
    
    // Get primary connection
    std::string primaryHostId = hostList["hosts"][0]["id"];
    virConnectPtr primaryConn = hostManager.getConnection(primaryHostId);
    
    if (!primaryConn) {
        std::cerr << "Failed to get primary connection!" << std::endl;
        logger.logSystemEvent(LogLevel::CRITICAL, "Failed to connect to primary host");
        return 1;
    }
    

    // NetworkManager needs connection
    NetworkManager networkMgr(primaryConn);
    
    // RemoteExecutor needs connection
    RemoteExec::RemoteExecutor remoteExec(primaryConn);
    
    // UserOperations needs connection and networkMgr
    UserOperations userOps(primaryConn, &networkMgr);
    
    ResourceMetadataStore g_metadataStore;
    
    NetworkProxyService g_proxyService(
        &remoteExec,
        hostList["hosts"][0]["id"]   
    );   

    std::cerr << " I am here "<< std::endl;

    // VMOperations needs everything initialized before it
    VMOperations vmOps(primaryConn, &hostManager, &networkMgr, 
                      &g_metadataStore, &g_proxyService);
    
    // BaseImageManager needs remoteExec
    BaseImageManager imageMgr(&remoteExec);
    
    // FlavorManager is standalone
    FlavorManager flavorMgr;
    
    // PaaSOperations needs connection, remoteExec, and hostManager
    PaaSOperations paasOps(primaryConn, &remoteExec, &hostManager);
    
    // SwarmOperations needs everything
    SwarmOperations swarmOps(primaryConn, &vmOps, &networkMgr, 
                            &remoteExec, &hostManager);
    
    // VNCHandler needs connection
    VNCHandler vncHandler(primaryConn);
    
    logger.logSystemEvent(LogLevel::INFO, "All components initialized successfully");
    
    // Initialize routes with all dependencies
    APIRoutes apiRoutes(&vmOps, &hostManager, &userOps, &paasOps, 
                       &swarmOps, &logger, &flavorMgr, &networkMgr, 
                       &imageMgr, &vncHandler);
    
    // Create and configure server
    Server svr;
    cors::setupMiddleware(svr);
    apiRoutes.setup(svr);
    
    int port = config.getInt("API_PORT", 3000);
    
    std::cout << "==================================" << std::endl;
    std::cout << "  THOTH CLOUD Platform" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Server started on http://0.0.0.0:" << port << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "==================================" << std::endl;
    
    logger.logSystemEvent(LogLevel::INFO, "Server listening on port " + 
                         std::to_string(port));
    
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "Failed to bind to port " << port << std::endl;
        logger.logSystemEvent(LogLevel::CRITICAL, 
                            "Failed to bind to port " + std::to_string(port));
        return 1;
    }
    
    logger.logSystemEvent(LogLevel::INFO, "Server shutdown");
    return 0;
}