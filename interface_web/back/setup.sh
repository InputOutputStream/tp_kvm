#!/bin/bash

set -e  # Arrêter en cas d'erreur

echo "🚀 Configuration du serveur libvirt C++..."
echo ""

# Couleurs pour l'output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Répertoires
PROJECT_DIR="$(pwd)"
DEPS_DIR="$PROJECT_DIR/dependencies"
INCLUDE_DIR="$PROJECT_DIR/include"

echo -e "${BLUE}📁 Création de la structure de répertoires...${NC}"
mkdir -p "$DEPS_DIR"
mkdir -p "$INCLUDE_DIR"
mkdir -p "$PROJECT_DIR/build"

# ========== VÉRIFICATION DES DÉPENDANCES SYSTÈME ==========
echo ""
echo -e "${BLUE}🔍 Vérification des dépendances système...${NC}"

if ! command -v g++ &> /dev/null; then
    echo -e "${RED}❌ g++ n'est pas installé${NC}"
    echo "Installez-le avec: sudo apt install g++"
    exit 1
fi

if ! dpkg -l | grep -q libvirt-dev; then
    echo -e "${YELLOW}⚠️  libvirt-dev n'est pas installé${NC}"
    echo "Installation de libvirt-dev..."
    sudo apt update
    sudo apt install -y libvirt-dev libvirt-daemon-system
else
    echo -e "${GREEN}✅ libvirt-dev est installé${NC}"
fi

# ========== CPP-HTTPLIB ==========
echo ""
echo -e "${BLUE}📦 Configuration de cpp-httplib...${NC}"

if [ ! -f "$INCLUDE_DIR/httplib.h" ]; then
    if [ ! -d "$DEPS_DIR/cpp-httplib" ]; then
        echo "Clonage de cpp-httplib..."
        cd "$DEPS_DIR"
        git clone https://github.com/yhirose/cpp-httplib.git
    fi
    
    echo "Copie de httplib.h..."
    cp "$DEPS_DIR/cpp-httplib/httplib.h" "$INCLUDE_DIR/"
    echo -e "${GREEN}✅ httplib.h installé${NC}"
else
    echo -e "${GREEN}✅ httplib.h déjà présent${NC}"
fi

# ========== NLOHMANN JSON ==========
echo ""
echo -e "${BLUE}📦 Configuration de nlohmann/json...${NC}"

if [ ! -f "$INCLUDE_DIR/json.hpp" ]; then
    if [ ! -d "$DEPS_DIR/json" ]; then
        echo "Clonage de nlohmann/json..."
        cd "$DEPS_DIR"
        git clone https://github.com/nlohmann/json.git
    fi
    
    echo "Copie de json.hpp..."
    cp "$DEPS_DIR/json/single_include/nlohmann/json.hpp" "$INCLUDE_DIR/"
    echo -e "${GREEN}✅ json.hpp installé${NC}"
else
    echo -e "${GREEN}✅ json.hpp déjà présent${NC}"
fi

# ========== CRÉATION DU FICHIER SERVER.CPP ==========
echo ""
echo -e "${BLUE}📝 Création du fichier server.cpp...${NC}"

cat > "$PROJECT_DIR/server.cpp" << 'EOF'
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
            std::cerr << "Erreur: impossible de se connecter à libvirt" << std::endl;
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
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt échouée"}}.dump(), "application/json");
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
    
    // GET /api/vms/:name - Info détaillée
    svr.Get(R"(/api/vms/([^/]+)$)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt échouée"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.status = 404;
            res.set_content(json{{"success", false}, {"error", "VM non trouvée"}}.dump(), "application/json");
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
        
        json response = {
            {"success", true},
            {"parsed", parsed},
            {"xml", xmlDesc}
        };
        
        free(xmlDesc);
        virDomainFree(domain);
        
        res.set_content(response.dump(), "application/json");
    });
    
    // POST /api/vms/:name/start
    svr.Post(R"(/api/vms/([^/]+)/start)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt échouée"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvée"}}.dump(), "application/json");
            return;
        }
        
        int result = virDomainCreate(domain);
        virDomainFree(domain);
        
        if (result < 0) {
            res.set_content(json{{"success", false}, {"error", "Échec démarrage"}}.dump(), "application/json");
        } else {
            res.set_content(json{{"success", true}, {"output", "VM démarrée"}}.dump(), "application/json");
        }
    });
    
    // POST /api/vms/:name/shutdown
    svr.Post(R"(/api/vms/([^/]+)/shutdown)", [](const Request& req, Response& res) {
        std::string name = req.matches[1];
        
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt échouée"}}.dump(), "application/json");
            return;
        }
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            res.set_content(json{{"success", false}, {"error", "VM non trouvée"}}.dump(), "application/json");
            return;
        }
        
        int result = virDomainShutdown(domain);
        virDomainFree(domain);
        
        if (result < 0) {
            res.set_content(json{{"success", false}, {"error", "Échec arrêt"}}.dump(), "application/json");
        } else {
            res.set_content(json{{"success", true}, {"output", "VM en cours d'arrêt"}}.dump(), "application/json");
        }
    });
    
    // GET /api/system/info
    svr.Get("/api/system/info", [](const Request& req, Response& res) {
        if (!connectLibvirt()) {
            res.set_content(json{{"success", false}, {"error", "Connexion libvirt échouée"}}.dump(), "application/json");
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
        
        json response = {
            {"success", true},
            {"nodeInfo", info.dump(2)},
            {"version", "Libvirt version: " + std::to_string(libVersion)}
        };
        
        res.set_content(response.dump(), "application/json");
    });
    
    // Servir les fichiers statiques (si le dossier front existe)
    if (fileExists("../front")) {
        svr.set_mount_point("/", "../front");
    }
}

int main() {
    std::cout << "🚀 Démarrage du serveur libvirt C++..." << std::endl;
    
    // Connexion initiale à libvirt
    if (!connectLibvirt()) {
        std::cerr << "❌ Impossible de se connecter à libvirt" << std::endl;
        std::cerr << "💡 Vérifiez que libvirt est installé et actif" << std::endl;
        std::cerr << "   sudo systemctl start libvirtd" << std::endl;
        return 1;
    }
    
    std::cout << "✅ Connecté à libvirt" << std::endl;
    
    Server svr;
    
    // Middleware CORS
    svr.set_post_routing_handler([](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });
    
    setupRoutes(svr);
    
    const int PORT = 3000;
    std::cout << "🌐 Serveur démarré sur http://localhost:" << PORT << std::endl;
    std::cout << "📡 API disponible sur http://localhost:" << PORT << "/api" << std::endl;
    std::cout << "\nAppuyez sur Ctrl+C pour arrêter le serveur" << std::endl;
    
    svr.listen("0.0.0.0", PORT);
    
    // Fermeture propre
    if (conn) {
        virConnectClose(conn);
    }
    
    return 0;
}
EOF

echo -e "${GREEN}✅ server.cpp créé${NC}"

# ========== COMPILATION ==========
echo ""
echo -e "${BLUE}🔨 Compilation du serveur...${NC}"

cd "$PROJECT_DIR"

g++ -std=c++17 -o build/libvirt_server server.cpp \
    -I"$INCLUDE_DIR" \
    -lvirt \
    -lpthread \
    -O2 \
    -Wall

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ Compilation réussie !${NC}"
    echo ""
    echo -e "${GREEN}═══════════════════════════════════════${NC}"
    echo -e "${GREEN}🎉 CONFIGURATION TERMINÉE !${NC}"
    echo -e "${GREEN}═══════════════════════════════════════${NC}"
    echo ""
    echo -e "${YELLOW}Pour démarrer le serveur :${NC}"
    echo -e "  ${BLUE}sudo ./build/libvirt_server${NC}"
    echo ""
    echo -e "${YELLOW}Le serveur sera disponible sur :${NC}"
    echo -e "  ${BLUE}http://localhost:3000${NC}"
    echo ""
else
    echo -e "${RED}❌ Erreur de compilation${NC}"
    exit 1
fi
EOF

echo -e "${GREEN}✅ Script créé : setup.sh${NC}"
echo ""
echo -e "${YELLOW}Pour exécuter le script :${NC}"
echo "  chmod +x setup.sh"
echo "  ./setup.sh"