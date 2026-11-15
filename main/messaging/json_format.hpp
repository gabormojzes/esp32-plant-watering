#pragma once

#include <optional>

#include "messaging/command.hpp"
#include "messaging/message_format.hpp"
#include "messaging/payload.hpp"
#include "messaging/telemetry.hpp"
#include "shared/media_types.hpp"

class JsonFormat : public MessageFormat {
public:
    static constexpr const char* MEDIA_TYPE{media_types::APPLICATION_JSON};

    const char* media_type() const override;
    [[nodiscard]] bool encode(const Telemetry& telemetry, Payload& payload) const override;
    std::optional<Command> decode(const Payload& payload) const override;
};
