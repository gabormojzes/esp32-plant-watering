#include "messaging/mqtt/mqtt_transport.hpp"

#include <esp_log.h>

#include "messaging/next_command.hpp"

namespace {

constexpr char TAG[]{"MQTT_TRANSPORT"};

}  // namespace

MqttTransport::MqttTransport(const Config& config, const MessageFormat& format)
    : format_{format},
      client_{config.client},
      command_inbox_{client_, config.topics.command},
      telemetry_publisher_{client_, config.topics.telemetry} {}

void MqttTransport::start() {
    // Both ends come up before the client, because the client only accepts
    // subscriptions before it starts and frames sent right after the first
    // connect must not be dropped.
    command_inbox_.start();
    telemetry_publisher_.start();
    client_.start();
}

std::optional<Command> MqttTransport::poll() {
    return next_command(command_inbox_, format_, inbound_payload_);
}

void MqttTransport::send(const Telemetry& telemetry) {
    if (!client_.is_connected()) {
        return;
    }
    if (!format_.encode(telemetry, outbound_payload_)) {
        ESP_LOGE(TAG, "Frame cannot be written as %s, dropped", format_.media_type());
        return;
    }
    telemetry_publisher_.send(outbound_payload_);
}

void MqttTransport::describe(MessagingInfo& messaging_info) const {
    messaging_info.mqtt = client_.info();
}
