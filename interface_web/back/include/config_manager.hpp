#ifndef CONFIG_MANAGER_HPP
#define CONFIG_MANAGER_HPP

#include <string>
#include <vector>
#include <map>

class ConfigManager {
private:
    std::map<std::string, std::string> config;
    std::string configFile;
    
    void loadEnv();
    
public:
    ConfigManager(const std::string& envFile = "/var/lib/thoth-cloud/.env");
    
    std::string get(const std::string& key, const std::string& defaultValue = "");
    int getInt(const std::string& key, int defaultValue = 0);
    std::vector<std::string> getList(const std::string& key);
};

#endif