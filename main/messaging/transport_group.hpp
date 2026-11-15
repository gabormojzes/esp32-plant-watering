#pragma once

#include <optional>
#include <span>

#include "messaging/command.hpp"
#include "messaging/messaging.hpp"
#include "messaging/messaging_info.hpp"
#include "messaging/telemetry.hpp"

// Fans one Messaging out to every transport that runs. An empty group carries
// nothing.
class TransportGroup : public Messaging {
public:
    struct Config {
        std::span<Messaging* const> transports;
    };

    explicit TransportGroup(const Config& config) : config_{config} {}

    void start() override {
        for (Messaging* transport : config_.transports) {
            transport->start();
        }
    }

    [[nodiscard]] std::optional<Command> poll() override {
        for (Messaging* transport : config_.transports) {
            if (std::optional<Command> command{transport->poll()}) {
                return command;
            }
        }
        return std::nullopt;
    }

    // Each transport drops the frame itself when it cannot deliver.
    void send(const Telemetry& telemetry) override {
        for (Messaging* transport : config_.transports) {
            transport->send(telemetry);
        }
    }

    void describe(MessagingInfo& messaging_info) const override {
        for (const Messaging* transport : config_.transports) {
            transport->describe(messaging_info);
        }
    }

private:
    Config config_{};
};
