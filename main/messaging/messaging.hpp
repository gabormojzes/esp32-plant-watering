#pragma once

#include <optional>

#include "messaging/command.hpp"
#include "messaging/messaging_info.hpp"
#include "messaging/telemetry.hpp"

class Messaging {
public:
    virtual ~Messaging() = default;
    virtual void start() = 0;
    // Returns the next received command, or nullopt when none is pending.
    [[nodiscard]] virtual std::optional<Command> poll() = 0;
    // Sends a telemetry frame now. Dropped when it cannot be delivered.
    virtual void send(const Telemetry& telemetry) = 0;
    // Fills in its part of `messaging_info`, for display only.
    virtual void describe(MessagingInfo& messaging_info) const = 0;
};
