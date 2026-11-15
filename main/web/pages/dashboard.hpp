#pragma once

#include "web/web_host.hpp"

class Dashboard {
public:
    struct Config {
        const char* uri;
    };

    static constexpr char CONTENT_SECURITY_POLICY[]{
        "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
        "connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'"
    };

    Dashboard(WebHost& web_host, const Config& config);
    // The route keeps a pointer to this object, never to a copy.
    Dashboard(const Dashboard&) = delete;
    Dashboard& operator=(const Dashboard&) = delete;
    // Only claims the route.
    void start();

private:
    static void on_request(HttpExchange& exchange, void* context);

    static void page(HttpExchange& exchange);

    WebHost& web_host_;
    Config config_{};
};
