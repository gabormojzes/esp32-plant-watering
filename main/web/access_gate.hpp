#pragma once

#include "web/access.hpp"

class BearerAuth;
class HttpExchange;
class SessionStore;

class AccessGate {
public:
    struct Config {
        const char* login_uri;
    };

    AccessGate(
        const Config& config, const BearerAuth& bearer_auth, const SessionStore& session_store
    );

    bool granted(AccessSet access, const HttpExchange& exchange) const;
    void refuse(AccessSet access, HttpExchange& exchange) const;

private:
    bool has_token(const HttpExchange& exchange) const;
    bool has_session(const HttpExchange& exchange) const;
    bool has_session_not_from_another_origin(const HttpExchange& exchange) const;

    Config config_{};
    const BearerAuth& bearer_auth_;
    const SessionStore& session_store_;
};
