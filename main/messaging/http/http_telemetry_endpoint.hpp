#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "messaging/payload.hpp"
#include "web/web_host.hpp"

// Latest-wins mailbox behind one route. send() never blocks, and a newer frame
// replaces an unread one. Until the first frame arrives the route answers 503.
class HttpTelemetryEndpoint {
public:
    HttpTelemetryEndpoint(WebHost& web_host, const char* uri, const char* media_type);
    // The route keeps a pointer to this object, never to a copy.
    HttpTelemetryEndpoint(const HttpTelemetryEndpoint&) = delete;
    HttpTelemetryEndpoint& operator=(const HttpTelemetryEndpoint&) = delete;
    // Creates the mailbox and claims the route. On failure send() does nothing.
    void start();
    void send(const Payload& payload);

private:
    static void on_request(HttpExchange& exchange, void* context);
    // CORS preflight for a cross-origin read, which credentials make non-simple
    static void on_preflight(HttpExchange& exchange, void* context);
    void serve(HttpExchange& exchange);

    WebHost& web_host_;
    const char* uri_{nullptr};
    const char* media_type_{nullptr};
    QueueHandle_t mailbox_{nullptr};
    // A member because a Payload does not fit the stack of the host's own task. Only that
    // task uses it, one request at a time.
    Payload payload_{};
};
