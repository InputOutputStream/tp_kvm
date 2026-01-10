#include <iostream>
#include "../include/httplib.h"
#include "../include/libvirt_manager.hpp"
#include "../include/vm_operations.hpp"
#include "../include/routes.hpp"
#include "../include/cors.hpp"
#include "../include/utils.hpp"
#include "../include/def.hpp"

using namespace httplib;

int main() {
    std::cout << "Starting libvirt C++ server..." << std::endl;
    
    // Initialize libvirt manager
    LibvirtManager manager;
    
    // Connect to libvirt (local or remote based on def.hpp config)
    if (!manager.connect(use_remote, remote_host, username)) {
        std::cerr << "Unable to connect to libvirt" << std::endl;
        std::cerr << "Check that libvirt is installed and active" << std::endl;
        std::cerr << "   sudo systemctl start libvirtd" << std::endl;
        return 1;
    }
    
    std::cout << "Connected to libvirt successfully" << std::endl;
    
    // Initialize VM operations
    VMOperations vmOps(manager.getConnection());
    
    // Initialize API routes
    APIRoutes apiRoutes(&vmOps, &manager);
    
    // Create HTTP server
    Server svr;
    
    // Setup CORS middleware
    cors::setupMiddleware(svr);
    
    // Setup API routes
    apiRoutes.setup(svr);
    
    // Serve static files if front directory exists
    if (fileExists("../../front")) {
        svr.set_mount_point("/", "../../front");
    }
    
    std::cout << "Server started on http://localhost:" << PORT << std::endl;
    std::cout << "API available at http://localhost:" << PORT << "/api" << std::endl;
    std::cout << "\nPress Ctrl+C to stop the server" << std::endl;
    
    // Start server
    svr.listen("0.0.0.0", PORT);
    
    // Cleanup happens automatically via destructor
    
    return 0;
}