// #include "../include/vm_lookup.hpp"
// #include "../include/utils.hpp"

// #include <vector>
// #include <string>
// #include <libxml/paser.h>
// #include <libxml/parser.h>
// #include <libxml/tree.h>


// // Parse XML to extract disk information
// std::vector<DiskInfo> parseDisksFromXML(const char* xmlDesc) {
//     std::vector<DiskInfo> disks;
    
//     xmlDocPtr doc = xmlReadMemory(xmlDesc, strlen(xmlDesc), "noname.xml", NULL, 0);
//     if (doc == NULL) {
//         std::cerr << "Failed to parse XML" << std::endl;
//         return disks;
//     }
    
//     xmlNodePtr root = xmlDocGetRootElement(doc);
//     if (root == NULL) {
//         xmlFreeDoc(doc);
//         return disks;
//     }
    
//     // Find <devices> node
//     xmlNodePtr devicesNode = NULL;
//     for (xmlNodePtr node = root->children; node; node = node->next) {
//         if (node->type == XML_ELEMENT_NODE && 
//             xmlStrcmp(node->name, (const xmlChar*)"devices") == 0) {
//             devicesNode = node;
//             break;
//         }
//     }
    
//     if (!devicesNode) {
//         xmlFreeDoc(doc);
//         return disks;
//     }
    
//     // Iterate through <disk> elements
//     for (xmlNodePtr diskNode = devicesNode->children; diskNode; diskNode = diskNode->next) {
//         if (diskNode->type == XML_ELEMENT_NODE && 
//             xmlStrcmp(diskNode->name, (const xmlChar*)"disk") == 0) {
            
//             DiskInfo disk;
            
//             // Get disk type attribute (file/block)
//             xmlChar* diskType = xmlGetProp(diskNode, (const xmlChar*)"type");
//             if (diskType) {
//                 disk.type = (char*)diskType;
//                 xmlFree(diskType);
//             }
            
//             // Get disk device attribute (disk/cdrom)
//             xmlChar* diskDevice = xmlGetProp(diskNode, (const xmlChar*)"device");
//             bool isDisk = diskDevice && xmlStrcmp(diskDevice, (const xmlChar*)"disk") == 0;
//             if (diskDevice) xmlFree(diskDevice);
            
//             if (!isDisk) continue; // Skip non-disk devices (cdrom, etc.)
            
//             // Iterate through disk children
//             for (xmlNodePtr child = diskNode->children; child; child = child->next) {
//                 if (child->type != XML_ELEMENT_NODE) continue;
                
//                 // Get source path
//                 if (xmlStrcmp(child->name, (const xmlChar*)"source") == 0) {
//                     xmlChar* file = xmlGetProp(child, (const xmlChar*)"file");
//                     if (file) {
//                         disk.path = (char*)file;
//                         xmlFree(file);
//                     }
//                 }
                
//                 // Get target device name
//                 else if (xmlStrcmp(child->name, (const xmlChar*)"target") == 0) {
//                     xmlChar* dev = xmlGetProp(child, (const xmlChar*)"dev");
//                     if (dev) {
//                         disk.device = (char*)dev;
//                         xmlFree(dev);
//                     }
//                 }
                
//                 // Get driver format
//                 else if (xmlStrcmp(child->name, (const xmlChar*)"driver") == 0) {
//                     xmlChar* format = xmlGetProp(child, (const xmlChar*)"type");
//                     if (format) {
//                         disk.format = (char*)format;
//                         xmlFree(format);
//                     }
//                 }
//             }
            
//             // Only add if we have a path
//             if (!disk.path.empty()) {
//                 disks.push_back(disk);
//             }
//         }
//     }
    
//     xmlFreeDoc(doc);
//     return disks;
// }

// // Get all disk paths for a VM
// std::vector<DiskInfo> getVMDisks(virDomainPtr domain) {
//     std::vector<DiskInfo> disks;
    
//     // Get XML description of the domain
//     char* xmlDesc = virDomainGetXMLDesc(domain, 0);
//     if (!xmlDesc) {
//         std::cerr << "Failed to get domain XML description" << std::endl;
//         return disks;
//     }
    
//     disks = parseDisksFromXML(xmlDesc);
//     free(xmlDesc);
    
//     return disks;
// }

// // Get VM name and its disk paths
// void getVMName(virConnectPtr conn, const char* vmName) {
//     // Get domain by name
//     virDomainPtr domain = virDomainLookupByName(conn, vmName);
//     if (!domain) {
//         std::cerr << "Failed to find domain: " << vmName << std::endl;
//         return;
//     }
    
//     // Get VM name (confirmation)
//     std::string name = virDomainGetName(domain);
//     vmName = VMNameManager::parseVMName(name):

//     // // Get all disks
//     // std::vector<DiskInfo> disks = getVMDisks(domain);
    
//     virDomainFree(domain);
// }

// // List all VMs and their disk paths
// void listAllVMsWithDisks(virConnectPtr conn) {
//     virDomainPtr* domains = NULL;
//     int numDomains = virConnectListAllDomains(conn, &domains, 
//                                               VIR_CONNECT_LIST_DOMAINS_ACTIVE | 
//                                               VIR_CONNECT_LIST_DOMAINS_INACTIVE);
    
//     if (numDomains < 0) {
//         std::cerr << "Failed to list domains" << std::endl;
//         return;
//     }
     
//     for (int i = 0; i < numDomains; i++) {
//         const char* name = virDomainGetName(domains[i]);
//         std::cout << "\n[" << (i + 1) << "] VM: " << name << std::endl;
        
//         std::vector<DiskInfo> disks = getVMDisks(domains[i]);
        
//         for (const auto& disk : disks) {
//             std::cout << "  └─ " << disk.device << ": " << disk.path 
//                       << " (" << disk.format << ")" << std::endl;
//         }
        
//         virDomainFree(domains[i]);
//     }
    
//     free(domains);
// }

// // Get primary disk path for a VM (usually the first disk)
// std::string getVMPrimaryDiskPath(virDomainPtr domain) {
//     std::vector<DiskInfo> disks = getVMDisks(domain);
    
//     if (disks.empty()) {
//         return "";
//     }
    
//     // Return the first disk (usually vda or hda)
//     return disks[0].path;
// }
