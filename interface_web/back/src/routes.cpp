#include "../include/routes.hpp"
#include "../include/utils.hpp"
#include "../include/user_operations.hpp"
#include "../include/paas_operations.hpp"
#include "../include/swarm_operations.hpp"
#include "../include/host_manager.hpp"
#include "../include/json.hpp"
#include <sstream>

using json = nlohmann::json;


// Check if user has access to VM
bool checkVMAccess(const std::string& vmName, const UserContext& userCtx) {
    if (userCtx.isAdmin) return true;
    
    VMNameManager manager;
    return manager.isOwner(vmName, userCtx.userId);
}

// Add authentication middleware
UserContext APIRoutes::extractUserContext(const httplib::Request& req) {
    UserContext ctx;
    
    std::string token = req.get_header_value("Authorization");
    if (token.empty()) {
        // Try cookie
        auto cookie = req.get_header_value("Cookie");
        if (!cookie.empty()) {
            // Extract token from cookie
            size_t pos = cookie.find("token=");
            if (pos != std::string::npos) {
                token = cookie.substr(pos + 6);
                size_t end = token.find(';');
                if (end != std::string::npos) {
                    token = token.substr(0, end);
                }
            }
        }
    }
    
    // Remove "Bearer " prefix if present
    if (token.find("Bearer ") == 0) {
        token = token.substr(7);
    }
    
    std::string username, role;
    if (userOps->validateToken(token, username, role)) {
        ctx.userId = username;
        ctx.role = role;
        ctx.isAdmin = (role == "admin");
        ctx.is_auth = true;
    }
    
    return ctx;
}

// Add authentication check wrapper
bool APIRoutes::requireAuth(const httplib::Request& req, 
                           httplib::Response& res,
                           UserContext& ctx) {
    ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        json error = {
            {"success", false},
            {"error", "Authentication required"}
        };
        res.status = 401;
        res.set_content(error.dump(), "application/json");
        return false;
    }
    
    return true;
}

bool APIRoutes::requireAdmin(const httplib::Request& req,
                            httplib::Response& res,
                            UserContext& ctx) {
    if (!requireAuth(req, res, ctx)) {
        return false;
    }
    
    if (!ctx.isAdmin) {
        json error = {
            {"success", false},
            {"error", "Administrator privileges required"}
        };
        res.status = 403;
        res.set_content(error.dump(), "application/json");
        return false;
    }
    
    return true;
}

void APIRoutes::handleListVMs(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx;
    if (!requireAuth(req, res, ctx)) return;
    
    json result;
    try {
        if (ctx.isAdmin) {
            result = vmOps->listAllVMs();
        } else {
            result = vmOps->listUserVMs(ctx.userId);
        }
        
        res.set_content(result.dump(), "application/json");
    } catch (const std::exception& e) {
        // Don't expose internal error details
        json error = {
            {"success", false},
            {"error", "Internal server error"}
        };
        res.status = 500;
        res.set_content(error.dump(), "application/json");
        
        // Log the actual error internally
        std::cerr << "Error in handleListVMs: " << e.what() << std::endl;
    }
}

bool APIRoutes::validateVMName(const std::string& name) {
    if (name.empty() || name.length() > 100) return false;
    
    // Only allow alphanumeric, hyphens, underscores
    static const std::regex valid_pattern(R"(^[a-zA-Z0-9_-]+$)");
    return std::regex_match(name, valid_pattern);
}

/**
 * @brief Send unauthorized response
 */
void sendUnauthorized(httplib::Response& res, const std::string& message = "Unauthorized") {
    json response = {
        {"success", false},
        {"error", message},
        {"code", 401}
    };
    res.set_content(response.dump(), "application/json");
    res.status = 401;
}

/**
 * @brief Send forbidden response
 */
void sendForbidden(httplib::Response& res, const std::string& message = "Access denied") {
    json response = {
        {"success", false},
        {"error", message},
        {"code", 403}
    };
    res.set_content(response.dump(), "application/json");
    res.status = 403;
}

/**
 * @brief Send error response
 */
void sendError(httplib::Response& res, const std::string& message, int code = 400) {
    json response = {
        {"success", false},
        {"error", message},
        {"code", code}
    };
    res.set_content(response.dump(), "application/json");
    res.status = code;
}


void APIRoutes::handleDeployVM(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    try {
        json vmParams = json::parse(req.body);
        
        // CRITICAL: Force username to be the authenticated user
        // Users cannot deploy VMs for other users
        vmParams["username"] = ctx.userId;
        
        // Validate required fields
        if (!vmParams.contains("vmName") || !vmParams.contains("flavor")) {
            sendError(res, "Missing required fields: vmName, flavor");
            return;
        }
        
        // Deploy VM
        bool success = vmOps->deployVM(vmParams);
        
        json result;
        if (success) {
            result["success"] = true;
            result["message"] = "VM deployment initiated";
            
            // Log the action
            Rlogger->logUserAction(ctx.userId, "deploy_vm", 
                                  vmParams["vmName"].get<std::string>(), true);
        } else {
            result["success"] = false;
            result["error"] = "VM deployment failed";
            
            Rlogger->logUserAction(ctx.userId, "deploy_vm", 
                                  vmParams["vmName"].get<std::string>(), false);
        }
        
        res.set_content(result.dump(), "application/json");
        
    } catch (const json::exception& e) {
        sendError(res, std::string("Invalid JSON: ") + e.what());
    }
}

void APIRoutes::handleStartVM(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    // Get resource ID from URL parameter
    std::string resourceID = req.matches[1];
    
    // Attempt to start VM (ownership checked in vmOps)
    bool success = vmOps->startVM(resourceID, ctx.userId);
    
    json result;
    if (success) {
        result["success"] = true;
        result["message"] = "VM started successfully";
        Rlogger->logUserAction(ctx.userId, "start_vm", resourceID, true);
    } else {
        result["success"] = false;
        result["error"] = "Failed to start VM or access denied";
        Rlogger->logUserAction(ctx.userId, "start_vm", resourceID, false);
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleShutdownVM(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    std::string resourceID = req.matches[1];
    
    bool success = vmOps->shutdownVM(resourceID, ctx.userId);
    
    json result;
    if (success) {
        result["success"] = true;
        result["message"] = "VM shutdown initiated";
        Rlogger->logUserAction(ctx.userId, "shutdown_vm", resourceID, true);
    } else {
        result["success"] = false;
        result["error"] = "Failed to shutdown VM or access denied";
        Rlogger->logUserAction(ctx.userId, "shutdown_vm", resourceID, false);
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleDeleteVM(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    std::string resourceID = req.matches[1];
    
    // Check if user wants to remove disks
    bool removeDisks = req.has_param("removeDisks") && 
                      req.get_param_value("removeDisks") == "true";
    
    json result = vmOps->deleteVM(resourceID, ctx.userId, removeDisks);
    
    if (result["success"].get<bool>()) {
        Rlogger->logUserAction(ctx.userId, "delete_vm", resourceID, true);
    } else {
        Rlogger->logUserAction(ctx.userId, "delete_vm", resourceID, false);
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleGetIP(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    std::string resourceID = req.matches[1];
    
    json result = vmOps->getVMIP(resourceID, ctx.userId);
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleGetVNC(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    std::string resourceID = req.matches[1];
    
    json result = vmOps->getVNCInfo(resourceID, ctx.userId);
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleCreatePortForward(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    try {
        json params = json::parse(req.body);
        
        std::string resourceID = req.matches[1];
        int vmPort = params["vmPort"].get<int>();
        std::string protocol = params.value("protocol", "tcp");
        
        json result = vmOps->createPortForward(resourceID, ctx.userId, vmPort, protocol);
        res.set_content(result.dump(), "application/json");
        
    } catch (const json::exception& e) {
        sendError(res, std::string("Invalid JSON: ") + e.what());
    }
}

void APIRoutes::handleListPortForwards(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    json result = vmOps->listPortForwards(ctx.userId);
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleDeletePortForward(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    std::string forwardID = req.matches[1];
    
    bool success = vmOps->deletePortForward(forwardID, ctx.userId);
    
    json result;
    if (success) {
        result["success"] = true;
        result["message"] = "Port forward deleted";
    } else {
        result["success"] = false;
        result["error"] = "Failed to delete port forward or access denied";
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleUpdateVMMetadata(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    try {
        std::string resourceID = req.matches[1];
        json updates = json::parse(req.body);
        
        // Users can only update displayName and tags
        json allowedUpdates;
        if (updates.contains("displayName")) {
            allowedUpdates["displayName"] = updates["displayName"];
        }
        if (updates.contains("tags")) {
            allowedUpdates["tags"] = updates["tags"];
        }
        
        json result = vmOps->updateVMMetadata(resourceID, ctx.userId, allowedUpdates);
        res.set_content(result.dump(), "application/json");
        
    } catch (const json::exception& e) {
        sendError(res, std::string("Invalid JSON: ") + e.what());
    }
}

void APIRoutes::handleActionVM(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    if (!ctx.is_auth) { sendUnauthorized(res); return; }

    std::string resourceID = req.matches[1]; // /api/vms/:id/action
    
    // Check ownership inside vmOps methods or here via MetadataStore
    // vmOps->performAction checks ownership now
    json body = json::parse(req.body);
    std::string action = body["action"];
    
    bool result = vmOps->performVMAction(resourceID, ctx.userId, action);
    
    if (result) {
        res.set_content("{\"success\": true}", "application/json");
    } else {
        sendError(res, "Action failed or access denied", 403);
    }
}


void APIRoutes::handleCreateNetwork(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    try {
        json params = json::parse(req.body);
        
        // CRITICAL: Force username
        std::string displayName = params.value("networkName", "default");
        
        // Create network for user
        json result = networkMgr->createUserNetwork(ctx.userId, displayName);
        res.set_content(result.dump(), "application/json");
        
        if (result["success"].get<bool>()) {
            Rlogger->logUserAction(ctx.userId, "create_network", displayName, true);
        }
        
    } catch (const json::exception& e) {
        sendError(res, std::string("Invalid JSON: ") + e.what());
    }
}

void APIRoutes::handleDeleteNetwork(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    
    if (!ctx.is_auth) {
        sendUnauthorized(res);
        return;
    }
    
    std::string networkID = req.matches[1];
    
    bool success = networkMgr->deleteUserNetwork(networkID, ctx.userId);
    
    json result;
    if (success) {
        result["success"] = true;
        result["message"] = "Network deleted";
        Rlogger->logUserAction(ctx.userId, "delete_network", networkID, true);
    } else {
        result["success"] = false;
        result["error"] = "Failed to delete network or access denied";
        Rlogger->logUserAction(ctx.userId, "delete_network", networkID, false);
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleDeleteUser(const httplib::Request& req, httplib::Response& res) {
    UserContext ctx = extractUserContext(req);
    // Only admin can delete others, users can delete themselves
    std::string targetUser = req.path_params.at("username");
    
    if (!ctx.is_auth || (!ctx.isAdmin && ctx.userId != targetUser)) {
        res.status = 403;
        res.set_content("{\"error\": \"Access denied\"}", "application/json");
        return;
    }

    vmOps->deleteAllVMs(targetUser); 
    
    json result = userOps->deleteUser(targetUser);
    
    if (result["success"].get<bool>()) {
        res.set_content(result.dump(), "application/json");
    } else {
        res.status = 400;
        res.set_content(result.dump(), "application/json");
    }
}



void APIRoutes::handleGetVMInfo(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    json result = vmOps->getVMInfo(name);
    
    if (!result["success"].get<bool>()) {
        res.status = 404;
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleGetVMStatus(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    json result = vmOps->getVMStatus(name);
    
    if (!result["success"].get<bool>()) {
        res.status = 404;
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleGetVMStats(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }

    json result = vmOps->getVMStats(name);
    
    if (!result["success"].get<bool>()) {
        res.status = 404;
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleDestroyVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    bool success = vmOps->destroyVM(name);
    
    json result = {
        {"success", success},
        {"output", success ? "Domain destroyed" : "Failed to destroy domain"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleRebootVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    bool success = vmOps->rebootVM(name);
    
    json result = {
        {"success", success},
        {"output", success ? "Domain is rebooting" : "Failed to reboot domain"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handlePauseVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    bool success = vmOps->pauseVM(name);
    
    json result = {
        {"success", success},
        {"output", success ? "Domain suspended" : "Failed to suspend domain"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleResumeVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    bool success = vmOps->resumeVM(name);
    
    json result = {
        {"success", success},
        {"output", success ? "Domain resumed" : "Failed to resume domain"}
    };
    
    res.set_content(result.dump(), "application/json");
}


void APIRoutes::handleListSnapshots(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }

    json result = vmOps->listSnapshots(name);
    
    if (!result["success"].get<bool>()) {
        res.status = 404;
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleCreateSnapshot(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }

    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        res.status = 400;
        json error = {{"success", false}, {"error", "Invalid JSON"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    if (!body.contains("snapshotName")) {
        res.status = 400;
        json error = {{"success", false}, {"error", "Snapshot name required"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    std::string snapName = body["snapshotName"];
    std::string desc = body.value("description", "Created via web interface");
    
    bool success = vmOps->createSnapshot(name, snapName, desc);
    
    json result = {
        {"success", success},
        {"output", success ? "Snapshot created" : "Failed to create snapshot"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleRevertSnapshot(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    std::string snapName = req.matches[2];
    auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    bool success = vmOps->revertSnapshot(name, snapName);
    
    json result = {
        {"success", success},
        {"output", success ? "Reverted to snapshot" : "Failed to revert snapshot"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleDeleteSnapshot(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    std::string snapName = req.matches[2];
    auto userCtx = extractUserContext(req);

    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    bool success = vmOps->deleteSnapshot(name, snapName);
    json result = {
        {"success", success},
        {"output", success ? "Snapshot deleted" : "Failed to delete snapshot"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleCloneVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        res.status = 400;
        json error = {{"success", false}, {"error", "Invalid JSON"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    if (!body.contains("cloneName")) {
        res.status = 400;
        json error = {{"success", false}, {"error", "Clone name required"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    std::string cloneName = body["cloneName"];
    bool success = vmOps->cloneVM(name, cloneName);
    
    json result = {
        {"success", success},
        {"output", success ? "VM cloned successfully" : "Failed to clone VM"}
    };
    
    if (!success) {
        res.status = 500;
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleSystemInfo(const httplib::Request& req, httplib::Response& res) {
    json result;
    result["success"] = false;
    std::string name = req.matches[1];

     auto userCtx = extractUserContext(req);
    
    if (!checkVMAccess(name, userCtx)) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    auto hosts = manager->listHosts();
    if (!hosts["hosts"].empty()) {
        std::string hostId = hosts["hosts"][0]["id"];
        virConnectPtr conn = manager->getConnection(hostId);
        
        virNodeInfo nodeInfo;
        unsigned long hvVersion, libVersion;
        
        if (virNodeGetInfo(conn, &nodeInfo) == 0 &&
            virConnectGetVersion(conn, &hvVersion) == 0 &&
            virConnectGetLibVersion(conn, &libVersion) == 0) {
            json info = {
                        {"model", std::string(nodeInfo.model)},
                        {"memory", std::to_string(nodeInfo.memory) + " KB"},
                        {"cpus", nodeInfo.cpus},
                        {"mhz", std::to_string(nodeInfo.mhz) + " MHz"},
                        {"nodes", nodeInfo.nodes},
                        {"sockets", nodeInfo.sockets},
                        {"cores", nodeInfo.cores},
                        {"threads", nodeInfo.threads},
                        {"hypervisorVersion", hvVersion},
                        {"libvirtVersion", libVersion}
                };

            std::stringstream nodeInfoStr;
            nodeInfoStr << "Model: " << nodeInfo.model << "\n";
            nodeInfoStr << "Memory: " << nodeInfo.memory << " KB\n";
            nodeInfoStr << "CPUs: " << nodeInfo.cpus << "\n";
            nodeInfoStr << "MHz: " << nodeInfo.mhz << " MHz\n";
            nodeInfoStr << "Nodes: " << nodeInfo.nodes << "\n";
            nodeInfoStr << "Sockets: " << nodeInfo.sockets << "\n";
            nodeInfoStr << "Cores: " << nodeInfo.cores << "\n";
            nodeInfoStr << "Threads: " << nodeInfo.threads << "\n";
            nodeInfoStr << "Hypervisor Version: " << hvVersion << "\n";
            nodeInfoStr << "Libvirt Version: " << libVersion;
            
            result["success"] = true;
            result["nodeInfo"] = nodeInfoStr.str();
            result["version"] = "Libvirt version: " + std::to_string(libVersion);

        }
        
        json error = {{"success", false}, {"error", "Failed to get system info"}};
    }
    
    res.set_content(result.dump(), "application/json");
}


void APIRoutes::handleListUsers(const httplib::Request& req, httplib::Response& res) {
    auto userCtx = extractUserContext(req);
    
    if (!userCtx.isAdmin) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Admin access required"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    // Get all users with their current usage
    json result = userOps->listUsers();
    res.set_content(result.dump(), "application/json");
}


void APIRoutes::handleUpdateUser(const httplib::Request& req, httplib::Response& res){

    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        res.status = 400;
        json error = {{"success", false}, {"error", "Invalid JSON"}};
        res.set_content(error.dump(), "application/json");
        return;
    }

    std::string username = req.matches[1];
    json result = userOps->updateUser(username, body);
    
    if (!result["success"].get<bool>()) {
        res.status = 400;
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleGetUserUsage(const httplib::Request& req, httplib::Response& res){
    auto userCtx = extractUserContext(req);
    
    if (!userCtx.is_auth) {
        res.status = 401;
        json error = {{"success", false}, {"error", "Authentication required"}};
        res.set_content(error.dump(), "application/json");
        return;
    }

    std::string username = req.matches[1];
    
    // Non-admin users can only view their own usage
    if (!userCtx.isAdmin && username != userCtx.userId) {
        res.status = 403;
        json error = {{"success", false}, {"error", "Access denied"}};
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    json result = userOps->getUserUsage(username);
    
    if (!result["success"].get<bool>()) {
        res.status = 400;
    }
    
    res.set_content(result.dump(), "application/json");

}