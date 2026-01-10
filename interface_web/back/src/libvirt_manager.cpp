#include "../include/libvirt_manager.hpp"

#include <iostream>

LibvirtManager::LibvirtManager() 
    : conn(nullptr), useRemote(false) {}

LibvirtManager::~LibvirtManager() {
    disconnect();
}

bool LibvirtManager::connect(bool remote, const std::string& host, const std::string& user) {
    if (conn != nullptr) {
        return true; // Already connected
    }

    useRemote = remote;
    remoteHost = host;
    username = user;

    std::string uri;
    if (useRemote) {
        uri = "qemu+ssh://" + username + "@" + remoteHost + "/system";
        std::cout << "Connecting to: " << uri << std::endl;
    } else {
        uri = "qemu:///system";
    }

    conn = virConnectOpen(uri.c_str());

    if (conn == nullptr) {
        virErrorPtr err = virGetLastError();
        if (err) {
            std::cerr << "Libvirt error: " << err->message << std::endl;
        } else {
            std::cerr << "Error: Cannot connect to libvirt" << std::endl;
        }
        return false;
    }

    // Verify connection
    char* hostname = virConnectGetHostname(conn);
    if (hostname) {
        std::cout << "Connected to host: " << hostname << std::endl;
        free(hostname);
    }

    return true;
}

void LibvirtManager::disconnect() {
    if (conn != nullptr) {
        virConnectClose(conn);
        conn = nullptr;
    }
}

bool LibvirtManager::isConnected() const {
    return conn != nullptr;
}

virConnectPtr LibvirtManager::getConnection() const {
    return conn;
}

bool LibvirtManager::getNodeInfo(virNodeInfo& info) {
    if (!isConnected()) return false;
    return virNodeGetInfo(conn, &info) == 0;
}

bool LibvirtManager::getVersion(unsigned long& version) {
    if (!isConnected()) return false;
    return virConnectGetVersion(conn, &version) == 0;
}

bool LibvirtManager::getLibVersion(unsigned long& version) {
    if (!isConnected()) return false;
    return virConnectGetLibVersion(conn, &version) == 0;
}