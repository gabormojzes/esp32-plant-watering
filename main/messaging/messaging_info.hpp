#pragma once

#include <optional>

#include "messaging/mqtt/mqtt_info.hpp"

// What the built-in transports report about themselves. The part for a
// transport that is not built in stays empty.
struct MessagingInfo {
    std::optional<MqttInfo> mqtt;
};
