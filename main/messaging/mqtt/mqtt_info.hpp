#pragma once

#include <cstdint>

struct MqttInfo {
    bool connected{false};
    // Connections regained after a drop. The first connection is not one.
    std::uint32_t reconnects{0};
};
