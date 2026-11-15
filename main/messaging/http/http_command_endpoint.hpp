#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstddef>
#include <string_view>

#include "messaging/payload.hpp"
#include "shared/http_utils.hpp"
#include "web/web_host.hpp"

// FIFO of posted payloads behind one route, filled on the host's own task and drained
// by the consumer at its own pace
class HttpCommandEndpoint {
public:
    // Room for a posted Content-Type, parameters and terminator included
    static constexpr std::size_t CONTENT_TYPE_BUFFER_SIZE{64};

    // Whether commands can be taken in this media type. Its Content-Type must fit
    // the buffer it is read into, terminator included. It must also be one a
    // cross-origin page cannot post without a preflight.
    static constexpr bool is_command_media_type(std::string_view media_type) {
        return media_type.size() < CONTENT_TYPE_BUFFER_SIZE &&
               !http_utils::is_cors_safelisted_media_type(media_type);
    }

    HttpCommandEndpoint(WebHost& web_host, const char* uri, const char* media_type);
    // The route keeps a pointer to this object, never to a copy.
    HttpCommandEndpoint(const HttpCommandEndpoint&) = delete;
    HttpCommandEndpoint& operator=(const HttpCommandEndpoint&) = delete;
    // Creates the queue and claims the route. On failure the route is never
    // claimed.
    void start();
    // Takes the oldest pending payload. Returns false when there is none.
    [[nodiscard]] bool take(Payload& payload);

private:
    static void on_request(HttpExchange& exchange, void* context);
    static void on_preflight(HttpExchange& exchange, void* context);
    void receive(HttpExchange& exchange);

    WebHost& web_host_;
    const char* uri_{nullptr};
    const char* media_type_{nullptr};
    QueueHandle_t queue_{nullptr};
    // A member because a Payload does not fit the stack of the host's own task. Only that
    // task uses it, one request at a time.
    Payload payload_{};
};
