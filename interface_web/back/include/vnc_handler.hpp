#ifndef VNC_HANDLER_HPP
#define VNC_HANDLER_HPP

#include <string>
#include <libvirt/libvirt.h>
#include "json.hpp"
#include "remote_executor.hpp"

using json = nlohmann::json;

class VNCHandler {
private:
    virConnectPtr conn;
    json config;
    
    void loadConfig();
    void setDefaultConfig();
    std::string getVNCPassword(const std::string& vmName);
    void setVNCPassword(const std::string& vmName, const std::string& password);

    int findAvailablePort(int startPort, int endPort) {
        for (int port = startPort; port <= endPort; port++) {
            if (!isPortInUse(port)) {
                return port;
            }
        }
        return -1;
    }

    bool isPortInUse(int port) {
        RemoteExec::RemoteExecutor remoteExec(conn);
        
        std::stringstream cmd;
        cmd << "netstat -tuln | grep ':" << port << " ' || ss -tuln | grep ':" << port << " '";
        
        auto result = remoteExec.execute(cmd.str());
        
        // If output is not empty, port is in use
        return !result.output.empty();
    }
    
public:
    VNCHandler(virConnectPtr connection);
    
    // Get VNC connection info for a VM
    json getVNCInfo(const std::string& vmName);
    
    // Enable VNC for a VM (modifies VM XML)
    json enableVNC(const std::string& vmName, const std::string& password = "");
    
    // Get noVNC service status
    json getNoVNCStatus();
    
    // Create individual VNC proxy for a VM
    json createVNCProxy(const std::string& vmName, int vncPort);

    std::string getHostPublicIP(virConnectPtr conn);

    int setupVNCProxy(const std::string& vmName, 
                              const std::string& targetHost, 
                              int targetPort);

    void saveProxyInfo(const std::string& vmName, int proxyPort, const std::string& target);

};

#endif // VNC_HANDLER_HPP