#ifndef REMOTE_EXECUTOR_HPP
#define REMOTE_EXECUTOR_HPP

#include <string>
#include <libvirt/libvirt.h>

namespace RemoteExec {

/**
 * @brief RemoteExecutor handles execution of commands on remote libvirt hosts
 * 
 * When connected to a remote libvirt host via SSH (qemu+ssh://...),
 * this class provides utilities to execute commands, check files, and
 * verify system state on the remote host.
 */
class RemoteExecutor {
public:
    /**
     * @brief Result of a command execution
     */
    struct ExecResult {
        int exitCode;
        std::string output;
        
        bool success() const { return exitCode == 0; }
    };
    
    /**
     * @brief Constructor
     * @param connection Libvirt connection pointer
     */
    explicit RemoteExecutor(virConnectPtr connection);
    
    /**
     * @brief Execute a command (locally or remotely via SSH)
     * @param command Command to execute
     * @return Execution result with exit code and output
     */
    ExecResult execute(const std::string& command) const;
    
    /**
     * @brief Check if a file exists on the target host
     * @param path File path to check
     * @return true if file exists
     */
    bool fileExists(const std::string& path) const;
    
    /**
     * @brief Check if a directory exists on the target host
     * @param path Directory path to check
     * @return true if directory exists
     */
    bool directoryExists(const std::string& path) const;
    
    /**
     * @brief Get available disk space on the target host
     * @param path Path to check (e.g., /var/lib/libvirt/images)
     * @return Available bytes, or -1 on error
     */
    long long getAvailableDiskSpace(const std::string& path) const;
    
    /**
     * @brief Check if a command exists on the target host
     * @param command Command name (e.g., "qemu-img")
     * @return true if command is available
     */
    bool commandExists(const std::string& command) const;
    
    /**
     * @brief Validate if a disk image is valid (using qemu-img info)
     * @param imagePath Path to disk image
     * @return true if valid
     */
    bool isValidDiskImage(const std::string& imagePath) const;
    
    /**
     * @brief Get information about the target host
     * @return String describing the target (local or remote)
     */
    std::string getHostInfo() const;
    
    /**
     * @brief Test if SSH connection works
     * @return true if connection is functional
     */
    bool testConnection() const;
    
    /**
     * @brief Check if executor is for remote connection
     * @return true if remote, false if local
     */
    bool isRemoteConnection() const { return isRemote; }
    
    /**
     * @brief Get SSH key file path being used
     * @return SSH key file path or empty string
     */
    std::string getSSHKeyFile() const { return sshKeyFile; }
    
private:
    virConnectPtr conn;
    bool isRemote;
    std::string remoteUser;
    std::string remoteHost;
    std::string sshKeyFile;  // NEW: SSH key file path
    
    /**
     * @brief Build SSH command with proper authentication
     * @param command Command to execute remotely
     * @return Full SSH command string
     */
    std::string buildSSHCommand(const std::string& command) const;
    
    /**
     * @brief Find default SSH key in common locations
     * @return Path to SSH key or empty string
     */
    std::string findDefaultSSHKey() const;
};

} // namespace RemoteExec

#endif // REMOTE_EXECUTOR_HPP