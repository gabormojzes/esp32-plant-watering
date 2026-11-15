#include "messaging/http/http_command_endpoint.hpp"

#include <esp_log.h>

#include <array>
#include <cstddef>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"
#include "shared/http_utils.hpp"
#include "web/cors.hpp"

namespace {

constexpr char TAG[]{"HTTP_COMMAND_ENDPOINT"};

// A short queue absorbs a burst of posts without dropping the earlier of
// two quick commands.
constexpr UBaseType_t QUEUE_LENGTH{2};

// A quiet socket gets one retry before the client is treated as gone.
constexpr int BODY_RETRIES{1};

bool has_content_type(const HttpExchange& exchange, const char* media_type) {
    // Holds the type and any parameters, such as "; charset=utf-8". A longer
    // value reads as truncated and is refused.
    std::array<char, HttpCommandEndpoint::CONTENT_TYPE_BUFFER_SIZE> content_type{};
    if (!exchange.header("Content-Type", content_type)) {
        return false;
    }
    return http_utils::is_media_type(content_type.data(), media_type);
}

}  // namespace

HttpCommandEndpoint::HttpCommandEndpoint(WebHost& web_host, const char* uri, const char* media_type)
    : web_host_{web_host}, uri_{uri}, media_type_{media_type} {}

void HttpCommandEndpoint::start() {
    if (queue_ != nullptr) {
        return;
    }
    queue_ = xQueueCreate(QUEUE_LENGTH, sizeof(Payload));
    if (queue_ == nullptr) {
        faults::raise(fault_definitions::HTTP_COMMAND_QUEUE_UNAVAILABLE);
        ESP_LOGE(TAG, "Queue create failed, route %s disabled", uri_);
        return;
    }
    web_host_.route(
        uri_,
        HttpMethod::Post,
        {Access::Token, Access::SessionNotFromAnotherOrigin},
        &HttpCommandEndpoint::on_request,
        this
    );
    // A preflight is an OPTIONS request, which needs its own route. Browsers
    // send it without credentials, and the route is open.
    web_host_.route(
        uri_, HttpMethod::Options, {Access::Open}, &HttpCommandEndpoint::on_preflight, this
    );
}

bool HttpCommandEndpoint::take(Payload& payload) {
    return queue_ != nullptr && xQueueReceive(queue_, &payload, 0) == pdTRUE;
}

void HttpCommandEndpoint::on_request(HttpExchange& exchange, void* context) {
    static_cast<HttpCommandEndpoint*>(context)->receive(exchange);
}

void HttpCommandEndpoint::on_preflight(HttpExchange& exchange, void* /*context*/) {
    cors::preflight(exchange, "POST, OPTIONS");
}

void HttpCommandEndpoint::receive(HttpExchange& exchange) {
    // Set before branching, which puts it on every answer below.
    cors::allow_any_origin(exchange);

    // Requiring this type forces every cross-origin POST through a CORS
    // preflight.
    if (!has_content_type(exchange, media_type_)) {
        ESP_LOGW(TAG, "Not %s on route %s, command dropped", media_type_, uri_);
        exchange.set_status(HttpStatus::UnsupportedMediaType);
        exchange.send_empty();
        return;
    }

    const std::size_t length{exchange.body_length()};
    if (length > Payload::BUFFER_SIZE) {
        ESP_LOGW(TAG, "Body too long on route %s, command dropped", uri_);
        exchange.set_status(HttpStatus::ContentTooLarge);
        exchange.send_empty();
        return;
    }

    switch (exchange.read_body({payload_.data.data(), length}, BODY_RETRIES)) {
        case BodyResult::TimedOut:
            ESP_LOGW(TAG, "Body timed out on route %s, command dropped", uri_);
            exchange.set_status(HttpStatus::RequestTimeout);
            exchange.send_empty();
            return;
        case BodyResult::Failed:
            ESP_LOGW(TAG, "Body read failed on route %s, command dropped", uri_);
            exchange.set_status(HttpStatus::BadRequest);
            exchange.send_empty();
            return;
        case BodyResult::Complete:
            break;
    }
    payload_.length = static_cast<int>(length);

    // Zero timeout, because the host's own task must never block. A full queue drops
    // the newest command.
    if (xQueueSend(queue_, &payload_, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Queue full on route %s, command dropped", uri_);
        exchange.set_status(HttpStatus::ServiceUnavailable);
        exchange.send_empty();
        return;
    }

    // 202, because the payload is only queued, not yet validated or applied.
    exchange.set_status(HttpStatus::Accepted);
    exchange.send_empty();
}
