#include "../include/routes.hpp"
#include "../include/utils.hpp"
#include "../include/user_operations.hpp"
#include "../include/paas_operations.hpp"
#include "../include/swarm_operations.hpp"
#include "../include/host_manager.hpp"
#include "../include/json.hpp"
#include <sstream>

using json = nlohmann::json;

void APIRoutes::setup(httplib::Server& svr) {
    
    // ==========================================
    // 1. SYSTEM & HOST MANAGEMENT (Admin)
    // ==========================================
    
    // List configured hosts (Admin)
    svr.Get("/api/hosts", [this](const httplib::Request& req, httplib::Response& res) {
        auto userCtx = extractUserContext(req);
        if (!userCtx.isAdmin) {
            res.status = 403;
            json error = {{"success", false}, {"error", "Admin access required"}};
            res.set_content(error.dump(), "application/json");
            return;
        }
        json result = manager->listHosts();
        res.set_content(result.dump(), "application/json");
    });

    // Add new host
    svr.Post("/api/hosts", [this](const httplib::Request& req, httplib::Response& res) {
        auto userCtx = extractUserContext(req);
        if (!userCtx.isAdmin) {
            res.status = 403;
            return;
        }
        json body = json::parse(req.body);
        bool success = manager->addHost(body["uri"]);
        json result = {{"success", success}};
        res.set_content(result.dump(), "application/json");
    });

    // Host Statistics & Strategy
    svr.Get("/api/hosts/stats", [this](const httplib::Request& req, httplib::Response& res) {
        // Wrapper for getAllHostsStats
        json result = manager->getAllHostsStats();
        res.set_content(result.dump(), "application/json");
    });

    svr.Put("/api/hosts/strategy", [this](const httplib::Request& req, httplib::Response& res) { 
        this->handleSetHostSelectionStrategy(req, res);
    });

    svr.Get("/api/hosts/available", [this](auto& req, auto& res) {
        handleGetHostsAvailableForMigration(req, res);
    });

    svr.Get(R"(/api/hosts/([^/]+))", [this](auto& req, auto& res) {
        handleGetHostDetails(req, res);
    });

    svr.Get("/api/system/info", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleSystemInfo(req, res);
    });

    // ==========================================
    // 2. AUTHENTICATION
    // ==========================================

    svr.Post("/api/auth/login", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleLogin(req, res);
    });

    svr.Post("/api/auth/register", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleCreateUser(req, res);
    });

    svr.Get("/api/auth/validate", [this](const httplib::Request& req, httplib::Response& res) {
        std::string authHeader = req.get_header_value("Authorization");
        if (authHeader.find("Bearer ") != 0) {
            res.status = 401;
            json error = {{"success", false}, {"error", "Invalid authorization header"}};
            res.set_content(error.dump(), "application/json");
            return;
        }
        
        std::string token = authHeader.substr(7);
        std::string username, role;
        
        if (userOps->validateToken(token, username, role)) {
            json result = {
                {"success", true},
                {"user", {{"username", username}, {"role", role}}}
            };
            res.set_content(result.dump(), "application/json");
        } else {
            res.status = 401;
            json error = {{"success", false}, {"error", "Invalid or expired token"}};
            res.set_content(error.dump(), "application/json");
        }
    });

    // ==========================================
    // 3. USER MANAGEMENT
    // ==========================================

    svr.Get("/api/users", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleListUsers(req, res);
    });

    svr.Post("/api/users", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleCreateUser(req, res);
    });

    svr.Put("/api/users/:username", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleUpdateUser(req, res);
    });

    svr.Delete("/api/users/:username", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleDeleteUser(req, res);
    });

    svr.Get("/api/users/:username/usage", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetUserUsage(req, res);
    });
    
    svr.Get("/api/users/usage/all", [this](const httplib::Request& req, httplib::Response& res) {
        auto userCtx = extractUserContext(req);
        if (!userCtx.isAdmin) {
            res.status = 403;
            json error = {{"success", false}, {"error", "Admin access required"}};
            res.set_content(error.dump(), "application/json");
            return;
        }
        json result = userOps->getAllUsersUsage();
        res.set_content(result.dump(), "application/json");
    });

    // ==========================================
    // 4. VIRTUAL MACHINES (Compute)
    // ==========================================

    // Listing & Deployment
    svr.Get("/api/vms", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleListVMs(req, res);
    });

    svr.Post("/api/vms/deploy", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleDeployVM(req, res);
    });

    // Specific VM Operations (Using ID/Name)
    // Note: Matches /api/vms/i-1a2b3c...
    svr.Get(R"(/api/vms/([^/]+)$)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetVMInfo(req, res);
    });

    svr.Patch(R"(/api/vms/([^/]+))", [this](const auto& req, auto& res) {
        handleUpdateVMMetadata(req, res);
    });

    svr.Delete(R"(/api/vms/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleDeleteVM(req, res);
    });

    // Power Management & Actions
    svr.Get(R"(/api/vms/([^/]+)/status)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetVMStatus(req, res);
    });

    svr.Get(R"(/api/vms/([^/]+)/stats)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetVMStats(req, res);
    });

    svr.Post(R"(/api/vms/([^/]+)/start)", [this](const auto& req, auto& res) {
        handleStartVM(req, res);
    });

    svr.Post(R"(/api/vms/([^/]+)/shutdown)", [this](const auto& req, auto& res) {
        handleShutdownVM(req, res);
    });
    
    svr.Post(R"(/api/vms/([^/]+)/destroy)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleDestroyVM(req, res);
    });
    
    svr.Post(R"(/api/vms/([^/]+)/reboot)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleRebootVM(req, res);
    });
    
    svr.Post(R"(/api/vms/([^/]+)/pause)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handlePauseVM(req, res);
    });
    
    svr.Post(R"(/api/vms/([^/]+)/resume)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleResumeVM(req, res);
    });
    
    svr.Post(R"(/api/vms/([^/]+)/clone)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleCloneVM(req, res);
    });

    // ==========================================
    // 5. VM ACCESS & NETWORKING
    // ==========================================

    svr.Get(R"(/api/vms/([^/]+)/ip)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetIP(req, res);
    });

    // VNC Access
    svr.Get(R"(/api/vms/([^/]+)/vnc)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetVNC(req, res);
    });

    svr.Post("/api/vms/:name/vnc/enable", [this](const httplib::Request& req, httplib::Response& res) {
        // Forward to handler logic, ensuring auth is checked
        auto userCtx = extractUserContext(req);
        if (!userCtx.is_auth) { res.status = 401; return; }
        
        std::string vmName = req.path_params.at("name");
        json body = json::parse(req.body);
        std::string password = body.value("password", "");
        
        json result = vncHandler->enableVNC(vmName, password);
        res.set_content(result.dump(), "application/json");
    });
    
    svr.Get("/api/vnc/status", [this](const httplib::Request& req, httplib::Response& res) {
        auto userCtx = extractUserContext(req);
        if (!userCtx.is_auth) { res.status = 401; return; }
        
        json result = vncHandler->getNoVNCStatus();
        res.set_content(result.dump(), "application/json");
    });

    // Port Forwarding
    svr.Get("/api/forwards", [this](const auto& req, auto& res) {
        handleListPortForwards(req, res);
    });

    svr.Get(R"(/api/vms/([^/]+)/forwards)", [this](const httplib::Request& req, httplib::Response& res) {
         // Assuming handleListPortForwards handles the ID extraction or specific logic
        handleListPortForwards(req, res); 
    });

    svr.Post(R"(/api/vms/([^/]+)/forwards)", [this](const auto& req, auto& res) {
        handleCreatePortForward(req, res);
    });
    
    svr.Delete(R"(/api/vms/([^/]+)/forward/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
         handleDeletePortForward(req, res);
    });
    // Legacy support for direct ID deletion if needed
    svr.Delete(R"(/api/forwards/(fwd_[a-z0-9_]+))", [this](const auto& req, auto& res) {
        handleDeletePortForward(req, res);
    });

    // ==========================================
    // 6. SNAPSHOTS
    // ==========================================

    svr.Get(R"(/api/vms/([^/]+)/snapshots)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleListSnapshots(req, res);
    });
    
    svr.Post(R"(/api/vms/([^/]+)/snapshots)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleCreateSnapshot(req, res);
    });
    
    svr.Post(R"(/api/vms/([^/]+)/snapshots/([^/]+)/revert)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleRevertSnapshot(req, res);
    });
    
    svr.Delete(R"(/api/vms/([^/]+)/snapshots/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleDeleteSnapshot(req, res);
    });

    // ==========================================
    // 7. NETWORKS
    // ==========================================

    svr.Get("/api/networks", [this](const auto& req, auto& res) {
        this->handleListAllNetworks(req, res);
    });
    
    svr.Get("/api/networks/mine", [this](const httplib::Request& req, httplib::Response& res) {
        auto userCtx = extractUserContext(req);
        if (!userCtx.is_auth) { res.status = 401; return; }
        json result = networkMgr->getUserNetworks(userCtx.userId);
        res.set_content(result.dump(), "application/json");
    });
    
    svr.Post("/api/networks", [this](const auto& req, auto& res) {
        this->handleCreateNetwork(req, res);
    });

    svr.Get(R"(/api/networks/([^/]+))", [this](const auto& req, auto& res) {
        this->handleGetNetwork(req, res);
    });
    
    svr.Patch(R"(/api/networks/([^/]+))", [this](const auto& req, auto& res) {
        this->handleUpdateNetwork(req, res);
    });
    
    svr.Delete(R"(/api/networks/([^/]+))", [this](const auto& req, auto& res) {
        this->handleDeleteNetwork(req, res);
    });

    // ==========================================
    // 8. PAAS (Platform as a Service)
    // ==========================================

    svr.Get("/api/paas/apps", [this](const httplib::Request& req, httplib::Response& res) {
        handleListPaaSApplications(req, res);
    });

    svr.Post("/api/paas/deploy", [this](const httplib::Request& req, httplib::Response& res) {
        this->handlePaaSDeploy(req, res);
    });

    svr.Post("/api/paas/select-host", [this](auto& req, auto& res) {
        handlePaaSSelectHost(req, res);
    });
    
    svr.Get(R"(/api/paas/apps/([^/]+)$)", [this](auto& req, auto& res) {
        handlePaaSAppDetails(req, res);
    });
    
    svr.Delete(R"(/api/paas/apps/([^/]+))", [this](auto& req, auto& res) {
        handleDeletePaaSApp(req, res);
    });

    // PaaS Actions
    svr.Post(R"(/api/paas/apps/([^/]+)/start)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleStartApp(req, res);
    });

    svr.Post(R"(/api/paas/apps/([^/]+)/stop)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleStopApp(req, res);
    });
    
    svr.Post(R"(/api/paas/apps/([^/]+)/migrate)", [this](auto& req, auto& res) {
        handlePaaSMigrate(req, res);
    });

    // PaaS Monitoring & Info
    svr.Get(R"(/api/paas/apps/([^/]+)/logs)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetAppLogs(req, res);
    });

    svr.Get(R"(/api/paas/apps/([^/]+)/stats)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetAppStats(req, res);
    });

    svr.Get(R"(/api/paas/apps/([^/]+)/access)", [this](const httplib::Request& req, httplib::Response& res) {
        // Consolidating access info logic
        auto userCtx = extractUserContext(req);
        if (!userCtx.is_auth) { res.status = 401; return; }
        std::string appId = req.matches[1];
        json result = paasOps->getApplicationAccessInfo(appId);
        res.set_content(result.dump(), "application/json");
    });

    // PaaS Network & SSL
    svr.Post(R"(/api/paas/apps/([^/]+)/proxy)", [this](const httplib::Request& req, httplib::Response& res) {
        auto userCtx = extractUserContext(req);
        if (!userCtx.is_auth) { res.status = 401; return; }
        std::string appId = req.matches[1];
        json body = json::parse(req.body);
        json result = paasOps->setupReverseProxy(appId, body["domain"]);
        res.set_content(result.dump(), "application/json");
    });

    svr.Post(R"(/api/paas/apps/([^/]+)/ssl)", [this](const httplib::Request& req, httplib::Response& res) {
        auto userCtx = extractUserContext(req);
        if (!userCtx.is_auth) { res.status = 401; return; }
        std::string appId = req.matches[1];
        json body = json::parse(req.body);
        json result = paasOps->enableSSL(appId, body["domain"], body["email"]);
        res.set_content(result.dump(), "application/json");
    });

    // Database
    svr.Post("/api/paas/database", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleProvisionDatabase(req, res);
    });

    svr.Get("/api/paas/database/credentials", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetDatabaseCredentials(req, res);
    });

    svr.Get("/api/paas/server-info", [this](auto& req, auto& res) {
        handlePaaSServerInfo(req, res);
    });

    // ==========================================
    // 9. SWARM CLUSTERS
    // ==========================================

    svr.Get("/api/swarm/clusters", [this](const httplib::Request& req, httplib::Response& res) { 
        this->handleListClusters(req, res);
    });
    
    svr.Post("/api/swarm/clusters", [this](const httplib::Request& req, httplib::Response& res) { 
        this->handleCreateCluster(req, res);
    });
    
    svr.Get(R"(/api/swarm/clusters/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) { 
        this->handleGetCluster(req, res);
    });
    
    svr.Delete(R"(/api/swarm/clusters/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) { 
        this->handleDeleteCluster(req, res);
    });

    // ==========================================
    // 10. SAAS & RESOURCES
    // ==========================================

    // Base Images & Flavors
    svr.Get("/api/flavors", [this](const httplib::Request& req, httplib::Response& res) { 
        this->handleListFlavors(req, res); 
    });

    svr.Get("/api/images", [this](const httplib::Request& req, httplib::Response& res) { 
        this->handleListImages(req, res);
    });

    // Documents (OnlyOffice/SaaS)
    svr.Post("/api/documents/deploy", [this](const httplib::Request& req, httplib::Response& res) {
        auto userCtx = extractUserContext(req);
        if (!userCtx.is_auth) { res.status = 401; return; }
        json result = paasOps->deployOnlyOffice(userCtx.userId);
        res.set_content(result.dump(), "application/json");
    });

    svr.Get("/api/documents/url", [this](const httplib::Request& req, httplib::Response& res) {
        auto userCtx = extractUserContext(req);
        if (!userCtx.is_auth) { res.status = 401; return; }
        json result = paasOps->getOnlyOfficeURL(userCtx.userId);
        res.set_content(result.dump(), "application/json");
    });

    svr.Delete("/api/documents", [this](const httplib::Request& req, httplib::Response& res) {
        auto userCtx = extractUserContext(req);
        if (!userCtx.is_auth) { res.status = 401; return; }
        bool success = paasOps->deleteOnlyOffice(userCtx.userId);
        json result = {{"success", success}};
        res.set_content(result.dump(), "application/json");
    });
    
    svr.Get("/api/saas/billing", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetSaaSBilling(req, res);
    });
}