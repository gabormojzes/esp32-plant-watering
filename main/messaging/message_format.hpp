#pragma once

#include <optional>

#include "messaging/command.hpp"
#include "messaging/payload.hpp"
#include "messaging/telemetry.hpp"

// A wire format, which encodes telemetry and decodes commands without
// converting or validating values
class MessageFormat {
public:
    virtual ~MessageFormat() = default;
    virtual const char* media_type() const = 0;
    // Returns false when the frame cannot be written in this format or does not
    // fit.
    [[nodiscard]] virtual bool encode(const Telemetry& telemetry, Payload& payload) const = 0;
    // Returns nullopt when the payload is not a valid document in this format.
    // Values are returned as sent.
    virtual std::optional<Command> decode(const Payload& payload) const = 0;
};
