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
    
    void loadTunnels() {
        std::ifstream file(tunnelsConfigFile);
        if (file.is_open()) {
            try {
                json data;
                file >> data;
                for (const auto& item : data["tunnels"]) {
                    TunnelInfo info;
                    info.tunnelID = item["tunnelID"];
                    info.vmInternalName = item["vmInternalName"];
                    info.vmIP = item["vmIP"];
                    info.vmPort = item["vmPort"];
                    info.localPort = item["localPort"];
                    info.publicPort = item["publicPort"];
                    info.protocol = item.value("protocol", "tcp");
                    info.tunnelPid = item.value("pid", 0);
                    info.created = item["created"];
                    info.owner = item["owner"];
                    info.active = false;  // Will be checked
                    
                    activeTunnels[info.tunnelID] = info;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error loading tunnels: " << e.what() << std::endl;
            }
            file.close();
        }
    }
    
    bool saveTunnels() {
        std::lock_guard<std::mutex> lock(tunnelsMutex);
        
        json data;
        data["tunnels"] = json::array();
        
        for (const auto& [id, info] : activeTunnels) {
            if (info.active) {  // Only save active tunnels
                data["tunnels"].push_back({
                    {"tunnelID", info.tunnelID},
                    {"vmInternalName", info.vmInternalName},
                    {"vmIP", info.vmIP},
                    {"vmPort", info.vmPort},
                    {"localPort", info.localPort},
                    {"publicPort", info.publicPort},
                    {"protocol", info.protocol},
                    {"pid", info.tunnelPid},
                    {"created", info.created},
                    {"owner", info.owner}
                });
            }
        }
        
        std::ofstream file(tunnelsConfigFile);
        if (!file.is_open()) return false;
        
        file << data.dump(2);
        file.close();
        return true;
    }
    
    int findAvailablePort(int start = 10000, int end = 60000) {
        for (int port = nextAvailablePort; port < end; ++port) {
            if (!isPortInUse(port)) {
                nextAvailablePort = port + 1;
                return port;
            }
        }
        nextAvailablePort = start;
        return -1;
    }
    
    bool isPortInUse(int port) {
        // Check on libvirt host
        if (!remoteExec) return false;
        
        std::string cmd = "netstat -tuln | grep ':" + std::to_string(port) + " ' || "
                         "ss -tuln | grep ':" + std::to_string(port) + " '";
        auto result = remoteExec->execute(cmd);
        return !result.output.empty();
    }
    
public:
    NetworkProxyService(RemoteExec::RemoteExecutor* executor,
                    const std::string& hostAddress,
                    const std::string& configFile = "/var/lib/thoth-cloud/tunnels.json")
    : tunnelsConfigFile(configFile),
      remoteExec(executor),
      libvirtHost(hostAddress),
      nextAvailablePort(10000)
    {
        loadTunnels();
        cleanupStaleTunnels();
    }

    
    ~NetworkProxyService() {
        saveTunnels();
    }
    
    /**
     * @brief Get VM's IP address through libvirt DHCP leases
     * This solves the problem of VMs being on isolated networks
     */
    json getVMIP(const std::string& vmInternalName, const std::string& networkName) {
        json result;
        result["success"] = false;
        
        if (!remoteExec) {
            result["error"] = "No remote executor";
            return result;
        }
        
        // Method 1: Check DHCP leases file
        std::string cmd = "virsh net-dhcp-leases " + networkName + 
                         " | grep '" + vmInternalName + "' | awk '{print $5}' | cut -d'/' -f1";
        
        auto execResult = remoteExec->execute(cmd);
        if (execResult.success() && !execResult.output.empty()) {
            std::string ip = execResult.output;
            // Remove newlines
            ip.erase(std::remove(ip.begin(), ip.end(), '\n'), ip.end());
            ip.erase(std::remove(ip.begin(), ip.end(), '\r'), ip.end());
            
            if (!ip.empty()) {
                result["success"] = true;
                result["ip"] = ip;
                result["method"] = "dhcp-leases";
                return result;
            }
        }
        
        // Method 2: Use virsh domifaddr
        cmd = "virsh domifaddr " + vmInternalName + 
              " | grep -oP '\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}' | head -1";
        
        execResult = remoteExec->execute(cmd);
        if (execResult.success() && !execResult.output.empty()) {
            std::string ip = execResult.output;
            ip.erase(std::remove(ip.begin(), ip.end(), '\n'), ip.end());
            ip.erase(std::remove(ip.begin(), ip.end(), '\r'), ip.end());
            
            if (!ip.empty()) {
                result["success"] = true;
                result["ip"] = ip;
                result["method"] = "domifaddr";
                return result;
            }
        }
        
        // Method 3: Parse dumpxml for MAC, then check arp
        cmd = "virsh dumpxml " + vmInternalName + 
              " | grep \"mac address\" | sed \"s/.*'\\([^']*\\)'.*/\\1/\"";
        
        execResult = remoteExec->execute(cmd);
        if (execResult.success() && !execResult.output.empty()) {
            std::string mac = execResult.output;
            mac.erase(std::remove(mac.begin(), mac.end(), '\n'), mac.end());
            
            // Check ARP table
            std::string arpCmd = "arp -an | grep '" + mac + "' | "
                                "grep -oP '\\(\\K[^)]+' | head -1";
            auto arpResult = remoteExec->execute(arpCmd);
            
            if (arpResult.success() && !arpResult.output.empty()) {
                std::string ip = arpResult.output;
                ip.erase(std::remove(ip.begin(), ip.end(), '\n'), ip.end());
                
                result["success"] = true;
                result["ip"] = ip;
                result["method"] = "arp";
                return result;
            }
        }
        
        result["error"] = "Could not determine VM IP address";
        return result;
    }
    
    /**
     * @brief Create SSH tunnel for VNC access
     * 
     * Creates a tunnel chain:
     * Dashboard -> Libvirt Host (publicPort) -> VM (vmPort)
     */
    json createVNCTunnel(const std::string& vmInternalName,
                         const std::string& vmIP,
                         int vncPort,
                         const std::string& owner) {
        json result;
        result["success"] = false;
        
        std::lock_guard<std::mutex> lock(tunnelsMutex);
        
        // Find available ports
        int localPort = findAvailablePort();   // Port on libvirt host
        int publicPort = findAvailablePort();  // Port exposed to dashboard
        
        if (localPort == -1 || publicPort == -1) {
            result["error"] = "No available ports";
            return result;
        }
        
        // Create tunnel on libvirt host: local port -> VM:vncPort
        std::stringstream tunnelCmd;
        tunnelCmd << "nohup socat TCP-LISTEN:" << localPort 
                  << ",fork,reuseaddr TCP:" << vmIP << ":" << vncPort 
                  << " > /dev/null 2>&1 & echo $!";
        
        auto execResult = remoteExec->execute(tunnelCmd.str());
        if (!execResult.success()) {
            result["error"] = "Failed to create tunnel on host";
            return result;
        }
        
        pid_t pid = 0;
        try {
            std::string pidStr = execResult.output;
            pidStr.erase(std::remove(pidStr.begin(), pidStr.end(), '\n'), pidStr.end());
            pid = std::stoi(pidStr);
        } catch (...) {
            result["error"] = "Failed to get tunnel PID";
            return result;
        }
        
        // Generate tunnel ID
        std::string tunnelID = "vnc_" + vmInternalName + "_" + std::to_string(time(nullptr));
        
        // Store tunnel info
        TunnelInfo info;
        info.tunnelID = tunnelID;
        info.vmInternalName = vmInternalName;
        info.vmIP = vmIP;
        info.vmPort = vncPort;
        info.localPort = localPort;
        info.publicPort = publicPort;
        info.protocol = "tcp";
        info.tunnelPid = pid;
        info.created = time(nullptr);
        info.owner = owner;
        info.active = true;
        
        activeTunnels[tunnelID] = info;
        saveTunnels();
        
        result["success"] = true;
        result["tunnelID"] = tunnelID;
        result["vncURL"] = "vnc://" + libvirtHost + ":" + std::to_string(localPort);
        result["novncURL"] = "http://" + libvirtHost + ":6080/vnc.html?host=" + 
                            libvirtHost + "&port=" + std::to_string(localPort);
        result["localPort"] = localPort;
        result["publicPort"] = publicPort;
        
        return result;
    }
    
    /**
     * @brief Create port forward for application access
     */
    json createPortForward(const std::string& vmInternalName,
                          const std::string& vmIP,
                          int vmPort,
                          const std::string& protocol,
                          const std::string& owner) {
        json result;
        result["success"] = false;
        
        std::lock_guard<std::mutex> lock(tunnelsMutex);
        
        int localPort = findAvailablePort();
        if (localPort == -1) {
            result["error"] = "No available ports";
            return result;
        }
        
        // Use socat for TCP, or iptables for more complex forwarding
        std::stringstream cmd;
        if (protocol == "tcp") {
            cmd << "nohup socat TCP-LISTEN:" << localPort 
                << ",fork,reuseaddr TCP:" << vmIP << ":" << vmPort 
                << " > /dev/null 2>&1 & echo $!";
        } else if (protocol == "udp") {
            cmd << "nohup socat UDP-LISTEN:" << localPort 
                << ",fork,reuseaddr UDP:" << vmIP << ":" << vmPort 
                << " > /dev/null 2>&1 & echo $!";
        } else {
            result["error"] = "Unsupported protocol";
            return result;
        }
        
        auto execResult = remoteExec->execute(cmd.str());
        if (!execResult.success()) {
            result["error"] = "Failed to create port forward";
            return result;
        }
        
        pid_t pid = 0;
        try {
            std::string pidStr = execResult.output;
            pidStr.erase(std::remove(pidStr.begin(), pidStr.end(), '\n'), pidStr.end());
            pid = std::stoi(pidStr);
        } catch (...) {
            result["error"] = "Failed to get forward PID";
            return result;
        }
        
        std::string forwardID = "fwd_" + vmInternalName + "_" + 
                               std::to_string(vmPort) + "_" + 
                               std::to_string(time(nullptr));
        
        TunnelInfo info;
        info.tunnelID = forwardID;
        info.vmInternalName = vmInternalName;
        info.vmIP = vmIP;
        info.vmPort = vmPort;
        info.localPort = localPort;
        info.publicPort = localPort;  // Same for port forwards
        info.protocol = protocol;
        info.tunnelPid = pid;
        info.created = time(nullptr);
        info.owner = owner;
        info.active = true;
        
        activeTunnels[forwardID] = info;
        saveTunnels();
        
        result["success"] = true;
        result["forwardID"] = forwardID;
        result["accessURL"] = libvirtHost + ":" + std::to_string(localPort);
        result["localPort"] = localPort;
        
        return result;
    }
    
    /**
     * @brief List all tunnels for a user
     */
    json listTunnels(const std::string& owner) {
        std::lock_guard<std::mutex> lock(tunnelsMutex);
        
        json result = json::array();
        for (const auto& [id, info] : activeTunnels) {
            if (info.owner == owner && info.active) {
                result.push_back({
                    {"tunnelID", info.tunnelID},
                    {"vmName", info.vmInternalName},
                    {"vmIP", info.vmIP},
                    {"vmPort", info.vmPort},
                    {"localPort", info.localPort},
                    {"accessURL", libvirtHost + ":" + std::to_string(info.localPort)},
                    {"protocol", info.protocol},
                    {"created", info.created}
                });
            }
        }
        return result;
    }
    
    /**
     * @brief Delete tunnel
     */
    bool deleteTunnel(const std::string& tunnelID, const std::string& owner) {
        std::lock_guard<std::mutex> lock(tunnelsMutex);
        
        auto it = activeTunnels.find(tunnelID);
        if (it == activeTunnels.end() || it->second.owner != owner) {
            return false;
        }
        
        // Kill the process
        if (it->second.tunnelPid > 0) {
            std::string killCmd = "kill " + std::to_string(it->second.tunnelPid);
            remoteExec->execute(killCmd);
        }
        
        activeTunnels.erase(it);
        saveTunnels();
        return true;
    }
    
    /**
     * @brief Cleanup stale tunnels (processes that no longer exist)
     */
    void cleanupStaleTunnels() {
        std::lock_guard<std::mutex> lock(tunnelsMutex);
        
        for (auto it = activeTunnels.begin(); it != activeTunnels.end();) {
            if (it->second.tunnelPid > 0) {
                std::string checkCmd = "ps -p " + std::to_string(it->second.tunnelPid) + 
                                      " > /dev/null 2>&1; echo $?";
                auto result = remoteExec->execute(checkCmd);
                
                if (result.output.find("1") != std::string::npos) {
                    // Process doesn't exist
                    it = activeTunnels.erase(it);
                    continue;
                }
            }
            ++it;
        }
        saveTunnels();
    }
    
    /**
     * @brief Cleanup old tunnels (older than specified time)
     */
    void cleanupOldTunnels(int maxAgeSeconds = 3600) {
        std::lock_guard<std::mutex> lock(tunnelsMutex);
        
        time_t now = time(nullptr);
        for (auto it = activeTunnels.begin(); it != activeTunnels.end();) {
            if (now - it->second.created > maxAgeSeconds) {
                if (it->second.tunnelPid > 0) {
                    std::string killCmd = "kill " + std::to_string(it->second.tunnelPid);
                    remoteExec->execute(killCmd);
                }
                it = activeTunnels.erase(it);
            } else {
                ++it;
            }
        }
        saveTunnels();
    }
};

#endif // NETWORK_PROXY_SERVICE_HPP
