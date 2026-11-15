#pragma once

#include <optional>

#include "messaging/command.hpp"
#include "messaging/http/http_command_endpoint.hpp"
#include "messaging/http/http_telemetry_endpoint.hpp"
#include "messaging/message_format.hpp"
#include "messaging/messaging.hpp"
#include "messaging/messaging_info.hpp"
#include "messaging/payload.hpp"
#include "messaging/telemetry.hpp"
#include "web/web_host.hpp"

class HttpTransport : public Messaging {
public:
    HttpTransport(WebHost& web_host, const MessageFormat& format);

    void start() override;
    [[nodiscard]] std::optional<Command> poll() override;
    void send(const Telemetry& telemetry) override;
    // HTTP holds no link of its own, which leaves nothing to report.
    void describe(MessagingInfo& /*messaging_info*/) const override {}

private:
    const MessageFormat& format_;
    HttpCommandEndpoint command_endpoint_;
    HttpTelemetryEndpoint telemetry_endpoint_;
    // Members because a Payload is too large for the calling task's stack
    Payload inbound_payload_{};
    Payload outbound_payload_{};
};
