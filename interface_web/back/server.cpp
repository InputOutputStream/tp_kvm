#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <ctime>
#include <sstream>
#include <fstream>
#include <libvirt/libvirt.h>
#include <regex>
#include <cstdlib>
#include <sys/stat.h>
#include <chrono>
#include "include/httplib.h"
#include "include/json.hpp"

using json = nlohmann::json;
using namespace httplib;

// Connexion globale à libvirt
virConnectPtr conn = nullptr;

// Cache pour les stats CPU
struct CPUCache {
    unsigned long long cpuTime;
    long long timestamp;
};
std::map<std::string, CPUCache> statsCache;

// ========== UTILITAIRES ==========

long long getCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string execCommand(const std::string& cmd) {
    char buffer[128];
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

bool fileExists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// ========== CONNEXION LIBVIRT ==========

bool connectLibvirt() {
    if (conn == nullptr) {
        conn = virConnectOpen("qemu:///system");
        if (conn == nullptr) {
            std::cerr << "Erreur: impossible de se connecter a libvirt" << std::endl;
            return false;
        }
    }
    return true;
}

// ========== FONCTIONS VM ==========

json getVMStats(virDomainPtr domain, const std::string& vmName) {
    json stats;
    
    virDomainInfo info;
    if (virDomainGetInfo(domain, &info) < 0) {
        return stats;
    }
    
    // CPU usage
    double cpuUsage = 0.0;
    unsigned long long cpuTime = info.cpuTime;
    
    if (statsCache.find(vmName) != statsCache.end()) {
        CPUCache cached = statsCache[vmName];
        long long timeDiff = getCurrentTimeMs() - cached.timestamp;
        unsigned long long cpuDiff = cpuTime - cached.cpuTime;
        if (timeDiff > 0) {
            cpuUsage = ((double)cpuDiff / (timeDiff * 1000000.0)) * 100.0;
        }
    }
    statsCache[vmName] = {cpuTime, getCurrentTimeMs()};
    
    stats["cpu"] = cpuUsage;
    
    // Memory
    stats["memory"] = {
        {"used", info.memory},
        {"max", info.maxMem},
        {"percent", info.maxMem > 0 ? (info.memory * 100.0 / info.maxMem) : 0}
    };
    
    // Disk stats
    virDomainBlockStatsStruct blockStats;
    long long diskRead = 0, diskWrite = 0;
    if (virDomainBlockStats(domain, "vda", &blockStats, sizeof(blockStats)) == 0) {
        diskRead = blockStats.rd_bytes;
        diskWrite = blockStats.wr_bytes;
    }
    
    stats["disk"] = {
        {"read", diskRead},
        {"write", diskWrite},
        {"readMB", diskRead / 1024.0 / 1024.0},
        {"writeMB", diskWrite / 1024.0 / 1024.0}
    };
    
    // Network stats
    virDomainInterfaceStatsStruct netStats;
    long long netRx = 0, netTx = 0;
    if (virDomainInterfaceStats(domain, "vnet0", &netStats, sizeof(netStats)) == 0) {
        netRx = netStats.rx_bytes;
        netTx = netStats.tx_bytes;
    }
    
    stats["network"] = {
        {"rx", netRx},
        {"tx", netTx},
        {"rxMB", netRx / 1024.0 / 1024.0},
        {"txMB", netTx / 1024.0 / 1024.0}
    };
    
    return stats;
}

std::string getStateString(int state) {
    const char* states[] = {"no state", "running", "blocked", "paused", 
                           "shutdown", "shut off", "crashed", "pmsuspended"};
    if (state >= 0 && state < 8) {
        return states[state];
    }
    return "unknown";
}

// ========== ENDPOINTS API ==========

void setupRoutes(Server& svr) {
    
    // GET /api/vms - Liste toutes les VMs
    svr.Get("/api/vms", [](const Request& req, Response& res) {
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr* domains;
        int numDomains = virConnectListAllDomains(conn, &domains, 0);
        
        if (numDomains < 0) {
            res.set_content(json{{"success", false}, {"error", "Erreur listage VMs"}}.dump(), "application/json");
            return;
        }
        
        json vms = json::array();
        
        for (int i = 0; i < numDomains; i++) {
            const char* name = virDomainGetName(domains[i]);
            virDomainInfo info;
            virDomainGetInfo(domains[i], &info);
            
            int id = virDomainGetID(domains[i]);
            std::string state = getStateString(info.state);
            bool isRunning = (info.state == VIR_DOMAIN_RUNNING);
            
            json vm = {
                {"id", id},
                {"name", name},
                {"state", state},
                {"running", isRunning},
                {"stats", nullptr}
            };
            
            if (isRunning) {
                vm["stats"] = getVMStats(domains[i], name);
            }
            
            vms.push_back(vm);
            virDomainFree(domains[i]);
        }
        
        free(domains);
        
        json response = {{"success", true}, {"vms", vms}};
        res.set_content(response.dump(), "application/json");
    });
    
    // GET /api/vms/:name - Info detaillee
    svr.Get(R"(/api/vms/([^/]+)$)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.status = 404;
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        virDomainInfo info;
        virDomainGetInfo(domain, &info);
        
        char* xmlDesc = virDomainGetXMLDesc(domain, 0);
        
        json parsed = {
            {"Max memory", std::to_string(info.maxMem) + " KB"},
            {"Used memory", std::to_string(info.memory) + " KB"},
            {"CPU(s)", info.nrVirtCpu},
            {"CPU time", std::to_string(info.cpuTime) + "ns"},
            {"State", info.state}
        };
        
        std::stringstream infoStr;
        infoStr << "Max memory: " << info.maxMem << " KB\n";
        infoStr << "Used memory: " << info.memory << " KB\n";
        infoStr << "CPU(s): " << info.nrVirtCpu << "\n";
        infoStr << "CPU time: " << info.cpuTime << "ns\n";
        infoStr << "State: " << info.state;
        
        json response = {
            {"success", true},
            {"info", infoStr.str()},
            {"parsed", parsed},
            {"xml", xmlDesc}
        };
        
        free(xmlDesc);
        virDomainFree(domain);
        
        res.set_content(response.dump(), "application/json");
    });
    
    // GET /api/vms/:name/status
    svr.Get(R"(/api/vms/([^/]+)/status)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.status = 404;
            res.set_content(json{{"success", false}, {"error", "VM not found"}}.dump(), "application/json");
            return;
        }
        
        virDomainInfo info;
        virDomainGetInfo(domain, &info);
        
        std::string state = getStateString(info.state);
        bool isRunning = (info.state == VIR_DOMAIN_RUNNING);
        
        json response = {
            {"success", true},
            {"state", state},
            {"running", isRunning}
        };
        
        virDomainFree(domain);
        res.set_content(response.dump(), "application/json");
    });
    
    // GET /api/vms/:name/stats
    svr.Get(R"(/api/vms/([^/]+)/stats)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.status = 404;
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        json stats = getVMStats(domain, name);
        virDomainFree(domain);
        
        json response = {{"success", true}, {"stats", stats}};
        res.set_content(response.dump(), "application/json");
    });
    
    // POST /api/vms/:name/start
    svr.Post(R"(/api/vms/([^/]+)/start)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        int result = virDomainCreate(domain);
        virDomainFree(domain);
        
        if (result < 0) {
            res.set_content(json{{"success", false}, {"error", "Echec demarrage"}}.dump(), "application/json");
        } else {
            res.set_content(json{{"success", true}, {"output", "Domain started"}}.dump(), "application/json");
        }
    });
    
    // POST /api/vms/:name/shutdown
    svr.Post(R"(/api/vms/([^/]+)/shutdown)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        int result = virDomainShutdown(domain);
        virDomainFree(domain);
        
        if (result < 0) {
            res.set_content(json{{"success", false}, {"error", "Echec arret"}}.dump(), "application/json");
        } else {
            res.set_content(json{{"success", true}, {"output", "Domain is shutting down"}}.dump(), "application/json");
        }
    });
    
    // POST /api/vms/:name/destroy
    svr.Post(R"(/api/vms/([^/]+)/destroy)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        int result = virDomainDestroy(domain);
        virDomainFree(domain);
        
        if (result < 0) {
            res.set_content(json{{"success", false}, {"error", "Echec destruction"}}.dump(), "application/json");
        } else {
            res.set_content(json{{"success", true}, {"output", "Domain destroyed"}}.dump(), "application/json");
        }
    });
    
    // POST /api/vms/:name/reboot
    svr.Post(R"(/api/vms/([^/]+)/reboot)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        int result = virDomainReboot(domain, 0);
        virDomainFree(domain);
        
        if (result < 0) {
            res.set_content(json{{"success", false}, {"error", "Echec reboot"}}.dump(), "application/json");
        } else {
            res.set_content(json{{"success", true}, {"output", "Domain is rebooting"}}.dump(), "application/json");
        }
    });
    
    // POST /api/vms/:name/pause
    svr.Post(R"(/api/vms/([^/]+)/pause)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        int result = virDomainSuspend(domain);
        virDomainFree(domain);
        
        if (result < 0) {
            res.set_content(json{{"success", false}, {"error", "Echec pause"}}.dump(), "application/json");
        } else {
            res.set_content(json{{"success", true}, {"output", "Domain suspended"}}.dump(), "application/json");
        }
    });
    
    // POST /api/vms/:name/resume
    svr.Post(R"(/api/vms/([^/]+)/resume)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        int result = virDomainResume(domain);
        virDomainFree(domain);
        
        if (result < 0) {
            res.set_content(json{{"success", false}, {"error", "Echec reprise"}}.dump(), "application/json");
        } else {
            res.set_content(json{{"success", true}, {"output", "Domain resumed"}}.dump(), "application/json");
        }
    });
    
    // GET /api/vms/:name/vnc
    svr.Get(R"(/api/vms/([^/]+)/vnc)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        char* xmlDesc = virDomainGetXMLDesc(domain, 0);
        std::string xml(xmlDesc);
        free(xmlDesc);
        virDomainFree(domain);
        
        // Parser le XML pour extraire le port VNC
        std::regex portRegex("<graphics type='vnc' port='(\\d+)'");
        std::smatch match;
        
        if (std::regex_search(xml, match, portRegex) && match[1].str() != "-1") {
            int port = std::stoi(match[1].str());
            std::string display = ":" + std::to_string(port - 5900);
            
            json response = {
                {"success", true},
                {"display", display},
                {"port", port},
                {"host", "localhost"}
            };
            res.set_content(response.dump(), "application/json");
        } else {
            json response = {
                {"success", false},
                {"error", "VNC not configured or VM not running"}
            };
            res.set_content(response.dump(), "application/json");
        }
    });
    
    // GET /api/vms/:name/snapshots
    svr.Get(R"(/api/vms/([^/]+)/snapshots)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.status = 404;
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        virDomainSnapshotPtr* snapshots;
        int numSnapshots = virDomainListAllSnapshots(domain, &snapshots, 0);
        
        json snapshotList = json::array();
        
        if (numSnapshots >= 0) {
            for (int i = 0; i < numSnapshots; i++) {
                const char* snapName = virDomainSnapshotGetName(snapshots[i]);
                char* xmlDesc = virDomainSnapshotGetXMLDesc(snapshots[i], 0);
                
                std::string xml(xmlDesc);
                std::regex timeRegex("<creationTime>(\\d+)</creationTime>");
                std::regex stateRegex("<state>(\\w+)</state>");
                std::smatch match;
                
                std::string timeStr = "Unknown";
                if (std::regex_search(xml, match, timeRegex)) {
                    time_t timestamp = std::stoll(match[1].str());
                    char buffer[100];
                    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&timestamp));
                    timeStr = buffer;
                }
                
                std::string state = "unknown";
                if (std::regex_search(xml, match, stateRegex)) {
                    state = match[1].str();
                }
                
                snapshotList.push_back({
                    {"name", snapName},
                    {"creationTime", timeStr},
                    {"state", state}
                });
                
                free(xmlDesc);
                virDomainSnapshotFree(snapshots[i]);
            }
            free(snapshots);
        }
        
        virDomainFree(domain);
        
        json response = {{"success", true}, {"snapshots", snapshotList}};
        res.set_content(response.dump(), "application/json");
    });
    
    // POST /api/vms/:name/snapshots
    svr.Post(R"(/api/vms/([^/]+)/snapshots)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(json{{"success", false}, {"error", "JSON invalide"}}.dump(), "application/json");
            return;
        }
        
        if (!body.contains("snapshotName")) {
            res.status = 400;
            res.set_content(json{{"success", false}, {"error", "Snapshot name required"}}.dump(), "application/json");
            return;
        }
        
        std::string snapshotName = body["snapshotName"];
        std::string description = body.value("description", "Created via web interface");
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        std::string snapshotXML = 
            "<domainsnapshot>"
            "<name>" + snapshotName + "</name>"
            "<description>" + description + "</description>"
            "</domainsnapshot>";
        
        virDomainSnapshotPtr snapshot = virDomainSnapshotCreateXML(domain, snapshotXML.c_str(), 0);
        virDomainFree(domain);
        
        if (!snapshot) {
            res.set_content(json{{"success", false}, {"error", "Echec creation snapshot"}}.dump(), "application/json");
        } else {
            virDomainSnapshotFree(snapshot);
            res.set_content(json{{"success", true}, {"output", "Snapshot created"}}.dump(), "application/json");
        }
    });
    
    // POST /api/vms/:name/snapshots/:snapshot/revert
    svr.Post(R"(/api/vms/([^/]+)/snapshots/([^/]+)/revert)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        std::string snapshotName = req.matches[2];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        virDomainSnapshotPtr snapshot = virDomainSnapshotLookupByName(domain, snapshotName.c_str(), 0);
        if (!snapshot) {
            virDomainFree(domain);
            res.set_content(json{{"success", false}, {"error", "Snapshot non trouve"}}.dump(), "application/json");
            return;
        }
        
        int result = virDomainRevertToSnapshot(snapshot, 0);
        
        virDomainSnapshotFree(snapshot);
        virDomainFree(domain);
        
        if (result < 0) {
            res.set_content(json{{"success", false}, {"error", "Echec restauration"}}.dump(), "application/json");
        } else {
            res.set_content(json{{"success", true}, {"output", "Reverted to snapshot"}}.dump(), "application/json");
        }
    });
    
    // DELETE /api/vms/:name/snapshots/:snapshot
    svr.Delete(R"(/api/vms/([^/]+)/snapshots/([^/]+))", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        std::string snapshotName = req.matches[2];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        virDomainSnapshotPtr snapshot = virDomainSnapshotLookupByName(domain, snapshotName.c_str(), 0);
        if (!snapshot) {
            virDomainFree(domain);
            res.set_content(json{{"success", false}, {"error", "Snapshot non trouve"}}.dump(), "application/json");
            return;
        }
        
        int result = virDomainSnapshotDelete(snapshot, 0);
        
        virDomainSnapshotFree(snapshot);
        virDomainFree(domain);
        
        if (result < 0) {
            res.set_content(json{{"success", false}, {"error", "Echec suppression"}}.dump(), "application/json");
        } else {
            res.set_content(json{{"success", true}, {"output", "Snapshot deleted"}}.dump(), "application/json");
        }
    });
    
    // POST /api/vms/:name/clone
    svr.Post(R"(/api/vms/([^/]+)/clone)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(json{{"success", false}, {"error", "JSON invalide"}}.dump(), "application/json");
            return;
        }
        
        if (!body.contains("cloneName")) {
            res.status = 400;
            res.set_content(json{{"success", false}, {"error", "Clone name required"}}.dump(), "application/json");
            return;
        }
        
        std::string cloneName = body["cloneName"];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        char* xmlDesc = virDomainGetXMLDesc(domain, 0);
        std::string xml(xmlDesc);
        free(xmlDesc);
        virDomainFree(domain);
        
        // Modifier le XML pour le clone
        xml = std::regex_replace(xml, std::regex("<name>" + name + "</name>"), "<name>" + cloneName + "</name>");
        xml = std::regex_replace(xml, std::regex("<uuid>.*?</uuid>"), "");
        
        // Copier les disques
        std::regex diskRegex("<source file='([^']+)'");
        std::smatch match;
        std::string::const_iterator searchStart(xml.cbegin());
        
        while (std::regex_search(searchStart, xml.cend(), match, diskRegex)) {
            std::string oldPath = match[1].str();
            std::string newPath = std::regex_replace(oldPath, std::regex(name), cloneName);
            
            try {
                std::string cpCmd = "cp " + oldPath + " " + newPath;
                execCommand(cpCmd);
                
                xml = std::regex_replace(xml, std::regex(oldPath), newPath);
            } catch (...) {
                res.status = 500;
                res.set_content(json{{"success", false}, {"error", "Erreur copie disque"}}.dump(), "application/json");
                return;
            }
            
            searchStart = match.suffix().first;
        }
        
        // Definir le nouveau domaine
        virDomainPtr newDomain = virDomainDefineXML(conn, xml.c_str());
        if (!newDomain) {
            res.status = 500;
            res.set_content(json{{"success", false}, {"error", "Echec creation clone"}}.dump(), "application/json");
            return;
        }
        
        virDomainFree(newDomain);
        res.set_content(json{{"success", true}, {"output", "VM cloned successfully"}}.dump(), "application/json");
    });
    
    // POST /api/vms/deploy
    svr.Post("/api/vms/deploy", [](const Request& req, Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(json{{"success", false}, {"error", "JSON invalide"}}.dump(), "application/json");
            return;
        }
        
        // Validation des parametres requis
        std::vector<std::string> required = {"hostname", "memory", "vcpus", "disk", "isoPath", "username"};
        for (const auto& field : required) {
            if (!body.contains(field)) {
                res.status = 400;
                res.set_content(json{{"success", false}, {"error", "Parametres manquants: " + field}}.dump(), "application/json");
                return;
            }
        }
        
        // Pour l'instant, retourner une erreur car l'implementation complete necessite cloud-init
        res.status = 501;
        res.set_content(json{{"success", false}, {"error", "VM deployment not yet implemented in C++ version. Use virt-install manually."}}.dump(), "application/json");
    });
    
    // GET /api/vms/:name/ip
    svr.Get(R"(/api/vms/([^/]+)/ip)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvee"}}.dump(), "application/json");
            return;
        }
        
        // Note: L'obtention de l'IP via qemu-guest-agent necessite des bindings supplementaires
        // Pour l'instant, retourner une erreur
        virDomainFree(domain);
        res.set_content(json{{"success", false}, {"error", "IP non disponible pour le moment"}}.dump(), "application/json");
    });
    
    // GET /api/system/info
    svr.Get("/api/system/info", [](const Request& req, Response& res) {
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt echouee"}}.dump(), "application/json");
            return;
        }
        
        virNodeInfo nodeInfo;
        virNodeGetInfo(conn, &nodeInfo);
        
        unsigned long version;
        virConnectGetVersion(conn, &version);
        
        unsigned long libVersion;
        virConnectGetLibVersion(conn, &libVersion);
        
        json info = {
            {"model", std::string(nodeInfo.model)},
            {"memory", std::to_string(nodeInfo.memory) + " KB"},
            {"cpus", nodeInfo.cpus},
            {"mhz", std::to_string(nodeInfo.mhz) + " MHz"},
            {"nodes", nodeInfo.nodes},
            {"sockets", nodeInfo.sockets},
            {"cores", nodeInfo.cores},
            {"threads", nodeInfo.threads},
            {"hypervisorVersion", version},
            {"libvirtVersion", libVersion}
        };
        
        std::stringstream nodeInfoStr;
        nodeInfoStr << "Model: " << nodeInfo.model << "\n";
        nodeInfoStr << "Memory: " << nodeInfo.memory << " KB\n";
        nodeInfoStr << "CPUs: " << nodeInfo.cpus << "\n";
        nodeInfoStr << "MHz: " << nodeInfo.mhz << " MHz\n";
        nodeInfoStr << "Nodes: " << nodeInfo.nodes << "\n";
        nodeInfoStr << "Sockets: " << nodeInfo.sockets << "\n";
        nodeInfoStr << "Cores: " << nodeInfo.cores << "\n";
        nodeInfoStr << "Threads: " << nodeInfo.threads << "\n";
        nodeInfoStr << "Hypervisor Version: " << version << "\n";
        nodeInfoStr << "Libvirt Version: " << libVersion;
        
        json response = {
            {"success", true},
            {"nodeInfo", nodeInfoStr.str()},
            {"version", "Libvirt version: " + std::to_string(libVersion)}
        };
        
        res.set_content(response.dump(), "application/json");
    });
    
    // Servir les fichiers statiques
    if (fileExists("../front")) {
        svr.set_mount_point("/", "../front");
    }
}

int main() {
    std::cout << "Demarrage du serveur libvirt C++..." << std::endl;
    
    // Connexion initiale a libvirt
    if (!connectLibvirt()) {
        std::cerr << "Impossible de se connecter a libvirt" << std::endl;
        std::cerr << "Verifiez que libvirt est installe et actif" << std::endl;
        std::cerr << "   sudo systemctl start libvirtd" << std::endl;
        return 1;
    }
    
    std::cout << "Connecte a libvirt" << std::endl;
    
    Server svr;
    
    // Middleware CORS
    svr.set_post_routing_handler([](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });
    
    // Handler pour OPTIONS (CORS preflight)
    svr.Options(R"(.*)", [](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 200;
    });
    
    setupRoutes(svr);
    
    const int PORT = 3000;
    std::cout << "Serveur demarre sur http://localhost:" << PORT << std::endl;
    std::cout << "API disponible sur http://localhost:" << PORT << "/api" << std::endl;
    std::cout << "\nAppuyez sur Ctrl+C pour arreter le serveur" << std::endl;
    
    svr.listen("0.0.0.0", PORT);
    
    // Fermeture propre
    if (conn) {
        virConnectClose(conn);
    }
    
    return 0;
}