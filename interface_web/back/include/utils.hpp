#ifndef UTILS_HPP
#define UTILS_HPP

#include <iostream>
#include <string>
#include <regex>
#include <chrono>
#include <vector>
#include <sstream>
#include <iomanip>
#include <libvirt/libvirt.h>

// Get current time in milliseconds
long long getCurrentTimeMs();

// Execute shell command and return output
std::string execCommand(const std::string& cmd);

// Check if file exists
bool fileExists(const std::string& path);

std::vector<std::string> split(const std::string& str, char delimiter);

class VMNameManager {
protected:
    const std::string SEPARATOR = "__";
    const std::regex FORBIDDEN_CHARS = std::regex("[^a-zA-Z0-9-]");

private:   
    static const int MAX_LENGTH = 50;

    // Sanitize user input
    std::string sanitize(const std::string& input) {
        static const std::regex FORBIDDEN_CHARS("[^a-zA-Z0-9_-]");
        std::string clean = std::regex_replace(input, FORBIDDEN_CHARS, "");
        
        size_t pos;
        while ((pos = clean.find(SEPARATOR)) != std::string::npos) {
            clean.replace(pos, SEPARATOR.length(), "_");
        }
        
        if (clean.length() > MAX_LENGTH) {
            clean = clean.substr(0, MAX_LENGTH);
        }
        
        return clean;
    }
    
    // Generate unique timestamp
    std::string getTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();
        
        std::stringstream ss;
        ss << ms;
        return ss.str();
    }


public:
    // Create internal VM name from user input
    std::string createVMName(const std::string& username, const std::string& userVMName) {
        std::string cleanUser = sanitize(username);
        std::string cleanVMName = sanitize(userVMName);
        std::string timestamp = getTimestamp();
        
        // FIXED: Added timestamp, removed trailing separator
        return cleanUser + SEPARATOR + cleanVMName + SEPARATOR + timestamp;
    }
    
    // Parse internal name to get components
    struct VMNameInfo {
        std::string username;
        std::string vmName;
        std::string timestamp;
        bool valid;
    };
    
    VMNameInfo parseVMName(const std::string& internalName) {
        VMNameInfo info = {"", "", "", false};
        
        size_t pos1 = internalName.find(SEPARATOR);
        if (pos1 == std::string::npos) return info;
        
        size_t pos2 = internalName.find(SEPARATOR, pos1 + SEPARATOR.length());
        if (pos2 == std::string::npos) return info;
        
        info.username = internalName.substr(0, pos1);
        info.vmName = internalName.substr(pos1 + SEPARATOR.length(), 
                                          pos2 - pos1 - SEPARATOR.length());
        info.timestamp = internalName.substr(pos2 + SEPARATOR.length());
        info.valid = true;
        
        return info;
    }
    
    // Get display name for user (removes timestamp)
    std::string getDisplayName(const std::string& internalName) {
        VMNameInfo info = parseVMName(internalName);
        if (!info.valid) return internalName;
        return info.vmName;
    }
    
    // Check if user owns this VM
    bool isOwner(const std::string& internalName, const std::string& username) {
        VMNameInfo info = parseVMName(internalName);
        return info.valid && info.username == username;
    }
    
    // List VMs for a user (filter by username)
    static void filterUserVMs(
        const virDomainPtr *allVMs, 
        const std::string& username, 
        virDomainPtr **userDomains, 
        int numDomains, 
        int *numUserDomains, 
        std::string SEPARATOR = "__") { 
        
        std::string prefix = username + SEPARATOR;
        
        *userDomains = (virDomainPtr*)malloc(numDomains * sizeof(virDomainPtr));
        if (!*userDomains) {
            *numUserDomains = 0;
            return;
        }
        
        int j = 0;
        for(int i = 0; i < numDomains; i++) {
            const char* name = virDomainGetName(allVMs[i]);
            if (!name) continue;
            
            std::string vmName(name);
            if (vmName.find(prefix) == 0) {
                (*userDomains)[j] = allVMs[i];
                virDomainRef(allVMs[i]);
                j++;
            }
        }
        
        *numUserDomains = j;
        
        if (j < numDomains && j > 0) {
            *userDomains = (virDomainPtr*)realloc(*userDomains, j * sizeof(virDomainPtr));
        } else if (j == 0) {
            free(*userDomains);
            *userDomains = nullptr;
        }
    }

    // Validate user input
    bool isValidInput(const std::string& input) {
        if (input.empty() || input.length() > 50) return false;
        if (input.find(SEPARATOR) != std::string::npos) return false;
        return true;
    }
};

#endif // UTILS_HPP