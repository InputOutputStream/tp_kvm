#include "../include/isolation_utils.hpp"

std::mutex ResourceIDGenerator::idMutex;
std::set<std::string> ResourceIDGenerator::usedIDs;