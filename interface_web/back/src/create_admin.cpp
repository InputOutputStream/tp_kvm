
#include "../include/user_operations.hpp"
#include "../include/libvirt_manager.hpp"
#include <iostream>

// Standalone script for admin user creation
int main() {
    LibvirtManager manager;
    manager.connect();
    
    UserOperations userOps(manager.getConnection());
    
    json adminData = {
        {"username", "admin"},
        {"password", "Aadmin123"},  // Admin Password
        {"email", "admin@admin.com"},
        {"firstName", "admin"},
        {"lastName", "admin"},
        {"role", "admin"}
    };
    
    json result = userOps.createUser(adminData);
    
    if (result["success"].get<bool>()) {
        std::cout << "✅ Admin user created successfully" << std::endl;
    } else {
        std::cerr << "❌ Failed: " << result["error"] << std::endl;
    }
    
    return 0;
}