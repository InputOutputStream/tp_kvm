#ifndef ISOLATION_UTILS_HPP
#define ISOLATION_UTILS_HPP

#include <iostream>
#include <string>
#include <regex>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <map>
#include <set>
#include <fstream>
#include <cstring>

#include "json.hpp"

/**
 * @brief Complete isolation system for multi-tenant cloud platform
 * 
 * This provides true user isolation similar to AWS/Azure:
 * - Users cannot see each other's resources
 * - All names are system-generated to prevent conflicts
 * - Resource IDs are opaque tokens (like i-1234abcd in AWS)
 */

using json = nlohmann::json;

class ResourceIDGenerator {
private:
    static std::mutex idMutex;
    static std::set<std::string> usedIDs;
    
    static std::string generateRandomID(const std::string& prefix, int length = 8) {
        static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, sizeof(chars) - 2);
        
        std::string id = prefix;
        for (int i = 0; i < length; ++i) {
            id += chars[dis(gen)];
        }
        return id;
    }
    
public:
    /**
     * Generate AWS-style resource IDs
     * VM: i-1a2b3c4d (like EC2 instance)
     * Network: net-5e6f7g8h (like VPC)
     * App: app-9i0j1k2l (like ECS task)
     * Cluster: cls-3m4n5o6p (like EKS cluster)
     */
    
    static std::string generateVMID() {
        std::lock_guard<std::mutex> lock(idMutex);
        std::string id;
        do {
            id = generateRandomID("i-", 8);
        } while (usedIDs.find(id) != usedIDs.end());
        usedIDs.insert(id);
        return id;
    }
    
    static std::string generateNetworkID() {
        std::lock_guard<std::mutex> lock(idMutex);
        std::string id;
        do {
            id = generateRandomID("net-", 10);
        } while (usedIDs.find(id) != usedIDs.end());
        usedIDs.insert(id);
        return id;
    }
    
    static std::string generateAppID() {
        std::lock_guard<std::mutex> lock(idMutex);
        std::string id;
        do {
            id = generateRandomID("app-", 10);
        } while (usedIDs.find(id) != usedIDs.end());
        usedIDs.insert(id);
        return id;
    }
    
    static std::string generateClusterID() {
        std::lock_guard<std::mutex> lock(idMutex);
        std::string id;
        do {
            id = generateRandomID("cls-", 10);
        } while (usedIDs.find(id) != usedIDs.end());
        usedIDs.insert(id);
        return id;
    }
    
    static std::string generateSnapshotID() {
        std::lock_guard<std::mutex> lock(idMutex);
        std::string id;
        do {
            id = generateRandomID("snap-", 12);
        } while (usedIDs.find(id) != usedIDs.end());
        usedIDs.insert(id);
        return id;
    }
    
    static void releaseID(const std::string& id) {
        std::lock_guard<std::mutex> lock(idMutex);
        usedIDs.erase(id);
    }
};


/**
 * @brief Resource naming that prevents conflicts and ensures isolation
 * 
 * Internal names (libvirt): thoth_user123_vm_i1a2b3c4d
 * User sees: my-web-server (display name) with ID i-1a2b3c4d
 */
class IsolatedResourceNaming {
private:
    static constexpr const char* NAMESPACE_PREFIX = "thoth_";
    
public:
    /**
     * Create internal VM name for libvirt
     * Format: thoth_<username>_vm_<resourceID>
     * Example: thoth_alice_vm_i1a2b3c4d
     */
    static std::string createInternalVMName(const std::string& username, 
                                            const std::string& resourceID) {
        return std::string(NAMESPACE_PREFIX) + sanitizeUsername(username) + 
               "_vm_" + resourceID;
    }
    
    /**
     * Create internal network name for libvirt
     * Format: thoth_<username>_net_<resourceID>
     */
    static std::string createInternalNetworkName(const std::string& username, 
                                                  const std::string& resourceID) {
        return std::string(NAMESPACE_PREFIX) + sanitizeUsername(username) + 
               "_net_" + resourceID;
    }
    
    /**
     * Create internal app name
     * Format: thoth_<username>_app_<resourceID>
     */
    static std::string createInternalAppName(const std::string& username, 
                                             const std::string& resourceID) {
        return std::string(NAMESPACE_PREFIX) + sanitizeUsername(username) + 
               "_app_" + resourceID;
    }
    
    /**
     * Create internal cluster name
     * Format: thoth_<username>_cls_<resourceID>
     */
    static std::string createInternalClusterName(const std::string& username, 
                                                  const std::string& resourceID) {
        return std::string(NAMESPACE_PREFIX) + sanitizeUsername(username) + 
               "_cls_" + resourceID;
    }
    
    /**
     * Extract username from internal name
     */
    static std::string extractUsername(const std::string& internalName) {
        if (internalName.find(NAMESPACE_PREFIX) != 0) {
            return "";
        }
        
        size_t start = strlen(NAMESPACE_PREFIX);
        size_t end = internalName.find("_vm_", start);
        if (end == std::string::npos) {
            end = internalName.find("_net_", start);
        }
        if (end == std::string::npos) {
            end = internalName.find("_app_", start);
        }
        if (end == std::string::npos) {
            end = internalName.find("_cls_", start);
        }
        
        if (end != std::string::npos) {
            return internalName.substr(start, end - start);
        }
        return "";
    }
    
    /**
     * Extract resource ID from internal name
     */
    static std::string extractResourceID(const std::string& internalName) {
        std::regex pattern("_(i-[a-z0-9]+|net-[a-z0-9]+|app-[a-z0-9]+|cls-[a-z0-9]+|snap-[a-z0-9]+)$");
        std::smatch match;
        
        if (std::regex_search(internalName, match, pattern)) {
            return match[1].str();
        }
        return "";
    }
    
    /**
     * Check if user owns resource
     */
    static bool isOwner(const std::string& internalName, const std::string& username) {
        std::string owner = extractUsername(internalName);
        return owner == username;
    }
    
private:
    static std::string sanitizeUsername(const std::string& username) {
        std::string clean;
        for (char c : username) {
            if (std::isalnum(c) || c == '_') {
                clean += std::tolower(c);
            }
        }
        return clean;
    }
};



/**
 * @brief Complete resource metadata management
 */
class ResourceMetadataStore {
private:
    struct ResourceMetadata {
        std::string resourceID;        // i-1a2b3c4d
        std::string internalName;      // thoth_alice_vm_i1a2b3c4d
        std::string displayName;       // my-web-server
        std::string owner;             // alice
        std::string resourceType;      // vm, network, app, cluster
        json tags;                     // User-defined tags
        time_t created;
        time_t modified;
        std::string status;            // running, stopped, etc.
        json additionalData;           // Type-specific data
    };
    
    std::mutex metadataMutex;
    std::map<std::string, ResourceMetadata> metadata;  // resourceID -> metadata
    std::string metadataFile;
    
    void loadMetadata() {
        std::ifstream file(metadataFile);
        if (file.is_open()) {
            try {
                json data;
                file >> data;
                for (const auto& item : data["resources"]) {
                    ResourceMetadata meta;
                    meta.resourceID = item["resourceID"];
                    meta.internalName = item["internalName"];
                    meta.displayName = item["displayName"];
                    meta.owner = item["owner"];
                    meta.resourceType = item["resourceType"];
                    meta.tags = item.value("tags", json::object());
                    meta.created = item["created"];
                    meta.modified = item.value("modified", meta.created);
                    meta.status = item.value("status", "unknown");
                    meta.additionalData = item.value("data", json::object());
                    
                    metadata[meta.resourceID] = meta;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error loading metadata: " << e.what() << std::endl;
            }
            file.close();
        }
    }
    
    bool saveMetadata() {
        std::lock_guard<std::mutex> lock(metadataMutex);
        
        json data;
        data["resources"] = json::array();
        
        for (const auto& [id, meta] : metadata) {
            data["resources"].push_back({
                {"resourceID", meta.resourceID},
                {"internalName", meta.internalName},
                {"displayName", meta.displayName},
                {"owner", meta.owner},
                {"resourceType", meta.resourceType},
                {"tags", meta.tags},
                {"created", meta.created},
                {"modified", meta.modified},
                {"status", meta.status},
                {"data", meta.additionalData}
            });
        }
        
        std::ofstream file(metadataFile);
        if (!file.is_open()) return false;
        
        file << data.dump(2);
        file.close();
        return true;
    }
    
public:
    ResourceMetadataStore(const std::string& file = "/var/lib/thoth-cloud/resources.json") 
        : metadataFile(file) {
        loadMetadata();
    }
    
    ~ResourceMetadataStore() {
        saveMetadata();
    }
    
    /**
     * Register a new resource
     */
    bool registerResource(const std::string& resourceID,
                         const std::string& internalName,
                         const std::string& displayName,
                         const std::string& owner,
                         const std::string& resourceType,
                         const json& additionalData = json::object()) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        
        ResourceMetadata meta;
        meta.resourceID = resourceID;
        meta.internalName = internalName;
        meta.displayName = displayName;
        meta.owner = owner;
        meta.resourceType = resourceType;
        meta.tags = json::object();
        meta.created = time(nullptr);
        meta.modified = meta.created;
        meta.status = "creating";
        meta.additionalData = additionalData;
        
        metadata[resourceID] = meta;
        return saveMetadata();
    }
    
    /**
     * Get all resources owned by user
     */
    json getUserResources(const std::string& username, const std::string& resourceType = "") {
        std::lock_guard<std::mutex> lock(metadataMutex);
        
        json result = json::array();
        for (const auto& [id, meta] : metadata) {
            if (meta.owner == username) {
                if (resourceType.empty() || meta.resourceType == resourceType) {
                    result.push_back({
                        {"id", meta.resourceID},
                        {"name", meta.displayName},
                        {"type", meta.resourceType},
                        {"status", meta.status},
                        {"created", meta.created},
                        {"tags", meta.tags}
                    });
                }
            }
        }
        return result;
    }
    
    /**
     * Get resource by ID (with ownership check)
     */
    json getResource(const std::string& resourceID, const std::string& username) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        
        auto it = metadata.find(resourceID);
        if (it == metadata.end()) {
            return {{"error", "Resource not found"}};
        }
        
        if (it->second.owner != username) {
            return {{"error", "Access denied"}};
        }
        
        return {
            {"id", it->second.resourceID},
            {"internalName", it->second.internalName},
            {"displayName", it->second.displayName},
            {"type", it->second.resourceType},
            {"status", it->second.status},
            {"created", it->second.created},
            {"modified", it->second.modified},
            {"tags", it->second.tags},
            {"data", it->second.additionalData}
        };
    }
    
    /**
     * Update resource metadata
     */
    bool updateResource(const std::string& resourceID, 
                       const std::string& username,
                       const json& updates) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        
        auto it = metadata.find(resourceID);
        if (it == metadata.end() || it->second.owner != username) {
            return false;
        }
        
        if (updates.contains("displayName")) {
            it->second.displayName = updates["displayName"];
        }
        if (updates.contains("status")) {
            it->second.status = updates["status"];
        }
        if (updates.contains("tags")) {
            it->second.tags = updates["tags"];
        }
        if (updates.contains("data")) {
            it->second.additionalData = updates["data"];
        }
        
        it->second.modified = time(nullptr);
        return saveMetadata();
    }
    
    /**
     * Delete resource
     */
    bool deleteResource(const std::string& resourceID, const std::string& username) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        
        auto it = metadata.find(resourceID);
        if (it == metadata.end() || it->second.owner != username) {
            return false;
        }
        
        metadata.erase(it);
        ResourceIDGenerator::releaseID(resourceID);
        return saveMetadata();
    }
    
    /**
     * Get internal name from resource ID
     */
    std::string getInternalName(const std::string& resourceID, const std::string& username) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        
        auto it = metadata.find(resourceID);
        if (it == metadata.end() || it->second.owner != username) {
            return "";
        }
        return it->second.internalName;
    }
    
    /**
     * Check if user owns resource
     */
    bool checkOwnership(const std::string& resourceID, const std::string& username) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        
        auto it = metadata.find(resourceID);
        return it != metadata.end() && it->second.owner == username;
    }
};

#endif // ISOLATION_UTILS_HPP