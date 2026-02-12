#ifndef CORS_HPP
#define CORS_HPP

#include "httplib.h"

namespace cors {
    // Add CORS headers to response
    void addHeaders(httplib::Response& res, const std::string& origin);

    // Setup CORS middleware for server
    void setupMiddleware(httplib::Server& svr);
}

#endif // CORS_HPP