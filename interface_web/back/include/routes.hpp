#ifndef ROUTES_HPP
#define ROUTES_HPP

#include "httplib.h"
#include "host_manager.hpp"
#include "resource_logger.hpp"
#include "vm_operations.hpp"
#include "user_operations.hpp"
#include "paas_operations.hpp"
#include "swarm_operations.hpp"
#include "flavor_manager.hpp"
#include "vnc_handler.hpp"
#include "network_manager.hpp"
#include "baseimage_manager.hpp"


// Helper function to extract user info from request
struct UserContext {
    std::string userId;
    std::string role;
    bool isAdmin;
    bool is_auth;
    
    UserContext() : userId(""), role(""), isAdmin(false), is_auth(false) {}
};


class APIRoutes {
private:
    NetworkManager *networkMgr;
    ResourceLogger* Rlogger;
    FlavorManager *flavorMgr;
    VMOperations* vmOps;
    HostManager* manager;
    UserOperations *userOps;
    PaaSOperations *paasOps;
    SwarmOperations *swarmOps;

    BaseImageManager* baseImageManager;
    VNCHandler *vncHandler;    


public:
    APIRoutes(VMOperations* operations, HostManager *hostMgr, UserOperations *userOps, 
        PaaSOperations *paas, SwarmOperations *swarm, ResourceLogger* Rlog, FlavorManager *flMgr, 
        NetworkManager *ntMgr, BaseImageManager* biMgr, VNCHandler *vnc);
    
    // Setup all API routes
    void setup(httplib::Server& svr);

private:
    // Route handlers
    UserContext extractUserContext(const httplib::Request& req);
    bool requireAuth(const httplib::Request& req, 
                            httplib::Response& res,
                            UserContext& ctx);
    bool requireAdmin(const httplib::Request& req,
                                httplib::Response& res,
                                UserContext& ctx);
    bool validateVMName(const std::string& name);

    void handleLogin(const httplib::Request& req, httplib::Response& res);
    void handleListVMs(const httplib::Request& req, httplib::Response& res);
    void handleGetVMInfo(const httplib::Request& req, httplib::Response& res);
    void handleGetIP(const httplib::Request& req, httplib::Response& res);
    void handleGetVMStatus(const httplib::Request& req, httplib::Response& res);
    void handleGetVMStats(const httplib::Request& req, httplib::Response& res);
    void handleStartVM(const httplib::Request& req, httplib::Response& res);
    void handleShutdownVM(const httplib::Request& req, httplib::Response& res);
    void handleDestroyVM(const httplib::Request& req, httplib::Response& res);
    void handleRebootVM(const httplib::Request& req, httplib::Response& res);
    void handlePauseVM(const httplib::Request& req, httplib::Response& res);
    void handleResumeVM(const httplib::Request& req, httplib::Response& res);
    void handleGetVNC(const httplib::Request& req, httplib::Response& res);

    void handleListSnapshots(const httplib::Request& req, httplib::Response& res);
    void handleCreateSnapshot(const httplib::Request& req, httplib::Response& res);
    void handleRevertSnapshot(const httplib::Request& req, httplib::Response& res);
    void handleDeleteSnapshot(const httplib::Request& req, httplib::Response& res);

    void handleCloneVM(const httplib::Request& req, httplib::Response& res);
    void handleSystemInfo(const httplib::Request& req, httplib::Response& res);
    void handleDeployVM(const httplib::Request& req, httplib::Response& res);
    void handleDeleteVM(const httplib::Request& req, httplib::Response& res); 

    void handleListUsers(const httplib::Request& req, httplib::Response& res);
    void handleCreateUser(const httplib::Request& req, httplib::Response& res);
    void handleUpdateUser(const httplib::Request& req, httplib::Response& res);
    void handleDeleteUser(const httplib::Request& req, httplib::Response& res);
    void handleGetUserUsage(const httplib::Request& req, httplib::Response& res);

    // Flavors
    void handleListFlavors(const httplib::Request& req __attribute__((unused)), httplib::Response& res);
    // Base images  
    void handleListImages(const httplib::Request& req __attribute__((unused)), httplib::Response& res);
    // Networks
    void handleListAllNetworks(const httplib::Request& req, httplib::Response& res);
    void handleListUserNetworks(const httplib::Request& req, httplib::Response& res);
    void handleCreateNetwork(const httplib::Request& req, httplib::Response& res);
    void handleGetNetwork(const httplib::Request& req, httplib::Response& res);
    void handleUpdateNetwork(const httplib::Request& req, httplib::Response& res);
    void handleDeleteNetwork(const httplib::Request& req, httplib::Response& res);

    // Swarm clusters
    void handleCreateCluster(const httplib::Request& req, httplib::Response& res);
    void handleListClusters(const httplib::Request& req, httplib::Response& res);
    void handleGetCluster(const httplib::Request& req, httplib::Response& res);
    void handleDeleteCluster(const httplib::Request& req, httplib::Response& res);
    // Host strategy
    void handleSetHostSelectionStrategy(const httplib::Request& req, httplib::Response& res);
    void handleGetSaaSBilling(const httplib::Request& req, httplib::Response& res);

    void handleProvisionDatabase(const httplib::Request& req, httplib::Response& res);
    void handleStartApp(const httplib::Request& req, httplib::Response& res);
    void handleStopApp(const httplib::Request& req, httplib::Response& res);
    void handleGetAppLogs(const httplib::Request& req, httplib::Response& res);
    void handleGetAppStats(const httplib::Request& req, httplib::Response& res);
    void handleGetDatabaseCredentials(const httplib::Request& req, httplib::Response& res);
    void handlePaaSSelectHost(const httplib::Request& req, httplib::Response& res);
    void handlePaaSDeploy(const httplib::Request& req, httplib::Response& res);
    void handlePaaSAppDetails(const httplib::Request& req, httplib::Response& res);
    void handleListPaaSApplications(const httplib::Request& req, httplib::Response& res);
    void handlePaaSMigrate(const httplib::Request& req, httplib::Response& res);
    void handlePaaSServerInfo(const httplib::Request& req, httplib::Response& res);
    void handleDeletePaaSApp(const httplib::Request& req, httplib::Response& res);
    void handleGetHostsAvailableForMigration(const httplib::Request& req, httplib::Response& res);
    void handleGetHostDetails(const httplib::Request& req, httplib::Response& res);
    void handleGetAvailableHosts(const httplib::Request& req, httplib::Response& res);

    void handleCreatePortForward(const httplib::Request& req, httplib::Response& res);
    void handleListPortForwards(const httplib::Request& req, httplib::Response& res);
    void handleDeletePortForward(const httplib::Request& req, httplib::Response& res);
    
    void handleUpdateVMMetadata(const httplib::Request& req, httplib::Response& res);
    void handleActionVM(const httplib::Request& req, httplib::Response& res);
};





#endif // ROUTES_HPP