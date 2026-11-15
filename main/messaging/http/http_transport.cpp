#include "messaging/http/http_transport.hpp"

#include <esp_log.h>

#include "messaging/next_command.hpp"

namespace {

constexpr char TAG[]{"HTTP_TRANSPORT"};

constexpr char TELEMETRY_URI[]{"/api/telemetry"};
constexpr char COMMAND_URI[]{"/api/command"};

}  // namespace

HttpTransport::HttpTransport(WebHost& web_host, const MessageFormat& format)
    : format_{format},
      command_endpoint_{web_host, COMMAND_URI, format.media_type()},
      telemetry_endpoint_{web_host, TELEMETRY_URI, format.media_type()} {}

void HttpTransport::start() {
    command_endpoint_.start();
    telemetry_endpoint_.start();
}

std::optional<Command> HttpTransport::poll() {
    return next_command(command_endpoint_, format_, inbound_payload_);
}

void HttpTransport::send(const Telemetry& telemetry) {
    if (!format_.encode(telemetry, outbound_payload_)) {
        ESP_LOGE(TAG, "Frame cannot be written as %s, dropped", format_.media_type());
        return;
    }
    telemetry_endpoint_.send(outbound_payload_);
}
