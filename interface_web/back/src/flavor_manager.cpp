#include "../include/flavor_manager.hpp"
#include <fstream>
#include <sys/stat.h>
#include <iostream>

FlavorManager::FlavorManager() 
    : flavorsConfigFile("/var/lib/thoth-cloud/flavors.json") {
    loadFlavorsFromFile();
    
    // If no flavors exist, load defaults
    if (flavors.empty()) {
        loadDefaultFlavors();
        saveFlavorsToFile();
    }
}

FlavorManager::~FlavorManager() {
    saveFlavorsToFile();
}

void FlavorManager::loadDefaultFlavors() {
    std::vector<Flavor> defaultFlavors = {
        {"nano", "Nano", "Entry level - Testing & Development", 1, 512, 5, 800, true},
        {"micro", "Micro", "Micro instances - Small apps", 1, 1024, 10, 1500, true},
        {"small", "Small", "Small instances - Light workloads", 1, 2048, 15, 3500, true},
        {"medium", "Medium", "Medium instances - Standard workloads", 2, 4096, 20, 6500, true},
        {"large", "Large", "Large instances - Heavy workloads", 2, 8192, 40, 12000, true},
        {"xlarge", "X-Large", "Extra large - Database & Analytics", 4, 16384, 80, 22000, true},
        {"2xlarge", "2X-Large", "Very large - High performance", 8, 32768, 160, 42000, true},
        {"cpu-optimized", "CPU Optimized", "CPU intensive workloads", 4, 4096, 40, 15000, true},
        {"mem-optimized", "Memory Optimized", "Memory intensive workloads", 2, 16384, 40, 18000, true}
    };
    
    for (const auto& flavor : defaultFlavors) {
        flavors[flavor.id] = flavor;
    }
}

void FlavorManager::loadFlavorsFromFile() {
    std::ifstream file(flavorsConfigFile);
    if (!file.is_open()) {
        return;
    }
    
    try {
        json data;
        file >> data;
        
        for (const auto& item : data["flavors"]) {
            Flavor flavor;
            flavor.id = item["id"];
            flavor.name = item["name"];
            flavor.description = item["description"];
            flavor.vcpus = item["vcpus"];
            flavor.ram = item["ram"];
            flavor.disk = item["disk"];
            flavor.price = item["price"];
            flavor.active = item.value("active", true);
            
            flavors[flavor.id] = flavor;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading flavors: " << e.what() << std::endl;
    }
    
    file.close();
}

bool FlavorManager::saveFlavorsToFile() {
    mkdir("/var/lib/thoth-cloud", 0755);
    
    json data;
    data["flavors"] = json::array();
    
    for (const auto& [id, flavor] : flavors) {
        data["flavors"].push_back({
            {"id", flavor.id},
            {"name", flavor.name},
            {"description", flavor.description},
            {"vcpus", flavor.vcpus},
            {"ram", flavor.ram},
            {"disk", flavor.disk},
            {"price", flavor.price},
            {"active", flavor.active}
        });
    }
    
    std::ofstream file(flavorsConfigFile);
    if (!file.is_open()) {
        return false;
    }
    
    file << data.dump(2);
    file.close();
    return true;
}

json FlavorManager::getAllFlavors(bool activeOnly) {
    json result;
    result["success"] = true;
    result["flavors"] = json::array();
    
    for (const auto& [id, flavor] : flavors) {
        if (activeOnly && !flavor.active) {
            continue;
        }
        
        result["flavors"].push_back({
            {"id", flavor.id},
            {"name", flavor.name},
            {"description", flavor.description},
            {"specs", {
                {"vcpus", flavor.vcpus},
                {"ram", flavor.ram},
                {"disk", flavor.disk}
            }},
            {"price", flavor.price},
            {"priceFormatted", std::to_string(flavor.price) + " FCFA/mois"},
            {"active", flavor.active}
        });
    }
    
    return result;
}

json FlavorManager::getFlavor(const std::string& flavorId) {
    json result;
    result["success"] = false;
    
    auto it = flavors.find(flavorId);
    if (it == flavors.end()) {
        result["error"] = "Flavor not found";
        return result;
    }
    
    const Flavor& flavor = it->second;
    
    result["success"] = true;
    result["flavor"] = {
        {"id", flavor.id},
        {"name", flavor.name},
        {"description", flavor.description},
        {"specs", {
            {"vcpus", flavor.vcpus},
            {"ram", flavor.ram},
            {"disk", flavor.disk}
        }},
        {"price", flavor.price},
        {"active", flavor.active}
    };
    
    return result;
}

bool FlavorManager::isValidFlavor(const std::string& flavorId) {
    auto it = flavors.find(flavorId);
    return it != flavors.end() && it->second.active;
}

json FlavorManager::getFlavorSpecs(const std::string& flavorId) {
    json result;
    result["success"] = false;
    
    auto it = flavors.find(flavorId);
    if (it == flavors.end()) {
        result["error"] = "Flavor not found";
        return result;
    }
    
    result["success"] = true;
    result["vcpus"] = it->second.vcpus;
    result["ram"] = it->second.ram;
    result["disk"] = it->second.disk;
    
    return result;
}

int FlavorManager::calculatePrice(const std::string& flavorId, int months) {
    auto it = flavors.find(flavorId);
    if (it == flavors.end()) {
        return 0;
    }
    
    return it->second.price * months;
}

bool FlavorManager::addFlavor(const Flavor& flavor) {
    if (flavors.find(flavor.id) != flavors.end()) {
        return false; // Already exists
    }
    
    flavors[flavor.id] = flavor;
    return saveFlavorsToFile();
}

bool FlavorManager::updateFlavor(const std::string& flavorId, const Flavor& flavor) {
    if (flavors.find(flavorId) == flavors.end()) {
        return false;
    }
    
    flavors[flavorId] = flavor;
    return saveFlavorsToFile();
}

bool FlavorManager::deleteFlavor(const std::string& flavorId) {
    auto it = flavors.find(flavorId);
    if (it == flavors.end()) {
        return false;
    }
    
    // Don't actually delete, just mark as inactive
    it->second.active = false;
    return saveFlavorsToFile();
}