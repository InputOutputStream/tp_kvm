#include "../include/user_operations.hpp"
#include "../include/utils.hpp"
#include <openssl/sha.h>
#include <iomanip>


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

// Hash password with SHA-256
std::string UserOperations::hashPassword(const std::string& password) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)password.c_str(), password.length(), hash);
    
    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

json UserOperations::authenticate(const std::string& username, const std::string& password) {
    json result;
    result["success"] = false;
    
    for (const auto& user : users) {
        if (user["username"] == username) {
            std::string storedHash = user.value("passwordHash", "");
            std::string inputHash = hashPassword(password);
            
            if (storedHash == inputHash && user["active"].get<bool>()) {
                result["success"] = true;
                result["token"] = generateToken(username);
                result["user"] = {
                    {"username", user["username"]},
                    {"role", user["role"]},
                    {"email", user["email"]}
                };
                return result;
            } else {
                result["error"] = "Invalid password";
                return result;
            }
        }
    }
    
    result["error"] = "User not found";
    return result;
}

std::string UserOperations::generateToken(const std::string& username) {
    // Simple token: username + timestamp + hash
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::string tokenData = username + std::to_string(now);
    return hashPassword(tokenData);
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
    
    if (!userData.contains("username") || !userData.contains("password")) {
        result["error"] = "Username and password are required";
        return result;
    }
    
    std::string username = userData["username"];
    
    // Check if user exists
    for (const auto& user : users) {
        if (user["username"] == username) {
            result["error"] = "User already exists";
            return result;
        }
    }
    
    // Hash password
    std::string passwordHash = hashPassword(userData["password"].get<std::string>());
    
    json newUser = {
        {"id", users.size() + 1},
        {"username", username},
        {"passwordHash", passwordHash},
        {"role", userData.value("role", "user")},
        {"email", userData.value("email", "")},
        {"firstName", userData.value("firstName", "")},
        {"lastName", userData.value("lastName", "")},
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
        result["user"].erase("passwordHash"); // Don't send hash
    } else {
        result["error"] = "Failed to save user";
    }
    
    return result;
}

json UserOperations::checkUserQuota(const std::string& username, const json& vmRequest) {
    json result;
    result["allowed"] = false;
    
    auto userResult = getUser(username);
    if (!userResult["success"].get<bool>()) {
        result["error"] = "User not found";
        return result;
    }
    
    json user = userResult["user"];
    auto usage = getUserUsage(username);
    
    if (!usage["success"].get<bool>()) {
        result["error"] = "Could not get usage";
        return result;
    }
    
    // Extract requested resources
    int requestedVCPU = vmRequest["vcpus"].get<int>();
    int requestedRAM = vmRequest["memory"].get<int>();
    int requestedDisk = vmRequest["disk"].get<int>();
    
    // Current usage
    int currentVMs = usage["usage"]["vms"].get<int>();
    int currentCPU = usage["usage"]["cpu"].get<int>();
    int currentRAM = usage["usage"]["ram"].get<int>();
    long long currentStorage = usage["usage"]["storage"].get<long long>();
    
    // Quotas
    int maxVMs = user["quotas"]["maxVMs"].get<int>();
    int maxCPU = user["quotas"]["maxCPU"].get<int>();
    int maxRAM = user["quotas"]["maxRAM"].get<int>();
    long long maxStorage = user["quotas"]["maxStorage"].get<long long>() * 1024LL * 1024LL * 1024LL; // GB to bytes
    
    // Check each quota
    if (currentVMs + 1 > maxVMs) {
        result["error"] = "VM quota exceeded";
        result["details"] = {
            {"current", currentVMs},
            {"requested", 1},
            {"max", maxVMs},
            {"resource", "VMs"}
        };
        return result;
    }
    
    if (currentCPU + requestedVCPU > maxCPU) {
        result["error"] = "CPU quota exceeded";
        result["details"] = {
            {"current", currentCPU},
            {"requested", requestedVCPU},
            {"max", maxCPU},
            {"resource", "vCPUs"}
        };
        return result;
    }
    
    if (currentRAM + requestedRAM > maxRAM) {
        result["error"] = "RAM quota exceeded";
        result["details"] = {
            {"current", currentRAM},
            {"requested", requestedRAM},
            {"max", maxRAM},
            {"resource", "Memory (MB)"}
        };
        return result;
    }
    
    long long requestedStorageBytes = (long long)requestedDisk * 1024LL * 1024LL * 1024LL;
    if (currentStorage + requestedStorageBytes > maxStorage) {
        result["error"] = "Storage quota exceeded";
        result["details"] = {
            {"current", currentStorage / (1024*1024*1024)},
            {"requested", requestedDisk},
            {"max", maxStorage / (1024*1024*1024)},
            {"resource", "Storage (GB)"}
        };
        return result;
    }
    
    result["allowed"] = true;
    result["remaining"] = {
        {"vms", maxVMs - currentVMs - 1},
        {"cpu", maxCPU - currentCPU - requestedVCPU},
        {"ram", maxRAM - currentRAM - requestedRAM},
        {"storage", (maxStorage - currentStorage - requestedStorageBytes) / (1024*1024*1024)}
    };
    
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

int UserOperations::listUserDomains(virDomainPtr **domains, 
                                    std::string username, 
                                    int flags) {
    virDomainPtr* allDomains = nullptr;

    int numDomains = 0;
    int numUserDomains = 0;
    
    // Get all domains
    numDomains = virConnectListAllDomains(conn, &allDomains, flags);
    if (numDomains < 0) {
        std::cerr << "Failed to list domains" << std::endl;
        return -1;
    }
    
    // Filter by user
    VMNameManager::filterUserVMs(allDomains, username, 
                                 domains, numDomains, 
                                 &numUserDomains);
    
    // Free the original array (filterUserVMs creates a new one)
    if (allDomains) {
        for (int i = 0; i < numDomains; i++) {
            virDomainFree(allDomains[i]);
        }
        free(allDomains);
    }
    
    return numUserDomains;
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
        int numDomains = listUserDomains(&domains, username, 0);
        
        if (numDomains >= 0) {
            for (int i = 0; i < numDomains; i++) {
                virDomainInfo info;
                virDomainBlockInfo block_info;
                unsigned long long totalSize = 0;

                if (virDomainGetInfo(domains[i], &info) == 0) {
                          
                    vmCount++;
                    totalCPU += info.nrVirtCpu;
                    totalRAM += info.memory / 1024; // Convert to MB
                    
                    if (virDomainGetBlockInfo(domains[i], "vda", &block_info, 0) == 0) {
                        totalSize = block_info.capacity;  // Taille virtuelle
                        // info.allocation - espace réellement utilisé
                        // info.physical - taille physique sur l'hôte
                    }else if(virDomainGetBlockInfo(domains[i], "qcow", &block_info, 0) == 0)
                    {
                        totalSize = block_info.capacity;  // Taille virtuelle
                    }else if(virDomainGetBlockInfo(domains[i], "hda", &block_info, 0) == 0)
                    {
                        totalSize = block_info.capacity;  // Taille virtuelle
                    }
                        
                    totalStorage += totalSize; 
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