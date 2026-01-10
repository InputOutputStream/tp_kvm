#include "../include/cors.hpp"
#include <vector>
#include <algorithm>

namespace cors {

// List of allowed origins
std::vector<std::string> allowedOrigins = {
    "http://localhost:1234",
    "http://localhost:3000",
    "http://localhost:5173",
    "http://127.0.0.1:1234"
};

void addHeaders(httplib::Response& res, const std::string& origin = "*") {
    // Only set headers if they haven't been set already
    if (res.get_header_value("Access-Control-Allow-Origin").empty()) {
        res.set_header("Access-Control-Allow-Origin", origin);
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        
        // Only set credentials if not using wildcard
        if (origin != "*") {
            res.set_header("Access-Control-Allow-Credentials", "true");
        }
    }
}

void setupMiddleware(httplib::Server& svr) {
    // Handle OPTIONS preflight requests FIRST (before post routing)
    svr.Options(R"(.*)", [](const httplib::Request& req, httplib::Response& res) {
        std::string origin = req.get_header_value("Origin");
        
        if (!origin.empty() && 
            std::find(allowedOrigins.begin(), allowedOrigins.end(), origin) != allowedOrigins.end()) {
            res.set_header("Access-Control-Allow-Origin", origin);
            res.set_header("Access-Control-Allow-Credentials", "true");
        } else {
            res.set_header("Access-Control-Allow-Origin", "*");
        }
        
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set_header("Access-Control-Max-Age", "3600");
        res.status = 204; // No Content for OPTIONS
    });
    
    // Add CORS headers to all other responses
    svr.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        // Skip if this was an OPTIONS request (already handled)
        if (req.method == "OPTIONS") {
            return;
        }
        
        std::string origin = req.get_header_value("Origin");
        
        // Check if origin is in allowed list
        if (!origin.empty() && 
            std::find(allowedOrigins.begin(), allowedOrigins.end(), origin) != allowedOrigins.end()) {
            addHeaders(res, origin);
        } else if (origin.empty()) {
            // No origin header 
            addHeaders(res, "*");
        } else {
            // For development, allow all
            addHeaders(res, "*");  
        }
    });
}

} // namespace cors