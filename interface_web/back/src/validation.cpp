#include "../include/validation.hpp"
#include <libvirt/libvirt.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <cctype>
#include <algorithm>
#include <set>

namespace Validation {

// ==========================================
// HOSTNAME VALIDATION
// ==========================================

bool Validator::isValidHostnameChar(char c) {
    return std::isalnum(c) || c == '-' || c == '.';
}

ValidationResult Validator::validateHostname(const std::string& hostname) {
    ValidationResult result;
    
    // Check length
    if (hostname.length() < ResourceLimits::MIN_HOSTNAME_LENGTH) {
        result.valid = false;
        result.error = "Hostname is too short (minimum " + 
                      std::to_string(ResourceLimits::MIN_HOSTNAME_LENGTH) + " characters)";
        return result;
    }
    
    if (hostname.length() > ResourceLimits::MAX_HOSTNAME_LENGTH) {
        result.valid = false;
        result.error = "Hostname is too long (maximum " + 
                      std::to_string(ResourceLimits::MAX_HOSTNAME_LENGTH) + " characters)";
        return result;
    }
    
    // Check for invalid characters
    for (char c : hostname) {
        if (!isValidHostnameChar(c)) {
            result.valid = false;
            result.error = "Hostname contains invalid character: '" + std::string(1, c) + 
                          "'. Only alphanumeric, hyphen, and dot are allowed.";
            return result;
        }
    }
    
    // Cannot start or end with hyphen
    if (hostname.front() == '-' || hostname.back() == '-') {
        result.valid = false;
        result.error = "Hostname cannot start or end with a hyphen";
        return result;
    }
    
    // Cannot start with a dot
    if (hostname.front() == '.') {
        result.valid = false;
        result.error = "Hostname cannot start with a dot";
        return result;
    }
    
    // Check for reserved names
    if (isReservedName(hostname)) {
        result.valid = false;
        result.error = "Hostname '" + hostname + "' is a reserved name";
        return result;
    }
    
    return result;
}

bool Validator::isReservedName(const std::string& name) {
    static const std::set<std::string> reserved = {
        "localhost", "default", "template", "test", "example"
    };
    return reserved.find(name) != reserved.end();
}

// ==========================================
// RESOURCE VALIDATION
// ==========================================

ValidationResult Validator::validateMemory(int memory) {
    ValidationResult result;
    
    if (memory < ResourceLimits::MIN_MEMORY) {
        result.valid = false;
        result.error = "Memory is too low (minimum " + 
                      std::to_string(ResourceLimits::MIN_MEMORY) + " MB)";
        return result;
    }
    
    if (memory > ResourceLimits::MAX_MEMORY) {
        result.valid = false;
        result.error = "Memory is too high (maximum " + 
                      std::to_string(ResourceLimits::MAX_MEMORY) + " MB)";
        return result;
    }
    
    // Warn if not a power of 2 or multiple of 512
    if (memory % 512 != 0) {
        result.addWarning("Memory is not a multiple of 512 MB. "
                         "It's recommended to use values like 512, 1024, 2048, etc.");
    }
    
    return result;
}

ValidationResult Validator::validateVCPUs(int vcpus) {
    ValidationResult result;
    
    if (vcpus < ResourceLimits::MIN_VCPUS) {
        result.valid = false;
        result.error = "vCPUs is too low (minimum " + 
                      std::to_string(ResourceLimits::MIN_VCPUS) + ")";
        return result;
    }
    
    if (vcpus > ResourceLimits::MAX_VCPUS) {
        result.valid = false;
        result.error = "vCPUs is too high (maximum " + 
                      std::to_string(ResourceLimits::MAX_VCPUS) + ")";
        return result;
    }
    
    // Get system CPU count
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs > 0 && vcpus > nprocs) {
        result.addWarning("Requested vCPUs (" + std::to_string(vcpus) + 
                         ") exceeds available physical CPUs (" + 
                         std::to_string(nprocs) + "). This may affect performance.");
    }
    
    return result;
}

ValidationResult Validator::validateDisk(int disk) {
    ValidationResult result;
    
    if (disk < ResourceLimits::MIN_DISK) {
        result.valid = false;
        result.error = "Disk is too small (minimum " + 
                      std::to_string(ResourceLimits::MIN_DISK) + " GB)";
        return result;
    }
    
    if (disk > ResourceLimits::MAX_DISK) {
        result.valid = false;
        result.error = "Disk is too large (maximum " + 
                      std::to_string(ResourceLimits::MAX_DISK) + " GB)";
        return result;
    }
    
    return result;
}

// ==========================================
// USER VALIDATION
// ==========================================

ValidationResult Validator::validateUsername(const std::string& username) {
    ValidationResult result;
    
    if (username.empty()) {
        result.valid = false;
        result.error = "Username cannot be empty";
        return result;
    }
    
    if (username.length() > 32) {
        result.valid = false;
        result.error = "Username is too long (maximum 32 characters)";
        return result;
    }
    
    // Must start with letter
    if (!std::isalpha(username.front())) {
        result.valid = false;
        result.error = "Username must start with a letter";
        return result;
    }
    
    // Only lowercase letters, numbers, and underscore
    for (char c : username) {
        if (!std::islower(c) && !std::isdigit(c) && c != '_') {
            result.valid = false;
            result.error = "Username can only contain lowercase letters, numbers, and underscore";
            return result;
        }
    }
    
    // Check for reserved usernames
    static const std::set<std::string> reserved = {
        "root", "admin", "administrator", "daemon", "bin", "sys"
    };
    if (reserved.find(username) != reserved.end()) {
        result.valid = false;
        result.error = "Username '" + username + "' is reserved";
        return result;
    }
    
    return result;
}

ValidationResult Validator::validatePassword(const std::string& password) {
    ValidationResult result;
    
    if (password.length() < ResourceLimits::MIN_PASSWORD_LENGTH) {
        result.valid = false;
        result.error = "Password is too short (minimum " + 
                      std::to_string(ResourceLimits::MIN_PASSWORD_LENGTH) + " characters)";
        return result;
    }
    
    if (password.length() > ResourceLimits::MAX_PASSWORD_LENGTH) {
        result.valid = false;
        result.error = "Password is too long (maximum " + 
                      std::to_string(ResourceLimits::MAX_PASSWORD_LENGTH) + " characters)";
        return result;
    }
    
    // Check password strength
    bool hasLower = false, hasUpper = false, hasDigit = false, hasSpecial = false;
    
    for (char c : password) {
        if (std::islower(c)) hasLower = true;
        else if (std::isupper(c)) hasUpper = true;
        else if (std::isdigit(c)) hasDigit = true;
        else hasSpecial = true;
    }
    
    int strength = hasLower + hasUpper + hasDigit + hasSpecial;
    
    if (strength < 2) {
        result.addWarning("Password is weak. Consider using a mix of uppercase, "
                         "lowercase, numbers, and special characters.");
    }
    
    return result;
}

ValidationResult Validator::validateSSHKey(const std::string& sshKey) {
    ValidationResult result;
    
    if (sshKey.empty()) {
        result.valid = false;
        result.error = "SSH key cannot be empty";
        return result;
    }
    
    // Check for valid SSH key format (basic check)
    bool validFormat = false;
    std::vector<std::string> validPrefixes = {
        "ssh-rsa", "ssh-dss", "ssh-ed25519", "ecdsa-sha2-nistp256",
        "ecdsa-sha2-nistp384", "ecdsa-sha2-nistp521"
    };
    
    for (const auto& prefix : validPrefixes) {
        if (sshKey.find(prefix) == 0) {
            validFormat = true;
            break;
        }
    }
    
    if (!validFormat) {
        result.valid = false;
        result.error = "Invalid SSH key format. Key must start with ssh-rsa, ssh-ed25519, etc.";
        return result;
    }
    
    // Warn if key looks suspicious
    if (sshKey.length() < 100) {
        result.addWarning("SSH key seems unusually short. Make sure it's a complete public key.");
    }
    
    return result;
}

// ==========================================
// FILE PATH VALIDATION
// ==========================================

ValidationResult Validator::validateFilePath(const std::string& path, bool mustExist) {
    ValidationResult result;
    
    if (path.empty()) {
        result.valid = false;
        result.error = "File path cannot be empty";
        return result;
    }
    
    // Check for path traversal attempts
    if (path.find("..") != std::string::npos) {
        result.valid = false;
        result.error = "Path traversal detected (.. not allowed)";
        return result;
    }
    
    if (mustExist) {
        struct stat buffer;
        if (stat(path.c_str(), &buffer) != 0) {
            result.valid = false;
            result.error = "File does not exist: " + path;
            return result;
        }
    }
    
    return result;
}

ValidationResult Validator::validateBaseImage(const std::string& imagePath) {
    ValidationResult result = validateFilePath(imagePath, true);
    
    if (!result.valid) {
        return result;
    }
    
    // Check if it's a valid disk image (basic check)
    std::string checkCmd = "qemu-img info \"" + imagePath + "\" > /dev/null 2>&1";
    int exitCode = system(checkCmd.c_str());
    
    if (exitCode != 0) {
        result.valid = false;
        result.error = "Invalid or corrupted disk image: " + imagePath;
        return result;
    }
    
    return result;
}

// ==========================================
// COMPLETE DEPLOYMENT VALIDATION
// ==========================================

ValidationResult Validator::validateDeploymentParams(const json& params) {
    ValidationResult result;
    
    // Check required fields
    std::vector<std::string> requiredFields = {
        "hostname", "memory", "vcpus", "disk", "username", "authMethod"
    };
    
    for (const auto& field : requiredFields) {
        if (!params.contains(field)) {
            result.valid = false;
            result.error = "Missing required field: " + field;
            return result;
        }
    }
    
    // Validate hostname
    auto hostnameResult = validateHostname(params["hostname"]);
    if (!hostnameResult.valid) return hostnameResult;
    result.warnings.insert(result.warnings.end(), 
                          hostnameResult.warnings.begin(), 
                          hostnameResult.warnings.end());
    
    // Validate memory
    auto memoryResult = validateMemory(params["memory"]);
    if (!memoryResult.valid) return memoryResult;
    result.warnings.insert(result.warnings.end(), 
                          memoryResult.warnings.begin(), 
                          memoryResult.warnings.end());
    
    // Validate vCPUs
    auto vcpusResult = validateVCPUs(params["vcpus"]);
    if (!vcpusResult.valid) return vcpusResult;
    result.warnings.insert(result.warnings.end(), 
                          vcpusResult.warnings.begin(), 
                          vcpusResult.warnings.end());
    
    // Validate disk
    auto diskResult = validateDisk(params["disk"]);
    if (!diskResult.valid) return diskResult;
    
    // Validate username
    auto usernameResult = validateUsername(params["username"]);
    if (!usernameResult.valid) return usernameResult;
    
    // Validate auth method
    std::string authMethod = params["authMethod"];
    if (authMethod != "password" && authMethod != "ssh-key") {
        result.valid = false;
        result.error = "Invalid authMethod. Must be 'password' or 'ssh-key'";
        return result;
    }
    
    // Validate password or SSH key
    if (authMethod == "password") {
        if (!params.contains("password")) {
            result.valid = false;
            result.error = "Password is required when authMethod is 'password'";
            return result;
        }
        auto passwordResult = validatePassword(params["password"]);
        if (!passwordResult.valid) return passwordResult;
        result.warnings.insert(result.warnings.end(), 
                              passwordResult.warnings.begin(), 
                              passwordResult.warnings.end());
    } else {
        if (!params.contains("sshKey")) {
            result.valid = false;
            result.error = "SSH key is required when authMethod is 'ssh-key'";
            return result;
        }
        auto sshKeyResult = validateSSHKey(params["sshKey"]);
        if (!sshKeyResult.valid) return sshKeyResult;
        result.warnings.insert(result.warnings.end(), 
                              sshKeyResult.warnings.begin(), 
                              sshKeyResult.warnings.end());
    }
    
    return result;
}

// ==========================================
// SYSTEM VALIDATION
// ==========================================

ValidationResult SystemValidator::checkLibvirtConnection(void* conn) {
    ValidationResult result;
    
    if (!conn) {
        result.valid = false;
        result.error = "Not connected to libvirt";
        return result;
    }
    
    virConnectPtr connection = static_cast<virConnectPtr>(conn);
    
    // Try to get hostname to verify connection
    char* hostname = virConnectGetHostname(connection);
    if (!hostname) {
        result.valid = false;
        result.error = "Libvirt connection is not functional";
        return result;
    }
    
    free(hostname);
    return result;
}

ValidationResult SystemValidator::checkVMNameAvailable(void* conn, const std::string& name) {
    ValidationResult result;
    
    if (!conn) {
        result.valid = false;
        result.error = "Not connected to libvirt";
        return result;
    }
    
    virConnectPtr connection = static_cast<virConnectPtr>(conn);
    virDomainPtr domain = virDomainLookupByName(connection, name.c_str());
    
    if (domain) {
        virDomainFree(domain);
        result.valid = false;
        result.error = "VM with name '" + name + "' already exists on the libvirt host";
        return result;
    }
    
    return result;
}

ValidationResult SystemValidator::checkRequiredDirectories() {
    ValidationResult result;
    
    // This will be checked by RemoteExecutor in vm_operations
    // We'll do a basic warning here
    result.addWarning("Directory existence will be verified on the target libvirt host during deployment");
    
    return result;
}

ValidationResult SystemValidator::checkBaseImageValid(const std::string& imagePath) {
    ValidationResult result;
    
    // This will be checked by RemoteExecutor in vm_operations
    // Basic validation here
    if (imagePath.empty()) {
        result.valid = false;
        result.error = "Base image path cannot be empty";
        return result;
    }
    
    return result;
}

ValidationResult SystemValidator::checkDiskSpace(const std::string& path, long long requiredBytes) {
    ValidationResult result;
    
    // This will be checked by RemoteExecutor in vm_operations
    // Add a warning for now
    result.addWarning("Disk space will be verified on the target libvirt host during deployment");
    
    return result;
}

ValidationResult SystemValidator::checkRequiredTools() {
    ValidationResult result;
    
    // This will be checked by RemoteExecutor in vm_operations
    result.addWarning("Required tools will be verified on the target libvirt host during deployment");
    
    return result;
}

ValidationResult SystemValidator::checkNetworkAvailable(void* conn, const std::string& networkName) {
    ValidationResult result;
    
    if (!conn) {
        result.valid = false;
        result.error = "Not connected to libvirt";
        return result;
    }
    
    virConnectPtr connection = static_cast<virConnectPtr>(conn);
    virNetworkPtr network = virNetworkLookupByName(connection, networkName.c_str());
    
    if (!network) {
        result.valid = false;
        result.error = "Network '" + networkName + "' does not exist on the libvirt host";
        result.error += "\n\nOn the libvirt host, start the network:";
        result.error += "\n  sudo virsh net-start " + networkName;
        result.error += "\n  sudo virsh net-autostart " + networkName;
        return result;
    }
    
    // Check if network is active
    int active = virNetworkIsActive(network);
    virNetworkFree(network);
    
    if (active != 1) {
        result.valid = false;
        result.error = "Network '" + networkName + "' exists but is not active on the libvirt host";
        result.error += "\n\nOn the libvirt host, start the network:";
        result.error += "\n  sudo virsh net-start " + networkName;
        return result;
    }
    
    return result;
}

} // namespace Validation
