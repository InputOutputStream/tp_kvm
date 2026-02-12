#ifndef VALIDATION_HPP
#define VALIDATION_HPP

#include <string>
#include <regex>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;

namespace Validation {

// ==========================================
// VALIDATION RESULTS
// ==========================================

struct ValidationResult {
    bool valid;
    std::string error;
    std::vector<std::string> warnings;
    
    ValidationResult() : valid(true) {}
    ValidationResult(bool v, const std::string& e = "") : valid(v), error(e) {}
    
    void addWarning(const std::string& warning) {
        warnings.push_back(warning);
    }
    
    json toJson() const {
        json result;
        result["valid"] = valid;
        if (!error.empty()) {
            result["error"] = error;
        }
        if (!warnings.empty()) {
            result["warnings"] = warnings;
        }
        return result;
    }
};

// ==========================================
// RESOURCE LIMITS
// ==========================================

struct ResourceLimits {
    // Memory limits (in MB)
    static constexpr int MIN_MEMORY = 256;
    static constexpr int MAX_MEMORY = 65536;  // 64GB
    
    // CPU limits
    static constexpr int MIN_VCPUS = 1;
    static constexpr int MAX_VCPUS = 32;
    
    // Disk limits (in GB)
    static constexpr int MIN_DISK = 5;
    static constexpr int MAX_DISK = 1000;  // 1TB
    
    // Hostname constraints
    static constexpr int MIN_HOSTNAME_LENGTH = 1;
    static constexpr int MAX_HOSTNAME_LENGTH = 63;
    
    // Password constraints
    static constexpr int MIN_PASSWORD_LENGTH = 3;
    static constexpr int MAX_PASSWORD_LENGTH = 128;
};

// ==========================================
// VALIDATION FUNCTIONS
// ==========================================

class Validator {
public:
    // Hostname validation
    static ValidationResult validateHostname(const std::string& hostname);
    
    // Resource validation
    static ValidationResult validateMemory(int memory);
    static ValidationResult validateVCPUs(int vcpus);
    static ValidationResult validateDisk(int disk);
    
    // Docker container name validation
    static ValidationResult sanitizeDockerName(std::string name);

    // User validation
    static ValidationResult validateUsername(const std::string& username);
    static ValidationResult validatePassword(const std::string& password);
    static ValidationResult validateSSHKey(const std::string& sshKey);
    
    // File path validation
    static ValidationResult validateFilePath(const std::string& path, bool mustExist = false);
    static ValidationResult validateBaseImage(const std::string& imagePath);
    
    // Network validation
    static ValidationResult validateIPAddress(const std::string& ip);
    static ValidationResult validateMACAddress(const std::string& mac);
    
    // VM name validation (for existing VMs)
    static ValidationResult validateVMName(const std::string& name);
    
    // Complete deployment validation
    static ValidationResult validateDeploymentParams(const json& params);
    
    // Snapshot name validation
    static ValidationResult validateSnapshotName(const std::string& name);
    
    // Clone name validation
    static ValidationResult validateCloneName(const std::string& name, const std::string& originalName);
    
private:
    // Helper functions
    static bool isValidHostnameChar(char c);
    static bool containsSpecialChars(const std::string& str);
    static bool isReservedName(const std::string& name);
};

// ==========================================
// SYSTEM CHECKS
// ==========================================

class SystemValidator {
public:
    // Check if libvirt is connected and functional
    static ValidationResult checkLibvirtConnection(void* conn);
    
    // Check available system resources
    static ValidationResult checkAvailableResources(int requestedMemory, int requestedVCPUs);
    
    // Check disk space
    static ValidationResult checkDiskSpace(const std::string& path, long long requiredBytes);
    
    // Check if VM name already exists
    static ValidationResult checkVMNameAvailable(void* conn, const std::string& name);
    
    // Check network availability
    static ValidationResult checkNetworkAvailable(void* conn, const std::string& networkName);
    
    // Check base image exists and is valid
    static ValidationResult checkBaseImageValid(const std::string& imagePath);
    
    // Check required directories exist
    static ValidationResult checkRequiredDirectories();
    
    // Check required tools are installed
    static ValidationResult checkRequiredTools();
};

// ==========================================
// ERROR CODES
// ==========================================

enum class ErrorCode {
    // General
    SUCCESS = 0,
    UNKNOWN_ERROR = 1000,
    
    // Validation errors (2000-2999)
    INVALID_HOSTNAME = 2001,
    INVALID_MEMORY = 2002,
    INVALID_VCPUS = 2003,
    INVALID_DISK = 2004,
    INVALID_USERNAME = 2005,
    INVALID_PASSWORD = 2006,
    INVALID_SSH_KEY = 2007,
    INVALID_VM_NAME = 2008,
    
    // Resource errors (3000-3999)
    INSUFFICIENT_MEMORY = 3001,
    INSUFFICIENT_CPU = 3002,
    INSUFFICIENT_DISK = 3003,
    VM_NAME_EXISTS = 3004,
    
    // System errors (4000-4999)
    LIBVIRT_NOT_CONNECTED = 4001,
    BASE_IMAGE_NOT_FOUND = 4002,
    BASE_IMAGE_INVALID = 4003,
    DIRECTORY_NOT_FOUND = 4004,
    NETWORK_NOT_AVAILABLE = 4005,
    TOOL_NOT_INSTALLED = 4006,
    
    // Runtime errors (5000-5999)
    VM_NOT_FOUND = 5001,
    VM_ALREADY_RUNNING = 5002,
    VM_NOT_RUNNING = 5003,
    SNAPSHOT_NOT_FOUND = 5004,
    DISK_OPERATION_FAILED = 5005,
};

struct ErrorInfo {
    ErrorCode code;
    std::string message;
    std::string suggestion;
    
    ErrorInfo(ErrorCode c, const std::string& m, const std::string& s = "")
        : code(c), message(m), suggestion(s) {}
    
    json toJson() const {
        json result;
        result["code"] = static_cast<int>(code);
        result["message"] = message;
        if (!suggestion.empty()) {
            result["suggestion"] = suggestion;
        }
        return result;
    }
};

} // namespace Validation

#endif // VALIDATION_HPP