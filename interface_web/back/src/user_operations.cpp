#include "../include/user_operations.hpp"
#include "../include/utils.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>

UserOperations::UserOperations(virConnectPtr connection) 
    : conn(connection), usersFile("/var/lib/thoth-cloud/users.json") {
    loadUsers();
}

void UserOperations::loadUsers() {
    std::ifstream file(usersFile);
    if (file.is_open()) {
        try {
            file >> users;
        } catch (const std::exception& e) {
            std::cerr << "Error loading users: " << e.what() << std::endl;
            users = json::array();
        }
        file.close();
    } else {
        users = json::array();
    }
}

bool UserOperations::saveUsers() {
    // Create directory if it doesn't exist
    std::string dir = "/var/lib/thoth-cloud";
    mkdir(dir.c_str(), 0755);
    
    std::ofstream file(usersFile);
    if (!file.is_open()) {
        std::cerr << "Failed to open users file for writing" << std::endl;
        return false;
    }
    
    try {
        file << users.dump(2);
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving users: " << e.what() << std::endl;
        return false;
    }
}

json UserOperations::createUser(const json& userData) {
    json result;
    result["success"] = false;
    
    if (!userData.contains("username")) {
        result["error"] = "Username is required";
        return result;
    }
    
    std::string username = userData["username"];
    
    // Check if user already exists
    for (const auto& user : users) {
        if (user["username"] == username) {
            result["error"] = "User already exists";
            return result;
        }
    }
    
    // Create new user
    json newUser = {
        {"id", users.size() + 1},
        {"username", username},
        {"role", userData.value("role", "user")},
        {"email", userData.value("email", "")},
        {"quotas", {
            {"maxVMs", userData.value("maxVMs", 5)},
            {"maxCPU", userData.value("maxCPU", 8)},
            {"maxRAM", userData.value("maxRAM", 16)},
            {"maxStorage", userData.value("maxStorage", 100)}
        }},
        {"usage", {
            {"vms", 0},
            {"cpu", 0},
            {"ram", 0},
            {"storage", 0}
        }},
        {"created", getCurrentTimeMs()},
        {"active", true}
    };
    
    users.push_back(newUser);
    
    if (saveUsers()) {
        result["success"] = true;
        result["user"] = newUser;
    } else {
        result["error"] = "Failed to save user";
    }
    
    return result;
}

json UserOperations::listUsers() {
    json result;
    result["success"] = true;
    result["users"] = users;
    return result;
}

json UserOperations::getUser(const std::string& username) {
    json result;
    result["success"] = false;
    
    for (const auto& user : users) {
        if (user["username"] == username) {
            result["success"] = true;
            result["user"] = user;
            return result;
        }
    }
    
    result["error"] = "User not found";
    return result;
}

json UserOperations::updateUser(const std::string& username, const json& updates) {
    json result;
    result["success"] = false;
    
    for (auto& user : users) {
        if (user["username"] == username) {
            // Update fields
            if (updates.contains("role")) {
                user["role"] = updates["role"];
            }
            if (updates.contains("email")) {
                user["email"] = updates["email"];
            }
            if (updates.contains("active")) {
                user["active"] = updates["active"];
            }
            
            // Update quotas
            if (updates.contains("quotas")) {
                for (auto& [key, value] : updates["quotas"].items()) {
                    user["quotas"][key] = value;
                }
            }
            
            if (saveUsers()) {
                result["success"] = true;
                result["user"] = user;
            } else {
                result["error"] = "Failed to save changes";
            }
            
            return result;
        }
    }
    
    result["error"] = "User not found";
    return result;
}

json UserOperations::deleteUser(const std::string& username) {
    json result;
    result["success"] = false;
    
    for (size_t i = 0; i < users.size(); i++) {
        if (users[i]["username"] == username) {
            users.erase(users.begin() + i);
            
            if (saveUsers()) {
                result["success"] = true;
                result["message"] = "User deleted successfully";
            } else {
                result["error"] = "Failed to save changes";
            }
            
            return result;
        }
    }
    
    result["error"] = "User not found";
    return result;
}

json UserOperations::updateUserQuotas(const std::string& username, const json& quotas) {
    json updates = {{"quotas", quotas}};
    return updateUser(username, updates);
}

json UserOperations::getUserUsage(const std::string& username) {
    json result;
    result["success"] = false;
    
    // Find user
    json* userPtr = nullptr;
    for (auto& user : users) {
        if (user["username"] == username) {
            userPtr = &user;
            break;
        }
    }
    
    if (!userPtr) {
        result["error"] = "User not found";
        return result;
    }
    
    // Calculate actual usage from VMs
    int vmCount = 0;
    int totalCPU = 0;
    int totalRAM = 0;
    long long totalStorage = 0;
    
    if (conn) {
        virDomainPtr* domains;
        int numDomains = virConnectListAllDomains(conn, &domains, 0);
        
        if (numDomains >= 0) {
            for (int i = 0; i < numDomains; i++) {
                // In a real implementation, you'd check VM ownership
                // For now, we'll just count all VMs
                virDomainInfo info;
                if (virDomainGetInfo(domains[i], &info) == 0) {
                    vmCount++;
                    totalCPU += info.nrVirtCpu;
                    totalRAM += info.memory / 1024; // Convert to MB
                    
                    // Get disk usage (simplified)
                    totalStorage += 10; // Placeholder: 10 GB per VM
                }
                virDomainFree(domains[i]);
            }
            free(domains);
        }
    }
    
    // Update user usage
    (*userPtr)["usage"] = {
        {"vms", vmCount},
        {"cpu", totalCPU},
        {"ram", totalRAM},
        {"storage", totalStorage}
    };
    
    saveUsers();
    
    result["success"] = true;
    result["usage"] = (*userPtr)["usage"];
    result["quotas"] = (*userPtr)["quotas"];
    
    // Calculate percentage used
    json percentages = {
        {"vms", (vmCount * 100.0) / (*userPtr)["quotas"]["maxVMs"].get<int>()},
        {"cpu", (totalCPU * 100.0) / (*userPtr)["quotas"]["maxCPU"].get<int>()},
        {"ram", (totalRAM * 100.0) / (*userPtr)["quotas"]["maxRAM"].get<int>()},
        {"storage", (totalStorage * 100.0) / (*userPtr)["quotas"]["maxStorage"].get<int>()}
    };
    
    result["percentages"] = percentages;
    
    return result;
}

json UserOperations::getAllUsersUsage() {
    json result;
    result["success"] = true;
    result["users"] = json::array();
    
    for (const auto& user : users) {
        std::string username = user["username"];
        json userUsage = getUserUsage(username);
        
        if (userUsage["success"].get<bool>()) {
            json userData = {
                {"username", username},
                {"role", user["role"]},
                {"usage", userUsage["usage"]},
                {"quotas", userUsage["quotas"]},
                {"percentages", userUsage["percentages"]}
            };
            result["users"].push_back(userData);
        }
    }
    
    return result;
}

bool UserOperations::checkQuota(const std::string& username, 
                               const std::string& resource, 
                               int requestedAmount) {
    for (const auto& user : users) {
        if (user["username"] == username) {
            int currentUsage = user["usage"][resource].get<int>();
            int maxQuota = user["quotas"]["max" + resource].get<int>();
            
            return (currentUsage + requestedAmount) <= maxQuota;
        }
    }
    
    return false;
}