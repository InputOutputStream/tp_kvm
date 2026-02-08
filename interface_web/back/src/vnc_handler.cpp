#include "../include/vnc_handler.hpp"
#include "../include/utils.hpp"
#include "../include/remote_executor.hpp"
#include <sstream>
#include <regex>
#include <fstream>
#include <unistd.h>

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
        {"enabled", true}
    };
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
    
    // Extract VNC port from XML
    // <graphics type='vnc' port='5900' autoport='yes' listen='127.0.0.1'>
    std::regex vncRegex("<graphics type='vnc'[^>]*port='(\\d+)'");
    std::smatch match;
    
    int vncPort = -1;
    std::string vncHost = "127.0.0.1";
    
    if (std::regex_search(xml, match, vncRegex)) {
        vncPort = std::stoi(match[1].str());
    }
    
    // Extract listen address if present
    std::regex listenRegex("listen='([^']+)'");
    if (std::regex_search(xml, match, listenRegex)) {
        vncHost = match[1].str();
    }
    
    virDomainFree(domain);
    
    if (vncPort == -1) {
        result["error"] = "VNC not configured for this VM";
        return result;
    }
    
    // Build noVNC URL
    std::stringstream novncUrl;
    novncUrl << "http://" << config["novncHost"].get<std::string>() 
             << ":" << config["novncPort"].get<int>()
             << "/vnc.html?host=" << config["novncHost"].get<std::string>()
             << "&port=" << vncPort;
    
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
    result["vncHost"] = vncHost;
    result["novncUrl"] = novncUrl.str();
    result["vmName"] = vmName;
    
    return result;
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
        virDomainCreate(newDomain);
        result["restarted"] = true;
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
    auto serviceCheck = remoteExec.execute("systemctl is-active novnc");
    
    result["novncRunning"] = (serviceCheck.success() && 
                              serviceCheck.output.find("active") != std::string::npos);
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
    
    // Start websockify for this specific VM
    std::stringstream cmd;
    cmd << "websockify -D " << proxyPort << " localhost:" << vncPort;
    
    auto execResult = remoteExec.execute(cmd.str());
    
    if (execResult.success()) {
        result["success"] = true;
        result["proxyPort"] = proxyPort;
        result["url"] = "ws://localhost:" + std::to_string(proxyPort);
    } else {
        result["error"] = "Failed to create VNC proxy: " + execResult.output;
    }
    
    return result;
}