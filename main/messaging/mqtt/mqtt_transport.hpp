#pragma once

#include <optional>

#include "messaging/command.hpp"
#include "messaging/message_format.hpp"
#include "messaging/messaging.hpp"
#include "messaging/messaging_info.hpp"
#include "messaging/mqtt/mqtt_client.hpp"
#include "messaging/mqtt/mqtt_inbox.hpp"
#include "messaging/mqtt/mqtt_publisher.hpp"
#include "messaging/mqtt/mqtt_topics.hpp"
#include "messaging/payload.hpp"
#include "messaging/telemetry.hpp"

class MqttTransport : public Messaging {
public:
    struct Config {
        MqttClient::Config client;
        MqttTopics topics;
    };

    MqttTransport(const Config& config, const MessageFormat& format);

    void start() override;
    [[nodiscard]] std::optional<Command> poll() override;
    void send(const Telemetry& telemetry) override;
    void describe(MessagingInfo& messaging_info) const override;

private:
    const MessageFormat& format_;
    MqttClient client_;
    MqttInbox command_inbox_;
    MqttPublisher telemetry_publisher_;
    // Members because a Payload is too large for the calling task's stack
    Payload inbound_payload_{};
    Payload outbound_payload_{};
};
