#include "../include/routes.hpp"
#include "../include/json.hpp"
#include <sstream>

using json = nlohmann::json;

APIRoutes::APIRoutes(VMOperations* operations, LibvirtManager* mgr) 
    : vmOps(operations), manager(mgr) {}

void APIRoutes::setup(httplib::Server& svr) {

    // VM listing
    svr.Get("/api/vms", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleListVMs(req, res);
    });
    
    // VM info
    svr.Get(R"(/api/vms/([^/]+)$)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetVMInfo(req, res);
    });
    
    // VM status
    svr.Get(R"(/api/vms/([^/]+)/status)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetVMStatus(req, res);
    });
    
    // VM stats
    svr.Get(R"(/api/vms/([^/]+)/stats)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetVMStats(req, res);
    });

    // VM Create
    svr.Post(R"(/api/vms/deploy)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleDeployVM(req, res);
    });
    
    // VM control
    svr.Post(R"(/api/vms/([^/]+)/start)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleStartVM(req, res);
    });

    svr.Post(R"(/api/vms/([^/]+)/shutdown)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleShutdownVM(req, res);
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

    svr.Delete(R"(/api/vms/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleDeleteVM(req, res);
    });
    
    // VNC
    svr.Get(R"(/api/vms/([^/]+)/vnc)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetVNC(req, res);
    });

    // IP
    
    svr.Get(R"(/api/vms/([^/]+)/ip)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleGetIP(req, res);
    });

    
    // Snapshots
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
    
    // Clone
    svr.Post(R"(/api/vms/([^/]+)/clone)", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleCloneVM(req, res);
    });
    
    // System info
    svr.Get("/api/system/info", [this](const httplib::Request& req, httplib::Response& res) {
        this->handleSystemInfo(req, res);
    });

    }



// Routes Handler definitions
void APIRoutes::handleListVMs(const httplib::Request& req, httplib::Response& res) {
    json result = vmOps->listAllVMs();
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleGetVMInfo(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    json result = vmOps->getVMInfo(name);
    
    if (!result["success"].get<bool>()) {
        res.status = 404;
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleGetVMStatus(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    json result = vmOps->getVMStatus(name);
    
    if (!result["success"].get<bool>()) {
        res.status = 404;
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleGetVMStats(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    json result = vmOps->getVMStats(name);
    
    if (!result["success"].get<bool>()) {
        res.status = 404;
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleStartVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    bool success = vmOps->startVM(name);
    
    json result = {
        {"success", success},
        {"output", success ? "Domain started" : "Failed to start domain"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleDeleteVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    
    // Get query parameter for disk removal
    bool removeDisks = false;
    if (req.has_param("removeDisks")) {
        std::string removeDiskParam = req.get_param_value("removeDisks");
        removeDisks = (removeDiskParam == "true" || removeDiskParam == "1");
    }
    
    std::cerr << "Delete VM request: " << name 
              << " (removeDisks: " << (removeDisks ? "true" : "false") << ")" << std::endl;
    
    json result = vmOps->deleteVM(name, removeDisks);
    
    if (!result["success"].get<bool>()) {
        res.status = 500;
        
        // Check if it's a "not found" error
        if (result.contains("error")) {
            std::string error = result["error"];
            if (error.find("not found") != std::string::npos) {
                res.status = 404;
            }
        }
    }
    
    res.set_content(result.dump(2), "application/json");
}

void APIRoutes::handleDeployVM(const httplib::Request& req, httplib::Response& res) {
    std::cerr << "Deploy request received" << std::endl;
    std::cerr << "Request body: " << req.body << std::endl;
    
    // Parse JSON body
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
    
    // Validate required fields
    if (!body.contains("hostname") || !body.contains("memory") || 
        !body.contains("vcpus") || !body.contains("disk")) {
        res.status = 400;
        json error = {
            {"success", false}, 
            {"error", "Missing required fields: hostname, memory, vcpus, disk"}
        };
        res.set_content(error.dump(), "application/json");
        return;
    }
    
    // Extract VM settings
    std::string hostname = body["hostname"];
    int memory = body["memory"];
    int vcpus = body["vcpus"];
    int disk = body["disk"];
    std::string osVariant = body.value("osVariant", "ubuntu22.04");
    std::string isoPath = body.value("isoPath", "");
    
    std::cerr << "Deploying VM: " << hostname 
              << " (RAM: " << memory << "MB, vCPUs: " << vcpus 
              << ", Disk: " << disk << "GB)" << std::endl;
    
    // Call VM operations
    bool success = vmOps->deployVM(body);
    
    if (success) {
        json result = {
            {"success", true},
            {"output", "VM deployment initiated successfully"},
            {"vmName", hostname}
        };
        res.set_content(result.dump(), "application/json");
    } else {
        res.status = 500;
        json error = {
            {"success", false},
            {"error", "Failed to deploy VM"}
        };
        res.set_content(error.dump(), "application/json");
    }
}

void APIRoutes::handleShutdownVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    bool success = vmOps->shutdownVM(name);
    
    json result = {
        {"success", success},
        {"output", success ? "Domain is shutting down" : "Failed to shutdown domain"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleDestroyVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    bool success = vmOps->destroyVM(name);
    
    json result = {
        {"success", success},
        {"output", success ? "Domain destroyed" : "Failed to destroy domain"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleRebootVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    bool success = vmOps->rebootVM(name);
    
    json result = {
        {"success", success},
        {"output", success ? "Domain is rebooting" : "Failed to reboot domain"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handlePauseVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    bool success = vmOps->pauseVM(name);
    
    json result = {
        {"success", success},
        {"output", success ? "Domain suspended" : "Failed to suspend domain"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleResumeVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    bool success = vmOps->resumeVM(name);
    
    json result = {
        {"success", success},
        {"output", success ? "Domain resumed" : "Failed to resume domain"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleGetVNC(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    json result = vmOps->getVNCInfo(name);
    res.set_content(result.dump(), "application/json");
}


void APIRoutes::handleGetIP(const httplib::Request& req, httplib::Response& res)
{
    std::string name = req.matches[1];
    json result = vmOps->getIP(name);

    if (!result["success"].get<bool>()) {
        res.status = 404;
    }

    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleListSnapshots(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    json result = vmOps->listSnapshots(name);
    
    if (!result["success"].get<bool>()) {
        res.status = 404;
    }
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleCreateSnapshot(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    
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
    
    bool success = vmOps->deleteSnapshot(name, snapName);
    
    json result = {
        {"success", success},
        {"output", success ? "Snapshot deleted" : "Failed to delete snapshot"}
    };
    
    res.set_content(result.dump(), "application/json");
}

void APIRoutes::handleCloneVM(const httplib::Request& req, httplib::Response& res) {
    std::string name = req.matches[1];
    
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
    
    if (!manager->isConnected()) {
        result["error"] = "Not connected to libvirt";
        res.set_content(result.dump(), "application/json");
        return;
    }
    
    virNodeInfo nodeInfo;
    unsigned long hvVersion, libVersion;
    
    if (!manager->getNodeInfo(nodeInfo) || 
        !manager->getVersion(hvVersion) || 
        !manager->getLibVersion(libVersion)) {
        result["error"] = "Failed to get system info";
        res.set_content(result.dump(), "application/json");
        return;
    }
    
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
    
    res.set_content(result.dump(), "application/json");
}