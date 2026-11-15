#include "messaging/http/http_telemetry_endpoint.hpp"

#include <esp_log.h>

#include <cstddef>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"
#include "shared/media_types.hpp"
#include "web/cors.hpp"

namespace {

constexpr char TAG[]{"HTTP_TELEMETRY_ENDPOINT"};

// One slot, because xQueueOverwrite, which send() relies on, requires a queue
// of length 1.
constexpr UBaseType_t QUEUE_LENGTH{1};

}  // namespace

HttpTelemetryEndpoint::HttpTelemetryEndpoint(
    WebHost& web_host, const char* uri, const char* media_type
)
    : web_host_{web_host}, uri_{uri}, media_type_{media_type} {}

void HttpTelemetryEndpoint::start() {
    if (mailbox_ != nullptr) {
        return;
    }
    mailbox_ = xQueueCreate(QUEUE_LENGTH, sizeof(Payload));
    if (mailbox_ == nullptr) {
        faults::raise(fault_definitions::HTTP_TELEMETRY_MAILBOX_UNAVAILABLE);
        ESP_LOGE(TAG, "Mailbox create failed, route %s disabled", uri_);
        return;
    }
    web_host_.route(
        uri_,
        HttpMethod::Get,
        {Access::Token, Access::SessionNotFromAnotherOrigin},
        &HttpTelemetryEndpoint::on_request,
        this
    );
    // A preflight is an OPTIONS request, which needs its own route. Browsers
    // send it without credentials, and the route is open.
    web_host_.route(
        uri_, HttpMethod::Options, {Access::Open}, &HttpTelemetryEndpoint::on_preflight, this
    );
}

void HttpTelemetryEndpoint::send(const Payload& payload) {
    if (mailbox_ == nullptr) {
        return;
    }
    // The result needs no checking. An overwrite always succeeds.
    xQueueOverwrite(mailbox_, &payload);
}

void HttpTelemetryEndpoint::on_request(HttpExchange& exchange, void* context) {
    static_cast<HttpTelemetryEndpoint*>(context)->serve(exchange);
}

void HttpTelemetryEndpoint::on_preflight(HttpExchange& exchange, void* /*context*/) {
    cors::preflight(exchange, "GET, OPTIONS");
}

void HttpTelemetryEndpoint::serve(HttpExchange& exchange) {
    cors::allow_any_origin(exchange);

    // Peeked rather than received, which leaves the frame for the next request
    // until a newer one replaces it.
    if (xQueuePeek(mailbox_, &payload_, 0) != pdTRUE) {
        exchange.set_status(HttpStatus::ServiceUnavailable);
        exchange.set_content_type(media_types::TEXT_PLAIN);
        exchange.send_text("No telemetry collected yet");
        return;
    }

    exchange.set_content_type(media_type_);
    exchange.send({payload_.data.data(), static_cast<std::size_t>(payload_.length)});
}
