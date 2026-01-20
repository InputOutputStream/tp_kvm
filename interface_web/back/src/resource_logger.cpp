#include "../include/resource_logger.hpp"
#include <sys/stat.h>
#include <ctime>
#include <iomanip>

ResourceLogger::ResourceLogger() : logDir("/var/log/thoth-cloud") {
    mkdir(logDir.c_str(), 0755);
}

std::string ResourceLogger::getTimestamp() {
    auto now = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string ResourceLogger::getLevelString(LogLevel level) {
    switch(level) {
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

void ResourceLogger::logDeployment(const std::string& vmName, const std::string& user,
                                  const json& resources, bool success, const std::string& error) {
    std::ofstream log(logDir + "/deployments.log", std::ios::app);
    
    json entry = {
        {"timestamp", getTimestamp()},
        {"type", "deployment"},
        {"vmName", vmName},
        {"user", user},
        {"resources", resources},
        {"success", success}
    };
    
    if (!success) {
        entry["error"] = error;
    }
    
    log << entry.dump() << "\n";
}

void ResourceLogger::logResourceCheck(const json& required, const json& available, bool sufficient) {
    std::ofstream log(logDir + "/resources.log", std::ios::app);
    
    json entry = {
        {"timestamp", getTimestamp()},
        {"type", "resource_check"},
        {"required", required},
        {"available", available},
        {"sufficient", sufficient}
    };
    
    log << entry.dump() << "\n";
}

void ResourceLogger::logUserAction(const std::string& user, const std::string& action,
                                  const std::string& target, bool success) {
    std::ofstream log(logDir + "/user_actions.log", std::ios::app);
    
    json entry = {
        {"timestamp", getTimestamp()},
        {"user", user},
        {"action", action},
        {"target", target},
        {"success", success}
    };
    
    log << entry.dump() << "\n";
}

void ResourceLogger::logSystemEvent(LogLevel level, const std::string& message) {
    std::ofstream log(logDir + "/system.log", std::ios::app);
    
    log << "[" << getTimestamp() << "] "
        << getLevelString(level) << ": "
        << message << "\n";
}