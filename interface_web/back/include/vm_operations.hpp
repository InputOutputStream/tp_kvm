#ifndef VM_OPERATIONS_HPP
#define VM_OPERATIONS_HPP

#include <libvirt/libvirt.h>

#include "host_manager.hpp"
#include "network_manager.hpp"
#include "isolation_utils.hpp"
#include "network_proxy_service.hpp"

#include <libvirt/libvirt-storage.h>
#include <string>
#include <map>

#include "json.hpp"
#include "remote_executor.hpp"

#define GRACEFULL_SHUTDOWN_TIME 30 // seconds

using json = nlohmann::json;

// Cache for CPU stats
struct CPUCache {
    unsigned long long cpuTime;
    long long timestamp;
};

class ConnectionPool {
private:
    std::vector<virConnectPtr> connections;
    std::mutex poolMutex;
    
public:
    virConnectPtr acquire() {
        std::lock_guard<std::mutex> lock(poolMutex);
        if (!connections.empty()) {
            auto conn = connections.back();
            connections.pop_back();
            return conn;
        }
        return nullptr;
    }
    
    void release(virConnectPtr conn) {
        std::lock_guard<std::mutex> lock(poolMutex);
        connections.push_back(conn);
    }
};


class VMOperations {
private:
    virConnectPtr conn;
    HostManager *hostManager;
    NetworkManager *networkManager;
    ResourceMetadataStore *g_metadataStore = nullptr;
    NetworkProxyService *g_proxyService = nullptr;

    std::map<std::string, CPUCache> statsCache;
    std::mutex statsCacheMutex;  
    
    // Helper methods for deletion
    bool stopVMIfRunning(virDomainPtr domain);
    bool deleteAllSnapshots(virDomainPtr domain);
    std::vector<std::string> getDiskPaths(virDomainPtr domain);
    bool deleteDiskFiles(const std::vector<std::string>& diskPaths);
    std::string generateVNCSessionToken();
    json generateVNCToken(int vncPort, json result);
    json getIPFromDHCPLeases(virDomainPtr domain, const std::string& name);
    std::string getFirstNetworkInterface(virDomainPtr domain);

    // Resource management
    struct HostSelection {
        std::string hostname;
        virConnectPtr connection;
    };

     HostSelection selectOptimalHost(const json& vmParams);
    bool logDeploymentFailure(const json& vmParams, const json& availability);
    
    // Validation methods
    bool validateConnection();
    bool validateNetwork(const std::string& networkName, const std::string& username);
    bool validateInputParameters(const json& vmParams);
    bool validateVMNameAvailability(const std::string& hostname);
    bool validateRemoteDirectories(RemoteExec::RemoteExecutor& remoteExec);
    bool validateRemoteTools(RemoteExec::RemoteExecutor& remoteExec);
    bool validateBaseImage(RemoteExec::RemoteExecutor& remoteExec, 
                                     const std::string& baseImageId);

    bool validateDiskSpace(RemoteExec::RemoteExecutor& remoteExec, int diskGB);
    bool validateNetwork();
    bool validateXML(const std::string& xml);

    // Cloud-init methods
    struct CloudInitConfig {
        std::string metaData;
        std::string userData;
    };
    
    CloudInitConfig createCloudInitConfig(const json& vmParams);
    bool writeCloudInitFiles(RemoteExec::RemoteExecutor& remoteExec, 
                            const std::string& hostname,
                            const CloudInitConfig& config);

    bool createCloudInitISO(RemoteExec::RemoteExecutor& remoteExec,
                           const std::string& hostname);
    
    // Disk operations
    bool copyBaseImage(RemoteExec::RemoteExecutor& remoteExec,
                      const std::string& hostname, const std::string& baseImageId);

    bool resizeDisk(RemoteExec::RemoteExecutor& remoteExec,
                   const std::string& hostname, int diskGB);
    
    // VM creation
    std::string generateDomainXML(const json& vmParams);
    virDomainPtr defineVM(const std::string& xml);
    bool startVM(virDomainPtr domain);
    
    // Helper methods
    void printConfiguration(const json& vmParams);
    std::string hashPassword(RemoteExec::RemoteExecutor& remoteExec, 
                            const std::string& password);
    

public:
    explicit VMOperations(virConnectPtr connection, HostManager *hostManager, 
        NetworkManager *netMgr, ResourceMetadataStore *g_metadataStore, NetworkProxyService *g_proxyService);

    // VM listing
    json listAllVMs();
    json listUserVMs(const std::string& userId);
    
    // VM info
    json getVMInfo(const std::string& name);
    json getIP(const std::string& name);

    json getVMStats(const std::string& name);
    json getVMStatus(const std::string& name);
    
    // VM control

    bool startVM(const std::string& resourceID, const std::string& username);
    bool shutdownVM(const std::string& resourceID, const std::string& username);
    json deleteVM(const std::string& resourceID, const std::string& username, bool removeDisks);
    json getVMIP(const std::string& resourceID, const std::string& username);
    json getVNCInfo(const std::string& resourceID, const std::string& username);
    bool performVMAction(const std::string& resourceID, std::string userId, std::string action);

    // Port forwarding
    json createPortForward(const std::string& resourceID, const std::string& username,
                          int vmPort, const std::string& protocol = "tcp");
    json listPortForwards(const std::string& username);
    bool deletePortForward(const std::string& forwardID, const std::string& username);
    
    // Metadata management
    json updateVMMetadata(const std::string& resourceID, const std::string& username,
                         const json& updates);


    bool deployVM(const nlohmann::json& vmParams);
    bool destroyVM(const std::string& name);
    bool rebootVM(const std::string& name);
    bool pauseVM(const std::string& name);
    bool resumeVM(const std::string& name);    
    bool undefineVM(const std::string& name);
    bool deleteAllVMs(std::string& username);

    // VNC
    json getVMIP(const std::string& vmName);
    int findAvailablePort(int startPort, int endPort);
    bool isPortInUse(int port);
    std::string savePortForward(const std::string& vmName, const std::string& vmIP,
                                         int vmPort, int hostPort, const std::string& protocol);


    // Snapshots
    json listSnapshots(const std::string& name);
    bool createSnapshot(const std::string& name, const std::string& snapName, const std::string& desc);
    bool revertSnapshot(const std::string& name, const std::string& snapName);
    bool deleteSnapshot(const std::string& name, const std::string& snapName);
    
    // Clone
    bool cloneVM(const std::string& name, const std::string& cloneName);

private:
    // Helper functions
    json getVMStatsInternal(virDomainPtr domain, const std::string& vmName);
    std::string getStateString(int state);
};

#endif // VM_OPERATIONS_HPP