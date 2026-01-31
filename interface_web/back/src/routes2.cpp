

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
void APIRoutes::handleListFlavors(const httplib::Request& __attribute__((unused)) req, httplib::Response& res){
    json result;
    result = flavorMgr->getAllFlavors(true);
    res.set_content(result.dump(), "application/json");
} 

// Base images  
void APIRoutes::handleListImages(const httplib::Request& __attribute__((unused)) req, httplib::Response& res){
    json result;
    result = baseImageManager->listImages();
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


void APIRoutes::handleProvisionDatabase(const httplib::Request& req, httplib::Response& res) {
    auto userCtx = getUserContext(req, userOps);
    
    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        res.status = 400;
        json error = {{"success", false}, {"error", "Invalid JSON"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    if (!body.contains("type") || !body.contains("username") || !body.contains("appname")) {
        res.status = 400;
        json error = {{"success", false}, {"error", "Missing required fields"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    std::string dbType = body["type"];
    std::string username = body["username"];
    std::string appname = body["appname"];
    
    // Verify user can only create databases for themselves (unless admin)
    if (!userCtx.isAdmin && userCtx.userId != username) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    std::string scriptCmd;
    if (dbType == "postgresql") {
        scriptCmd = "/var/lib/thoth-paas/manage-databases.sh create-pg " + username + " " + appname;
    } else if (dbType == "mariadb" || dbType == "mysql") {
        scriptCmd = "/var/lib/thoth-paas/manage-databases.sh create-mysql " + username + " " + appname;
    } else {
        res.status = 400;
        json error = {{"success", false}, {"error", "Invalid database type"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    try {
        std::string output = execCommand(scriptCmd + " 2>&1");
        
        // Parse credentials from output
        // Output format: "Database: dbname, User: username, Password: password"
        std::string dbName = username + "_" + appname;
        std::string dbUser = username + "_" + appname + "_user";
        
        // Extract password from output
        std::regex passRegex("Password: ([^\\s]+)");
        std::smatch match;
        std::string password = "unknown";
        if (std::regex_search(output, match, passRegex)) {
            password = match[1].str();
        }
        
        json result = {
            {"success", true},
            {"credentials", {
                {"host", dbType == "postgresql" ? "thoth-postgres" : "thoth-mariadb"},
                {"port", dbType == "postgresql" ? 5432 : 3306},
                {"database", dbName},
                {"user", dbUser},
                {"password", password}
            }},
            {"message", "Database created successfully"}
        };
        
        Rlogger->logSystemEvent(LogLevel::INFO, 
            "Database created: " + dbName + " for user " + username);
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        json error = {
            {"success", false}, 
            {"error", "Database creation failed: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
    }
}


// ==========================================
// BILLING API FOR OWNCLOUD
// ==========================================

void APIRoutes::handleGetSaaSBilling(const httplib::Request& req, httplib::Response& res) {
    auto userCtx = getUserContext(req, userOps);
    
    if (!userCtx.isAdmin) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Admin access required"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    // Execute billing tracker script
    std::string billingCmd = "/var/lib/thoth-saas/billing-tracker.sh calculate";
    std::string output = execCommand(billingCmd);
    
    // Read billing JSON
    std::ifstream billingFile("/var/lib/thoth-saas/billing.json");
    if (!billingFile.is_open()) {
        res.status = 500;
        json error = {{"success", false}, {"error", "Billing data not available"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    json billingData;
    billingFile >> billingData;
    billingFile.close();
    
    json result = {
        {"success", true},
        {"billing", billingData}
    };
    
    res.set_content(result.dump(), "application/json");
}


// Networks

// List all networks (admin view)
void APIRoutes::handleListAllNetworks(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);
    
    if (!ctx.is_auth) {
        res.status = 401;
        res.set_content("{\"error\": \"Unauthorized\"}", "application/json");
        return;
    }
    
    auto result = networkMgr->listAllNetworks();
    res.set_content(result.dump(), "application/json");
}

// List user's networks
void APIRoutes::handleListUserNetworks(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);
    
    if (!ctx.is_auth) {
        res.status = 401;
        res.set_content("{\"error\": \"Unauthorized\"}", "application/json");
        return;
    }
    
    // Users can only see their own networks (unless admin)
    std::string targetUser = ctx.userId;
    if (ctx.isAdmin && req.has_param("username")) {
        targetUser = req.get_param_value("username");
    }
    
    auto result = networkMgr->getUserNetworks(targetUser);
    res.set_content(result.dump(), "application/json");
}

// Create user network
void APIRoutes::handleCreateNetwork(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);
    
    if (!ctx.is_auth) {
        res.status = 401;
        res.set_content("{\"error\": \"Unauthorized\"}", "application/json");
        return;
    }
    
    try {
        auto body = json::parse(req.body);
        std::string username = body["username"];
        
        // Verify user can only create for themselves (unless admin)
        if (!ctx.isAdmin && username != ctx.userId) {
            res.status = 403;
            res.set_content("{\"error\": \"Forbidden\"}", "application/json");
            return;
        }
        
        std::string networkName = body.value("networkName", "");
        auto result = networkMgr->createUserNetwork(username, networkName);
        
        if (!result["success"].get<bool>()) {
            res.status = 400;
        }
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 400;
        json error = {{"error", "Invalid request"}, {"details", e.what()}};
        res.set_content(error.dump(), "application/json");
    }
}

// Get specific network info
void APIRoutes::handleGetNetwork(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);
    
    if (!ctx.is_auth) {
        res.status = 401;
        res.set_content("{\"error\": \"Unauthorized\"}", "application/json");
        return;
    }
    
    std::string networkId = req.matches[1];
    auto result = networkMgr->getNetworkInfo(networkId);
    
    if (!result["success"].get<bool>()) {
        res.status = 404;
        res.set_content(result.dump(), "application/json");
        return;
    }
    
    // Verify ownership (unless admin)
    if (!ctx.isAdmin) {
        std::string owner = result["network"]["owner"];
        if (owner != ctx.userId) {
            res.status = 403;
            res.set_content("{\"error\": \"Forbidden\"}", "application/json");
            return;
        }
    }
    
    res.set_content(result.dump(), "application/json");
}

// Update network (rename)
void APIRoutes::handleUpdateNetwork(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);
    
    if (!ctx.is_auth) {
        res.status = 401;
        res.set_content("{\"error\": \"Unauthorized\"}", "application/json");
        return;
    }
    
    try {
        std::string networkId = req.matches[1];
        
        // First, verify ownership
        auto networkInfo = networkMgr->getNetworkInfo(networkId);
        if (!networkInfo["success"].get<bool>()) {
            res.status = 404;
            res.set_content(networkInfo.dump(), "application/json");
            return;
        }
        
        if (!ctx.isAdmin) {
            std::string owner = networkInfo["network"]["owner"];
            if (owner != ctx.userId) {
                res.status = 403;
                res.set_content("{\"error\": \"Forbidden\"}", "application/json");
                return;
            }
        }
        
        // Perform update
        auto updates = json::parse(req.body);
        bool success = networkMgr->updateNetwork(networkId, updates);
        
        json result = {
            {"success", success},
            {"message", success ? "Network updated" : "Update failed"}
        };
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 400;
        json error = {{"error", "Invalid request"}, {"details", e.what()}};
        res.set_content(error.dump(), "application/json");
    }
}

// Delete network
void APIRoutes::handleDeleteNetwork(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);
    
    if (!ctx.is_auth) {
        res.status = 401;
        res.set_content("{\"error\": \"Unauthorized\"}", "application/json");
        return;
    }
    
    try {
        std::string networkId = req.matches[1];
        auto body = json::parse(req.body);
        std::string username = body["username"];
        
        // Verify user can only delete their own networks (unless admin)
        if (!ctx.isAdmin && username != ctx.userId) {
            res.status = 403;
            res.set_content("{\"error\": \"Forbidden\"}", "application/json");
            return;
        }
        
        bool success = networkMgr->deleteUserNetwork(networkId, username);
        
        json result = {
            {"success", success},
            {"message", success ? "Network deleted" : "Delete failed"}
        };
        
        if (!success) {
            res.status = 400;
        }
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 400;
        json error = {{"error", "Invalid request"}, {"details", e.what()}};
        res.set_content(error.dump(), "application/json");
    }
}


// GET /api/paas/database/credentials?type={mariadb|postgresql}&app={appName}&user={username}
void APIRoutes::handleGetDatabaseCredentials(const httplib::Request& req, httplib::Response& res) {
    auto dbType = req.get_param_value("type");
    auto appName = req.get_param_value("app");
    auto username = req.get_param_value("user");
    
    // Read password from stored location
    // /var/lib/thoth-paas/db-passwords/{username}_{appName}.txt
    std::string passwordFile = "/var/lib/thoth-paas/db-passwords/" + 
                                username + "_" + appName + ".txt";
    
    std::ifstream file(passwordFile);
    std::string password;
    if (file.is_open()) {
        std::getline(file, password);
        file.close();
    }
    
    json result;
    result["success"] = !password.empty();
    result["password"] = password;
    result["host"] = (dbType == "mariadb") ? "thoth-mariadb" : "thoth-postgres";
    result["port"] = (dbType == "mariadb") ? 3306 : 5432;
    result["database"] = username + "_" + appName;
    result["user"] = username + "_" + appName + "_user";
    
    res.set_content(result.dump(), "application/json");
}


// GET /api/paas/apps/{appName}/logs?lines={100}
void APIRoutes::handleGetAppLogs(const httplib::Request& req, httplib::Response& res) {
    auto appName = req.matches[1];
    auto lines = req.get_param_value("lines");
    if (lines.empty()) lines = "100";
    
    json response = paasOps->getApplicationLogs(appName, std::atoi(lines.c_str()));
    res.set_content(response.dump(), "application/json");
}


// GET /api/paas/apps/{appName}/stats
void APIRoutes::handleGetAppStats(const httplib::Request& req, httplib::Response& res) {
    auto appName = req.matches[1];
    
    // Get stats using docker stats --no-stream
   
    auto result = paasOps->getAppStats(appName);
    
    json response;
    if (result.success() && !result.output.empty()) {
        std::istringstream stream(result.output);
        std::string cpu, mem, net, block;
        
        std::getline(stream, cpu, '\t');
        std::getline(stream, mem, '\t');
        std::getline(stream, net, '\t');
        std::getline(stream, block, '\t');
        
        // Parse memory (format: "123.4MiB / 2GiB")
        size_t slashPos = mem.find('/');
        std::string memUsed = mem.substr(0, slashPos);
        
        // Parse network (format: "1.2MB / 3.4MB")
        slashPos = net.find('/');
        std::string netRx = net.substr(0, slashPos);
        std::string netTx = net.substr(slashPos + 2);
        
        // Parse block IO (format: "100MB / 200MB")
        slashPos = block.find('/');
        std::string diskRead = block.substr(0, slashPos);
        std::string diskWrite = block.substr(slashPos + 2);
        
        response["success"] = true;
        response["cpu"] = cpu;
        response["memory"] = memUsed;
        response["memoryPercent"] = "0"; // Calculate if needed
        response["networkRx"] = netRx;
        response["networkTx"] = netTx;
        response["diskRead"] = diskRead;
        response["diskWrite"] = diskWrite;
    } else {
        response["success"] = false;
        response["error"] = "Failed to get stats";
    }
    
    res.set_content(response.dump(), "application/json");
}


// POST /api/paas/apps/{appName}/stop
void APIRoutes::handleStopApp(const httplib::Request& req, httplib::Response& res) {
    auto appName = req.matches[1];
    
    bool success = paasOps->stopApplication(appName);
    
    json result;
    result["success"] = success;
    if (!success) {
        result["error"] = "Failed to stop application";
    }
    
    res.set_content(result.dump(), "application/json");
}


// POST /api/paas/apps/{appName}/start
void APIRoutes::handleStartApp(const httplib::Request& req, httplib::Response& res) {
    auto appName = req.matches[1];
    
    bool success = paasOps->startApplication(appName);
    
    json result;
    result["success"] = success;
    if (!success) {
        result["error"] = "Failed to start application";
    }
    
    res.set_content(result.dump(), "application/json");
}

// POST /api/paas/select-host
void APIRoutes::handlePaaSSelectHost(const httplib::Request& req, httplib::Response& res) {    
    UserContext ctx = getUserContext(req, userOps);

    if (!ctx.is_auth) {
        res.status = 401;
        json error = {{"success", false}, {"error", "Unauthorized"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    try {
        auto body = json::parse(req.body);
        json result = paasOps->selectPaaSHost(body);    
        
        if (!result["success"].get<bool>()) {
            res.status = 400;
        }
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 400;
        json error = {
            {"success", false}, 
            {"error", "Invalid request: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
    }
}

// POST /api/paas/deploy
void APIRoutes::handlePaaSDeploy(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);

    if (!ctx.is_auth) {
        res.status = 401;
        json error = {{"success", false}, {"error", "Unauthorized"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    try {
        auto body = json::parse(req.body);
        
        // Verify user can only deploy for themselves (unless admin)
        if (body.contains("username") && !ctx.isAdmin) {
            std::string targetUser = body["username"];
            if (targetUser != ctx.userId) {
                res.status = 403;
                json error = {{"success", false}, {"error", "Forbidden"}};
                res.set_content(error.dump(), "application/json");
                return;
            }
        }
        
        json result = paasOps->deployApplication(body);    
        
        if (!result["success"].get<bool>()) {
            res.status = 400;
        }
        
        Rlogger->logSystemEvent(LogLevel::INFO, 
            "Application deployed by user " + ctx.userId);
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 400;
        json error = {
            {"success", false}, 
            {"error", "Deployment failed: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
    }
}

// GET /api/paas/applications (list all applications)
void APIRoutes::handleListPaaSApplications(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);

    if (!ctx.is_auth) {
        res.status = 401;
        json error = {{"success", false}, {"error", "Unauthorized"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    try {
        // listApplications() doesn't take parameters - returns all apps
        json result = paasOps->listApplications();
        
        // Filter by user if not admin
        if (!ctx.isAdmin && result["success"].get<bool>()) {
            json filteredApps = json::array();
            for (const auto& app : result["applications"]) {
                // Check if app belongs to user (by name pattern: username_appname)
                std::string appName = app["name"];
                if (appName.find(ctx.userId) == 0) {
                    filteredApps.push_back(app);
                }
            }
            result["applications"] = filteredApps;
        }
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        json error = {
            {"success", false}, 
            {"error", "Failed to list applications: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
    }
}

// GET /api/paas/applications/{appId}/status
void APIRoutes::handlePaaSAppDetails(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);

    if (!ctx.is_auth) {
        res.status = 401;
        json error = {{"success", false}, {"error", "Unauthorized"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    try {
        std::string appId = req.matches[1];
        json result = paasOps->getApplicationStatus(appId);    
        
        if (!result["success"].get<bool>()) {
            res.status = 404;
        }
        
        // Add container info to response
        if (result["success"].get<bool>()) {
            result["application"] = {
                {"id", appId},
                {"name", appId},
                {"status", result["status"]},
                {"running", result["running"]},
                {"containerId", appId}
            };
        }
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        json error = {
            {"success", false}, 
            {"error", "Failed to get application status: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
    }
}

// DELETE /api/paas/applications/{appId}
void APIRoutes::handleDeletePaaSApp(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);

    if (!ctx.is_auth) {
        res.status = 401;
        json error = {{"success", false}, {"error", "Unauthorized"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    try {
        std::string appId = req.matches[1];
        
        // Verify ownership (unless admin) - check if app name starts with username
        if (!ctx.isAdmin) {
            if (appId.find(ctx.userId) != 0) {
                res.status = 403;
                json error = {{"success", false}, {"error", "Forbidden"}};
                res.set_content(error.dump(), "application/json");
                return;
            }
        }
        
        bool success = paasOps->deleteApplication(appId);
        
        json result = {
            {"success", success},
            {"message", success ? "Application deleted" : "Delete failed"}
        };
        
        if (!success) {
            res.status = 400;
            result["error"] = "Failed to delete application";
        }
        
        Rlogger->logSystemEvent(LogLevel::INFO, 
            "Application " + appId + " deleted by user " + ctx.userId);
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        json error = {
            {"success", false}, 
            {"error", "Delete failed: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
    }
}

// POST /api/paas/applications/{appName}/migrate
// NOTE: Migration functionality not implemented in PaaSOperations 
void APIRoutes::handlePaaSMigrate(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);

    if (!ctx.is_auth) {
        res.status = 401;
        json error = {{"success", false}, {"error", "Unauthorized"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    // Return not implemented for now
    res.status = 501;
    json error = {
        {"success", false}, 
        {"error", "Migration functionality not yet implemented"}
    };
    res.set_content(error.dump(), "application/json");
    
    /* TODO: Implement migration 
    try {
        std::string appName = req.matches[1];
        auto body = json::parse(req.body);
        
        if (!body.contains("targetHost")) {
            res.status = 400;
            json error = {
                {"success", false}, 
                {"error", "Missing targetHost parameter"}
            };
            res.set_content(error.dump(), "application/json");
            return;
        }
        
        std::string targetHost = body["targetHost"];
        bool liveMigration = body.value("liveMigration", false);
        
        // This method doesn't exist yet in PaaSOperations
        // json result = paasOps->migrateApplication(appName, targetHost, liveMigration);
        
        Rlogger->logSystemEvent(LogLevel::INFO, 
            "Application " + appName + " migration to " + targetHost + 
            (liveMigration ? " (live)" : " (cold)") + " by user " + ctx.userId);
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        json error = {
            {"success", false}, 
            {"error", "Migration failed: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
    }
    */
}

// GET /api/paas/server-info
void APIRoutes::handlePaaSServerInfo(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);

    if (!ctx.is_auth) {
        res.status = 401;
        json error = {{"success", false}, {"error", "Unauthorized"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    try {
        // Get server IP from connection URI
        std::string serverIP = "localhost"; // Default
        if (manager) {
            // Try to extract from host manager
            json hostsInfo = manager->listHosts();
            if (hostsInfo["success"].get<bool>() && !hostsInfo["hosts"].empty()) {
                // Use first host's URI to determine IP
                std::string uri = hostsInfo["hosts"][0]["uri"];
                // Extract IP from URI if it's remote (e.g., qemu+ssh://user@192.168.1.1/system)
                size_t atPos = uri.find('@');
                if (atPos != std::string::npos) {
                    size_t slashPos = uri.find('/', atPos);
                    serverIP = uri.substr(atPos + 1, slashPos - atPos - 1);
                }
            }
        }
        
        json result = {
            {"success", true},
            {"ip", serverIP},
            {"hosts", manager ? manager->getAllHostsStats()["hosts"] : json::array()},
            {"selectionStrategy", manager ? manager->getSelectionStrategy() : "LEAST_USED"},
            {"defaultPort", 80}
        };
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        json error = {
            {"success", false}, 
            {"error", "Failed to get server info: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
    }
}

// GET /api/hosts (for frontend to get available hosts)
void APIRoutes::handleGetAvailableHosts(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);

    if (!ctx.is_auth) {
        res.status = 401;
        json error = {{"success", false}, {"error", "Unauthorized"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    try {
        if (!manager) {
            res.status = 500;
            json error = {{"success", false}, {"error", "Host manager not available"}};
            res.set_content(error.dump(), "application/json");
            return;
        }
        
        json result = manager->listHosts();
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        json error = {
            {"success", false}, 
            {"error", "Failed to get hosts: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
    }
}

// GET /api/hosts/{hostId} (for frontend to get specific host info)
void APIRoutes::handleGetHostDetails(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);

    if (!ctx.is_auth) {
        res.status = 401;
        json error = {{"success", false}, {"error", "Unauthorized"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    try {
        std::string hostId = req.matches[1];
        
        if (!manager) {
            res.status = 500;
            json error = {{"success", false}, {"error", "Host manager not available"}};
            res.set_content(error.dump(), "application/json");
            return;
        }
        
        json result = manager->getHostStats(hostId);
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        json error = {
            {"success", false}, 
            {"error", "Failed to get host info: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
    }
}

// GET /api/hosts/available (for migration - get hosts with available resources)
void APIRoutes::handleGetHostsAvailableForMigration(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = getUserContext(req, userOps);

    if (!ctx.is_auth) {
        res.status = 401;
        json error = {{"success", false}, {"error", "Unauthorized"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    try {
        if (!manager) {
            res.status = 500;
            json error = {{"success", false}, {"error", "Host manager not available"}};
            res.set_content(error.dump(), "application/json");
            return;
        }
        
        // Get all hosts stats
        json allHosts = manager->getAllHostsStats();
        
        if (!allHosts["success"].get<bool>()) {
            res.status = 500;
            res.set_content(allHosts.dump(), "application/json");
            return;
        }
        
        // Filter to only active hosts with available resources
        json availableHosts = json::array();
        for (const auto& host : allHosts["hosts"]) {
            if (host["active"].get<bool>() && 
                host["availableMemory"].get<int>() > 512 &&  // At least 512MB
                host["availableCPUs"].get<int>() > 0) {      // At least 1 CPU
                availableHosts.push_back(host);
            }
        }
        
        json result = {
            {"success", true},
            {"hosts", availableHosts}
        };
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        json error = {
            {"success", false}, 
            {"error", "Failed to get available hosts: " + std::string(e.what())}
        };
        res.set_content(error.dump(), "application/json");
    }
}