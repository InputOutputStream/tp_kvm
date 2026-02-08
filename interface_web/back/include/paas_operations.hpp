#ifndef PAAS_OPERATIONS_HPP
#define PAAS_OPERATIONS_HPP

#include "remote_executor.hpp"
#include "host_manager.hpp"
#include <libvirt/libvirt.h>
#include <mutex>
#include "json.hpp"
#include <string>

using json = nlohmann::json;

class PaaSOperations {
private:
    virConnectPtr conn;
    HostManager *hostManager;
    RemoteExec::RemoteExecutor *remoteExecutor;
    std::map<std::string, time_t> imageCache;
    std::mutex imageCacheMutex;

    bool dockerImageExists(const std::string& imageName);
    bool pullDockerImage(const std::string& imageName);
    std::string generateDockerComposeFile(const json& appConfig);
    int getNextAvailablePort();

public:
    PaaSOperations(virConnectPtr connection, RemoteExec::RemoteExecutor *remoteExec, HostManager *hostMgr);
    
    // Application deployment
    json deployApplication(const json& appConfig);
    json deployApplicationEnhanced(const json& appConfig);
    json migrateApplication(const std::string& appName, 
                       const std::string& targetHost, 
                       bool liveMigration);
    bool validateDockerImage(const std::string& imageName);

    json selectPaaSHost(const json& appConfig);

    json listApplications();
    json getApplicationStatus(const std::string& appId);
    
    // Application control
    bool stopApplication(const std::string& appId);
    bool startApplication(const std::string& appId);
    bool deleteApplication(const std::string& appId);
    
    // Application monitoring
    json getApplicationLogs(const std::string& appId, int lines = 100);
    RemoteExec::RemoteExecutor::ExecResult getAppStats(std::string appID);

    // Office cloud
    json deployOnlyOffice(const std::string& username);
    json getOnlyOfficeURL(const std::string& username);
    bool deleteOnlyOffice(const std::string& username);

    json getApplicationAccessInfo(const std::string& appId);
    std::string getHostPublicIP();
    json setupReverseProxy(const std::string& appId, const std::string& domain);
    json enableSSL(const std::string& appId, const std::string& domain, const std::string& email);


};

#endif // PAAS_OPERATIONS_HPP