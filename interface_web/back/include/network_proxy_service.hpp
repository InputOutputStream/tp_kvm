#ifndef NETWORK_PROXY_SERVICE_HPP
#define NETWORK_PROXY_SERVICE_HPP

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <fstream>
#include <algorithm>
#include <ctime>

#include "json.hpp"
#include "remote_executor.hpp"

using json = nlohmann::json;

/**
 * @brief Manages network tunnels and proxies for VM access
 * 
 * Provides:
 * 1. SSH tunnels for VNC access
 * 2. Port forwarding for applications
 * 3. SOCKS proxy for general access
 * 4. IP resolution through libvirt host
 */

class NetworkProxyService {
private:
    struct TunnelInfo {
        std::string tunnelID;
        std::string vmInternalName;
        std::string vmIP;           // VM's private IP
        int vmPort;                 // Port on VM
        int localPort;              // Port on libvirt host
        int publicPort;             // Port exposed to dashboard
        std::string protocol;       // tcp/udp
        pid_t tunnelPid;
        time_t created;
        std::string owner;
        bool active;
    };
    
    std::mutex tunnelsMutex;
    std::map<std::string, TunnelInfo> activeTunnels;
    std::string tunnelsConfigFile;
    RemoteExec::RemoteExecutor* remoteExec;
    std::string libvirtHost;
    int nextAvailablePort;
    
    void loadTunnels();
    
    bool saveTunnels();
    
    int findAvailablePort(int start = 10000, int end = 60000);
    
    bool isPortInUse(int port);
    
public:
    NetworkProxyService(RemoteExec::RemoteExecutor* executor,
                    const std::string& hostAddress,
                    const std::string& configFile = "/var/lib/thoth-cloud/tunnels.json");

    
    ~NetworkProxyService() {
        saveTunnels();
    }
    
    /**
     * @brief Get VM's IP address through libvirt DHCP leases
     * This solves the problem of VMs being on isolated networks
     */
    json getVMIP(const std::string& vmInternalName, const std::string& networkName) ;
    /**
     * @brief Create SSH tunnel for VNC access
     * 
     * Creates a tunnel chain:
     * Dashboard -> Libvirt Host (publicPort) -> VM (vmPort)
     */
    json createVNCTunnel(const std::string& vmInternalName,
                         const std::string& vmIP,
                         int vncPort,
                         const std::string& owner);
    /**
     * @brief Create port forward for application access
     */
    json createPortForward(const std::string& vmInternalName,
                          const std::string& vmIP,
                          int vmPort,
                          const std::string& protocol,
                          const std::string& owner);
    /**
     * @brief List all tunnels for a user
     */
    json listTunnels(const std::string& owner);
    
    /**
     * @brief Delete tunnel
     */
    bool deleteTunnel(const std::string& tunnelID, const std::string& owner);
    
    /**
     * @brief Cleanup stale tunnels (processes that no longer exist)
     */
    void cleanupStaleTunnels();
    
    /**
     * @brief Cleanup old tunnels (older than specified time)
     */
    void cleanupOldTunnels(int maxAgeSeconds = 3600);
};

#endif // NETWORK_PROXY_SERVICE_HPP
