#include "../include/baseimage_manager.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <regex>

BaseImageManager::BaseImageManager(RemoteExec::RemoteExecutor* executor, const std::string& imageDir)
    : baseImageDir(imageDir), remoteExec(executor) {
    scanBaseImages();
}

BaseImageManager::~BaseImageManager() {}

std::string BaseImageManager::extractOSName(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Common OS patterns
    if (lower.find("ubuntu") != std::string::npos) return "Ubuntu";
    if (lower.find("debian") != std::string::npos) return "Debian";
    if (lower.find("centos") != std::string::npos) return "CentOS";
    if (lower.find("fedora") != std::string::npos) return "Fedora";
    if (lower.find("rocky") != std::string::npos) return "Rocky Linux";
    if (lower.find("alma") != std::string::npos) return "AlmaLinux";
    if (lower.find("arch") != std::string::npos) return "Arch Linux";
    if (lower.find("opensuse") != std::string::npos) return "openSUSE";
    if (lower.find("alpine") != std::string::npos) return "Alpine Linux";
    
    return "Unknown";
}

std::string BaseImageManager::extractVersion(const std::string& filename) {
    // Try to extract version number (e.g., 22.04, 20.04, 11, etc.)
    std::regex versionRegex(R"((\d+\.?\d*))");
    std::smatch match;
    
    if (std::regex_search(filename, match, versionRegex)) {
        return match[1].str();
    }
    
    return "latest";
}

std::string BaseImageManager::extractArchitecture(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower.find("amd64") != std::string::npos || lower.find("x86_64") != std::string::npos) {
        return "amd64";
    }
    if (lower.find("arm64") != std::string::npos || lower.find("aarch64") != std::string::npos) {
        return "arm64";
    }
    if (lower.find("i386") != std::string::npos || lower.find("i686") != std::string::npos) {
        return "i386";
    }
    
    return "amd64"; // Default assumption
}


BaseImage BaseImageManager::parseImageInfo(const std::string& filename, const std::string& fullPath) {
    BaseImage img;
    img.filename = filename;
    img.path = fullPath;
    img.os = extractOSName(filename);
    img.version = extractVersion(filename);
    img.architecture = extractArchitecture(filename);
    img.size = getFileSize(fullPath);
    
    // Generate ID from filename (remove extension)
    img.id = filename;
    size_t dotPos = img.id.find_last_of('.');
    if (dotPos != std::string::npos) {
        img.id = img.id.substr(0, dotPos);
    }
    
    // Generate display name
    img.displayName = img.os + " " + img.version + " (" + img.architecture + ")";
    
    // Check if image is valid using qemu-img
    if (remoteExec) {
        img.available = remoteExec->isValidDiskImage(fullPath);
    } else {
        img.available = false;
    }
    
    return img;
}

void BaseImageManager::scanBaseImages() {
    images.clear();
    
    if (!remoteExec) {
        std::cerr << "No remote executor available" << std::endl;
        return;
    }
    
    // Check if directory exists
    if (!remoteExec->directoryExists(baseImageDir)) {
        std::cerr << "Base image directory does not exist: " << baseImageDir << std::endl;
        return;
    }
    
    // List all image files in directory
    std::string listCmd = "find " + baseImageDir + " -maxdepth 1 -type f \\( "
                         "-name '*.img' -o -name '*.qcow2' -o -name '*.qcow' "
                         "-o -name '*.raw' \\) 2>/dev/null";
    
    auto result = remoteExec->execute(listCmd);
    
    if (!result.success()) {
        std::cerr << "Failed to list base images" << std::endl;
        return;
    }
    
    // Parse output
    std::istringstream stream(result.output);
    std::string line;
    
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        
        // Extract filename from full path
        size_t lastSlash = line.find_last_of('/');
        std::string filename = (lastSlash != std::string::npos) 
                              ? line.substr(lastSlash + 1) 
                              : line;
        
        BaseImage img = parseImageInfo(filename, line);
        images[img.id] = img;
    }   
}

void BaseImageManager::refresh() {
    scanBaseImages();
}

json BaseImageManager::listImages() {
    json result;
    result["success"] = true;
    result["images"] = json::array();
    result["baseImageDir"] = baseImageDir;
    
    for (const auto& [id, img] : images) {
        if (!img.available) continue; // Only list valid images
        
        result["images"].push_back({
            {"id", img.id},
            {"displayName", img.displayName},
            {"os", img.os},
            {"version", img.version},
            {"architecture", img.architecture},
            {"filename", img.filename},
            {"size", img.size},
            {"sizeFormatted", std::to_string(img.size / (1024*1024)) + " MB"}
        });
    }
    
    result["count"] = result["images"].size();
    
    return result;
}

bool BaseImageManager::isValidPath(const std::string& path) {
    // Prevent path traversal
    if (path.find("..") != std::string::npos) return false;
    if (path.find("~") != std::string::npos) return false;
    return path.find(baseImageDir) == 0;
}

// Fix getFileSize with better error handling
long long BaseImageManager::getFileSize(const std::string& path) {
    if (!isValidPath(path)) {
        std::cerr << "Invalid path detected: " << path << std::endl;
        return -1;
    }
    
    if (!remoteExec) return -1;
    
    std::string cmd = "stat -c%s \"" + path + "\" 2>/dev/null";
    auto result = remoteExec->execute(cmd);
    
    if (!result.success() || result.output.empty()) {
        return -1;
    }
    
    try {
        std::string output = result.output;
        output.erase(std::remove_if(output.begin(), output.end(), ::isspace), 
                    output.end());
        return std::stoll(output);
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse file size: " << e.what() << std::endl;
        return -1;
    }
}

json BaseImageManager::getImage(const std::string& imageId) {
    json result;
    result["success"] = false;
    
    auto it = images.find(imageId);
    if (it == images.end()) {
        result["error"] = "Image not found";
        return result;
    }
    
    const BaseImage& img = it->second;
    
    if (!img.available) {
        result["error"] = "Image is not available or invalid";
        return result;
    }
    
    result["success"] = true;
    result["image"] = {
        {"id", img.id},
        {"displayName", img.displayName},
        {"os", img.os},
        {"version", img.version},
        {"architecture", img.architecture},
        {"filename", img.filename},
        {"path", img.path},
        {"size", img.size}
    };
    
    return result;
}

bool BaseImageManager::isImageAvailable(const std::string& imageId) {
    auto it = images.find(imageId);
    return it != images.end() && it->second.available;
}

std::string BaseImageManager::getImagePath(const std::string& imageId) {
    auto it = images.find(imageId);
    if (it == images.end() || !it->second.available) {
        return "";
    }
    
    return it->second.path;
}

