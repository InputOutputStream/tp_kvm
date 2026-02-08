#ifndef BASEIMAGE_MANAGER_HPP
#define BASEIMAGE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include "json.hpp"
#include "remote_executor.hpp"

using json = nlohmann::json;

struct BaseImage {
    std::string id;
    std::string filename;
    std::string displayName;
    std::string os;
    std::string version;
    std::string architecture;
    long long size;
    bool available;
    std::string path;
};

class BaseImageManager {
private:
    std::string baseImageDir;
    std::map<std::string, BaseImage> images;
    RemoteExec::RemoteExecutor* remoteExec;
    
    void scanBaseImages();
    BaseImage parseImageInfo(const std::string& filename, const std::string& fullPath);
    std::string extractOSName(const std::string& filename);
    std::string extractVersion(const std::string& filename);
    std::string extractArchitecture(const std::string& filename);
    long long getFileSize(const std::string& path);
    bool isValidPath(const std::string& path);

public:
    BaseImageManager(RemoteExec::RemoteExecutor* executor, 
                     const std::string& imageDir = "/var/lib/libvirt/images/baseimg");
    ~BaseImageManager();
    
    // Image discovery
    void refresh();
    json listImages();
    json getImage(const std::string& imageId);
    bool isImageAvailable(const std::string& imageId);

    // Get image path
    std::string getImagePath(const std::string& imageId);
};

#endif // BASEIMAGE_MANAGER_HPP