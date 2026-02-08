#include "../include/vnc_handler.hpp"
#include "../include/utils.hpp"
#include "../include/remote_executor.hpp"
#include <sstream>
#include <regex>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <map>

VNCHandler::VNCHandler(virConnectPtr connection) : conn(connection) {
    // Load VNC configuration
    loadConfig();
}

void VNCHandler::loadConfig() {
    std::ifstream configFile("/etc/thoth-cloud/vnc.json");
    if (configFile.is_open()) {
        try {
            configFile >> config;
            configFile.close();
        } catch (const std::exception& e) {
            std::cerr << "Error loading VNC config: " << e.what() << std::endl;
            // Set defaults
            setDefaultConfig();
        }
    } else {
        setDefaultConfig();
    }
}

void VNCHandler::setDefaultConfig() {
    config = {
        {"novncHost", "localhost"},
        {"novncPort", 6080},
        {"vncBasePort", 5900},
        {"vncPasswordFile", "/var/lib/thoth-cloud/vnc_passwords.json"},
        {"vncProxyFile", "/var/lib/thoth-cloud/vnc_proxies.json"},
        {"enabled", true}
    };
}

void VNCHandler::saveProxyInfo(const std::string& vmName, int proxyPort, const std::string& target) {
    json proxies;
    std::string proxyFile = config.value("vncProxyFile", "/var/lib/thoth-cloud/vnc_proxies.json");
    
    // Load existing proxies
    std::ifstream inFile(proxyFile);
    if (inFile.is_open()) {
        try {
            inFile >> proxies;
            inFile.close();
        } catch (const std::exception& e) {
            proxies = json::object();
        }
    }
    
    // Add new proxy info
    proxies[vmName] = {
        {"proxyPort", proxyPort},
        {"target", target},
        {"created", std::time(nullptr)}
    };
    
    // Save
    std::ofstream outFile(proxyFile);
    if (outFile.is_open()) {
        outFile << proxies.dump(2);
        outFile.close();
    }
}

json VNCHandler::getVNCInfo(const std::string& vmName) {
    json result;
    result["success"] = false;
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    virDomainPtr domain = virDomainLookupByName(conn, vmName.c_str());
    if (!domain) {
        result["error"] = "VM not found";
        return result;
    }
    
    // Check if VM is running
    virDomainInfo info;
    if (virDomainGetInfo(domain, &info) < 0 || info.state != VIR_DOMAIN_RUNNING) {
        result["error"] = "VM is not running";
        virDomainFree(domain);
        return result;
    }
    
    // Get VNC port from XML
    char* xmlDesc = virDomainGetXMLDesc(domain, 0);
    if (!xmlDesc) {
        result["error"] = "Failed to get VM XML";
        virDomainFree(domain);
        return result;
    }
    
    std::string xml(xmlDesc);
    free(xmlDesc);
    
    // Extract VNC port and listen address
    std::regex vncRegex("<graphics type='vnc'[^>]*port='(\\d+)'");
    std::regex listenRegex("listen='([^']+)'");
    std::smatch match;
    
    int vncPort = -1;
    std::string listenAddress = "127.0.0.1";
    
    if (std::regex_search(xml, match, vncRegex)) {
        vncPort = std::stoi(match[1].str());
    }
    
    if (std::regex_search(xml, match, listenRegex)) {
        listenAddress = match[1].str();
    }
    
    virDomainFree(domain);
    
    if (vncPort == -1) {
        result["error"] = "VNC not configured for this VM";
        return result;
    }
    
    // Get actual host IP (not localhost for remote access)
    std::string hostPublicIP = getHostPublicIP(conn);
    if (hostPublicIP.empty()) {
        // Fallback: Use connection hostname
        char* hostname = virConnectGetHostname(conn);
        hostPublicIP = hostname ? std::string(hostname) : "localhost";
        if (hostname) free(hostname);
    }
    
    // Determine if we need a proxy
    bool needsProxy = (listenAddress == "127.0.0.1" || listenAddress == "localhost");
    
    if (needsProxy) {
        // Start SSH tunnel or websocket proxy
        int proxyPort = setupVNCProxy(vmName, listenAddress, vncPort);
        if (proxyPort > 0) {
            vncPort = proxyPort;
            listenAddress = "127.0.0.1"; // Proxy runs locally
            result["proxy"] = true;
        }
    }
    
    // Build URLs for different access methods
    std::stringstream novncUrl;
    novncUrl << "http://" << config["novncHost"].get<std::string>() 
             << ":" << config["novncPort"].get<int>()
             << "/vnc.html?host=" << hostPublicIP
             << "&port=" << vncPort;
    
    // Add direct VNC access URL (for external VNC clients)
    result["directVNC"] = hostPublicIP + ":" + std::to_string(vncPort);
    
    // Check if VM has VNC password
    std::string vncPassword = getVNCPassword(vmName);
    if (!vncPassword.empty()) {
        novncUrl << "&password=" << vncPassword;
        result["hasPassword"] = true;
    } else {
        result["hasPassword"] = false;
    }
    
    result["success"] = true;
    result["vncPort"] = vncPort;
    result["vncHost"] = listenAddress;
    result["novncUrl"] = novncUrl.str();
    result["vmName"] = vmName;
    result["accessMethods"] = {
        {"direct", result["directVNC"]},
        {"web", result["novncUrl"]},
        {"host", hostPublicIP},
        {"needsProxy", needsProxy}
    };
    
    return result;
}

int VNCHandler::setupVNCProxy(const std::string& vmName, 
                              const std::string& targetHost, 
                              int targetPort) {
    RemoteExec::RemoteExecutor remoteExec(conn);
    
    // Find available port for proxy
    int proxyPort = findAvailablePort(5900, 6100);
    if (proxyPort == -1) {
        std::cerr << "No available ports for VNC proxy" << std::endl;
        return -1;
    }
    
    // Check if websockify is available
    if (!remoteExec.commandExists("websockify")) {
        std::cerr << "websockify is not installed on the target host" << std::endl;
        return -1;
    }
    
    // Create SSH tunnel or websocket proxy
    std::stringstream cmd;
    cmd << "websockify -D --web /usr/share/novnc " 
        << proxyPort << " " << targetHost << ":" << targetPort 
        << " > /dev/null 2>&1 &";
    
    auto result = remoteExec.execute(cmd.str());
    
    if (result.success()) {
        // Save proxy info for cleanup
        saveProxyInfo(vmName, proxyPort, targetHost + ":" + std::to_string(targetPort));
        return proxyPort;
    }
    
    std::cerr << "Failed to start VNC proxy: " << result.output << std::endl;
    return -1;
}

std::string VNCHandler::getHostPublicIP(virConnectPtr conn) {
    RemoteExec::RemoteExecutor remoteExec(conn);
    
    // Try multiple methods to get public IP
    std::vector<std::string> commands = {
        "curl -s --max-time 2 ifconfig.me",
        "curl -s --max-time 2 icanhazip.com",
        "curl -s --max-time 2 ipinfo.io/ip",
        "hostname -I | awk '{print $1}'"
    };
    
    for (const auto& cmd : commands) {
        auto result = remoteExec.execute(cmd);
        if (result.success() && !result.output.empty()) {
            std::string ip = result.output;
            // Remove whitespace
            ip.erase(std::remove_if(ip.begin(), ip.end(), ::isspace), ip.end());
            
            // Validate IP format
            if (std::regex_match(ip, std::regex(R"(^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$)"))) {
                return ip;
            }
        }
    }
    
    return "";
}

json VNCHandler::enableVNC(const std::string& vmName, const std::string& password) {
    json result;
    result["success"] = false;
    
    if (!conn) {
        result["error"] = "Not connected to libvirt";
        return result;
    }
    
    virDomainPtr domain = virDomainLookupByName(conn, vmName.c_str());
    if (!domain) {
        result["error"] = "VM not found";
        return result;
    }
    
    // Check if VM is shutdown (we need to modify XML)
    virDomainInfo info;
    virDomainGetInfo(domain, &info);
    bool wasRunning = (info.state == VIR_DOMAIN_RUNNING);
    
    if (wasRunning) {
        result["warning"] = "VM must be stopped to enable VNC. Stopping now...";
        virDomainShutdown(domain);
        
        // Wait for shutdown
        int maxWait = 30;
        while (maxWait-- > 0) {
            sleep(1);
            virDomainGetInfo(domain, &info);
            if (info.state == VIR_DOMAIN_SHUTOFF) break;
        }
        
        if (info.state != VIR_DOMAIN_SHUTOFF) {
            // Force destroy
            virDomainDestroy(domain);
            sleep(2);
        }
    }
    
    // Get current XML
    char* xmlDesc = virDomainGetXMLDesc(domain, VIR_DOMAIN_XML_INACTIVE);
    if (!xmlDesc) {
        result["error"] = "Failed to get VM XML";
        virDomainFree(domain);
        return result;
    }
    
    std::string xml(xmlDesc);
    free(xmlDesc);
    
    // Check if VNC graphics already exists
    if (xml.find("<graphics type='vnc'") != std::string::npos) {
        result["message"] = "VNC already enabled";
    } else {
        // Add VNC graphics to devices section
        std::string vncGraphics = 
            "    <graphics type='vnc' port='-1' autoport='yes' listen='0.0.0.0'>\n"
            "      <listen type='address' address='0.0.0.0'/>\n"
            "    </graphics>\n";
        
        size_t devicesEnd = xml.find("</devices>");
        if (devicesEnd != std::string::npos) {
            xml.insert(devicesEnd, vncGraphics);
        } else {
            result["error"] = "Could not find devices section in XML";
            virDomainFree(domain);
            return result;
        }
    }
    
    // Undefine and redefine domain with new XML
    virDomainUndefine(domain);
    virDomainPtr newDomain = virDomainDefineXML(conn, xml.c_str());
    
    if (!newDomain) {
        result["error"] = "Failed to redefine VM with VNC";
        virDomainFree(domain);
        return result;
    }
    
    // Save VNC password if provided
    if (!password.empty()) {
        setVNCPassword(vmName, password);
        result["passwordSet"] = true;
    }
    
    // Restart VM if it was running
    if (wasRunning) {
        if (virDomainCreate(newDomain) < 0) {
            result["warning"] = "VNC enabled but failed to restart VM";
        } else {
            result["restarted"] = true;
        }
    }
    
    virDomainFree(domain);
    virDomainFree(newDomain);
    
    result["success"] = true;
    result["message"] = "VNC enabled successfully";
    
    return result;
}

std::string VNCHandler::getVNCPassword(const std::string& vmName) {
    json passwords;
    std::string passwordFile = config["vncPasswordFile"].get<std::string>();
    
    std::ifstream file(passwordFile);
    if (file.is_open()) {
        try {
            file >> passwords;
            file.close();
            if (passwords.contains(vmName)) {
                return passwords[vmName].get<std::string>();
            }
        } catch (const std::exception& e) {
            std::cerr << "Error reading VNC passwords: " << e.what() << std::endl;
        }
    }
    
    return "";
}

void VNCHandler::setVNCPassword(const std::string& vmName, const std::string& password) {
    json passwords;
    std::string passwordFile = config["vncPasswordFile"].get<std::string>();
    
    // Ensure directory exists
    std::string dir = "/var/lib/thoth-cloud";
    mkdir(dir.c_str(), 0755);
    
    // Load existing passwords
    std::ifstream inFile(passwordFile);
    if (inFile.is_open()) {
        try {
            inFile >> passwords;
            inFile.close();
        } catch (const std::exception& e) {
            passwords = json::object();
        }
    }
    
    // Set new password
    passwords[vmName] = password;
    
    // Save
    std::ofstream outFile(passwordFile);
    if (outFile.is_open()) {
        outFile << passwords.dump(2);
        outFile.close();
    }
}

json VNCHandler::getNoVNCStatus() {
    json result;
    
    // Check if noVNC service is running
    RemoteExec::RemoteExecutor remoteExec(conn);
    auto serviceCheck = remoteExec.execute("systemctl is-active novnc 2>/dev/null || echo 'inactive'");
    
    result["novncRunning"] = (serviceCheck.success() && 
                              serviceCheck.output.find("active") != std::string::npos &&
                              serviceCheck.output.find("inactive") == std::string::npos);
    result["novncHost"] = config["novncHost"];
    result["novncPort"] = config["novncPort"];
    result["enabled"] = config["enabled"];
    
    return result;
}

json VNCHandler::createVNCProxy(const std::string& vmName, int vncPort) {
    json result;
    result["success"] = false;
    
    // This creates a websockify proxy for a specific VM
    // Useful when you need isolated VNC access per VM
    
    RemoteExec::RemoteExecutor remoteExec(conn);
    
    // Find an available proxy port (60xx range for user VNC proxies)
    int proxyPort = 6000 + (vncPort - 5900);
    
    // Check if port is already in use
    std::stringstream checkCmd;
    checkCmd << "netstat -tuln | grep -q ':" << proxyPort << " ' && echo 'in_use' || echo 'free'";
    auto checkResult = remoteExec.execute(checkCmd.str());
    
    if (checkResult.output.find("in_use") != std::string::npos) {
        // Find alternative port
        proxyPort = findAvailablePort(6000, 6200);
        if (proxyPort == -1) {
            result["error"] = "No available ports for VNC proxy";
            return result;
        }
    }
    
    // Start websockify for this specific VM
    std::stringstream cmd;
    cmd << "websockify -D " << proxyPort << " localhost:" << vncPort 
        << " > /dev/null 2>&1 &";
    
    auto execResult = remoteExec.execute(cmd.str());
    
    if (execResult.success()) {
        saveProxyInfo(vmName, proxyPort, "localhost:" + std::to_string(vncPort));
        result["success"] = true;
        result["proxyPort"] = proxyPort;
        result["url"] = "ws://localhost:" + std::to_string(proxyPort);
    } else {
        result["error"] = "Failed to create VNC proxy: " + execResult.output;
    }
    
    return result;
}