// flavor_manager.hpp
#ifndef FLAVOR_MANAGER_HPP
#define FLAVOR_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include "json.hpp"

using json = nlohmann::json;

struct Flavor {
    std::string id;
    std::string name;
    std::string description;
    int vcpus;
    int ram;        // In MB
    int disk;       // In GB
    int price;      // In FCFA per month
    bool active;
};

class FlavorManager {
private:
    std::map<std::string, Flavor> flavors;
    std::string flavorsConfigFile;
    
    void loadDefaultFlavors();
    void loadFlavorsFromFile();
    bool saveFlavorsToFile();
    
public:
    FlavorManager();
    ~FlavorManager();
    
    // Flavor management
    json getAllFlavors(bool activeOnly = true);
    json getFlavor(const std::string& flavorId);
    bool addFlavor(const Flavor& flavor);
    bool updateFlavor(const std::string& flavorId, const Flavor& flavor);
    bool deleteFlavor(const std::string& flavorId);
    
    // Validation
    bool isValidFlavor(const std::string& flavorId);
    json getFlavorSpecs(const std::string& flavorId);
    
    // Price calculation
    int calculatePrice(const std::string& flavorId, int months = 1);
};

#endif // FLAVOR_MANAGER_HPP