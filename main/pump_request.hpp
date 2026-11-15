#pragma once

#include <cstdint>

// A one-off request a command carries. Unlike a setting, it is acted on once
// and never stored.
enum class PumpRequest : std::uint8_t {
    Run,
    Stop
};
