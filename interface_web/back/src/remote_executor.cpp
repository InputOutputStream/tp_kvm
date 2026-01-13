#include "../include/remote_executor.hpp"
#include "../include/utils.hpp"
#include <sstream>
#include <regex>
#include <unistd.h>
#include <sys/stat.h>

namespace RemoteExec {

RemoteExecutor::RemoteExecutor(virConnectPtr connection) : conn(connection) {
    isRemote = false;
    
    if (!conn) return;
    
    // Get connection URI to determine if it's remote
    char* uri = virConnectGetURI(conn);
    if (uri) {
        std::string uriStr(uri);
        free(uri);
        
        // Check if URI contains SSH (qemu+ssh://user@host/system)
        if (uriStr.find("qemu+ssh://") != std::string::npos) {
            isRemote = true;
            
            // Extract user and host from URI
            // Format: qemu+ssh://user@host/system or qemu+ssh://user@host/system?keyfile=/path
            std::regex uriRegex(R"(qemu\+ssh://([^@]+)@([^/?]+))");
            std::smatch matches;
            
            if (std::regex_search(uriStr, matches, uriRegex)) {
                remoteUser = matches[1].str();
                remoteHost = matches[2].str();
            }
            
            // Extract SSH key file if specified in URI
            std::regex keyRegex(R"(keyfile=([^&]+))");
            if (std::regex_search(uriStr, matches, keyRegex)) {
                sshKeyFile = matches[1].str();
            } else {
                // Try to find default SSH key
                sshKeyFile = findDefaultSSHKey();
            }
        }
    }
}

std::string RemoteExecutor::findDefaultSSHKey() const {
    // Check common SSH key locations
    const char* home = getenv("HOME");
    if (!home) {
        return "";
    }
    
    std::vector<std::string> possibleKeys = {
        std::string(home) + "/.ssh/thoth_kvm_key",
        std::string(home) + "/.ssh/id_rsa",
        std::string(home) + "/.ssh/id_ed25519",
    };
    
    for (const auto& keyPath : possibleKeys) {
        struct stat buffer;
        if (stat(keyPath.c_str(), &buffer) == 0) {
            return keyPath;
        }
    }
    
    return "";
}

std::string RemoteExecutor::buildSSHCommand(const std::string& command) const {
    if (!isRemote) {
        return command;
    }
    
    // Build SSH command with proper escaping and key authentication
    std::stringstream ssh;
    ssh << "ssh ";
    
    // Add SSH key if available
    if (!sshKeyFile.empty()) {
        ssh << "-i " << sshKeyFile << " ";
    }
    
    // SSH options for non-interactive use
    ssh << "-o StrictHostKeyChecking=no ";
    ssh << "-o UserKnownHostsFile=/dev/null ";
    ssh << "-o ConnectTimeout=10 ";
    ssh << "-o BatchMode=yes ";  // Fail if password is required
    ssh << "-o PasswordAuthentication=no ";  // Don't ask for password
    
    ssh << remoteUser << "@" << remoteHost << " ";
    ssh << "'" << command << "'";
    
    return ssh.str();
}

RemoteExecutor::ExecResult RemoteExecutor::execute(const std::string& command) const {
    ExecResult result;
    
    std::string fullCommand = buildSSHCommand(command);
    fullCommand += " 2>&1";  // Capture stderr too
    
    FILE* pipe = popen(fullCommand.c_str(), "r");
    if (!pipe) {
        result.exitCode = -1;
        result.output = "Failed to execute command";
        return result;
    }
    
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }
    
    result.exitCode = pclose(pipe);
    // pclose returns the exit status shifted left by 8 bits
    result.exitCode = WEXITSTATUS(result.exitCode);
    
    return result;
}

bool RemoteExecutor::fileExists(const std::string& path) const {
    std::string cmd = "test -f \"" + path + "\"";
    auto result = execute(cmd);
    return result.success();
}

bool RemoteExecutor::directoryExists(const std::string& path) const {
    std::string cmd = "test -d \"" + path + "\"";
    auto result = execute(cmd);
    return result.success();
}

long long RemoteExecutor::getAvailableDiskSpace(const std::string& path) const {
    // Use df to get available space in bytes
    std::string cmd = "df -B1 \"" + path + "\" 2>/dev/null | tail -1 | awk '{print $4}'";
    auto result = execute(cmd);
    
    if (!result.success() || result.output.empty()) {
        return -1;
    }
    
    try {
        // Remove trailing newline and parse
        std::string output = result.output;
        output.erase(output.find_last_not_of("\n\r") + 1);
        return std::stoll(output);
    } catch (...) {
        return -1;
    }
}

bool RemoteExecutor::commandExists(const std::string& command) const {
    std::string cmd = "which " + command + " > /dev/null 2>&1";
    auto result = execute(cmd);
    return result.success();
}

bool RemoteExecutor::isValidDiskImage(const std::string& imagePath) const {
    std::string cmd = "qemu-img info \"" + imagePath + "\" > /dev/null 2>&1";
    auto result = execute(cmd);
    return result.success();
}

std::string RemoteExecutor::getHostInfo() const {
    if (isRemote) {
        std::string keyInfo = sshKeyFile.empty() ? " [NO KEY]" : " [key: " + sshKeyFile + "]";
        return remoteUser + "@" + remoteHost + " (remote)" + keyInfo;
    }
    return "localhost (local)";
}

bool RemoteExecutor::testConnection() const {
    if (!isRemote) {
        return true;
    }
    
    auto result = execute("echo 'connection_test'");
    return result.success() && result.output.find("connection_test") != std::string::npos;
}

} // namespace RemoteExec