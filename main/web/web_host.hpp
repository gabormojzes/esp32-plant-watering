#pragma once

#include <cstdint>

#include "web/access.hpp"
#include "web/http_exchange.hpp"

class WebHost {
public:
    // Runs on the host's own task, with the context the route was claimed with.
    using Handler = void (*)(HttpExchange& exchange, void* context);

    virtual ~WebHost() = default;

    virtual void start() = 0;
    // Judges time limits from the caller's task, because the host's own task can
    // be stuck on a client.
    virtual void tick(std::int64_t now_us) = 0;
    // Registers a route. Call only before start(), which registers the table with
    // the server; a route added later is never served. With no room left the
    // route is not served either. The host checks `access` before calling the
    // handler.
    virtual void route(
        const char* uri, HttpMethod method, AccessSet access, Handler handler, void* context
    ) = 0;
};
