#include "../include/config_manager.hpp"
#include <fstream>
#include <vector>
#include <sstream>

ConfigManager::ConfigManager(const std::string& envFile) : configFile(envFile) {
    loadEnv();
}

void ConfigManager::loadEnv() {
    std::ifstream file(configFile);
    if (!file.is_open()) return;
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        config[key] = value;
    }
}

std::string ConfigManager::get(const std::string& key, const std::string& defaultValue) {
    auto it = config.find(key);
    return (it != config.end()) ? it->second : defaultValue;
}

int ConfigManager::getInt(const std::string& key, int defaultValue) {
    auto it = config.find(key);
    return (it != config.end()) ? std::stoi(it->second) : defaultValue;
}

std::vector<std::string> ConfigManager::getList(const std::string& key) {
    std::vector<std::string> result;
    std::string value = get(key);
    
    std::stringstream ss(value);
    std::string item;
    
    while (std::getline(ss, item, ',')) {
        result.push_back(item);
    }
    
    return result;
}