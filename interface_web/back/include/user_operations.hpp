#ifndef USER_OPERATIONS_HPP
#define USER_OPERATIONS_HPP

#include <libvirt/libvirt.h>
#include "network_manager.hpp"
#include <mutex>
#include <unordered_map>

#include "json.hpp"
#include <string>

using json = nlohmann::json;

class UserOperations {
private:
    virConnectPtr conn;
    NetworkManager* networkManager;

    json users;
    std::string usersFile;

    struct UserSession {
        std::string username;
        std::string role;
        long long expiry;
    };
    
private:
    std::string sessionsFile;
    void loadSessions();
    bool saveSessions();


    std::unordered_map<std::string, UserSession> sessions;
    std::mutex sessions_mutex;
    
    void loadUsers();
    bool saveUsers();
    std::string hashPassword(const std::string& password);
    std::string generateToken(const std::string& username);
    void cleanupExpiredSessions();

public:
    UserOperations(virConnectPtr connection, NetworkManager *netWMgr);
    
    // User management
    json createUser(const json& userData);
    json listUsers();
    json getUser(const std::string& username);
    json updateUser(const std::string& username, const json& updates);
    json deleteUser(const std::string& username);
    json authenticate(const std::string& username, const std::string& password);
    bool validateToken(const std::string& token, std::string& username, std::string& role);

    // User Vm Management
    int listUserDomains(virDomainPtr **domains, std::string username, int flags);

    // Quota management
    json updateUserQuotas(const std::string& username, const json& quotas);
    json getUserUsage(const std::string& username);
    json getAllUsersUsage();
    json checkUserQuota(const std::string& username, const json& vmRequest);
    bool checkQuota(const std::string& username, const std::string& resource, int requestedAmount);
};

#endif // USER_OPERATIONS_HPP