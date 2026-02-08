#ifndef RESOURCE_LOGGER_HPP
#define RESOURCE_LOGGER_HPP

#include "json.hpp"
#include <string>
#include <fstream>

using json = nlohmann::json;

enum class LogLevel {
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

class ResourceLogger {
private:
    std::string logDir;
    
    std::string getLevelString(LogLevel level);
    std::string getTimestamp();
    
public:
    ResourceLogger();
    
    void logDeployment(const std::string& vmName, const std::string& user, 
                      const json& resources, bool success, const std::string& error = "");
    void logResourceCheck(const json& required, const json& available, bool sufficient);
    void logUserAction(const std::string& user, const std::string& action, 
                      const std::string& target, bool success);
    void logSystemEvent(LogLevel level, const std::string& message);
    
    json getRecentLogs(int count = 100);
    json getUserLogs(const std::string& username, int count = 50);
};

#endif