#pragma once

#include <cstdint>

// The device's status as a whole. Each indicator shows it in its own way.
enum class Status : std::uint8_t {
    Booting,
    Nominal,
    Warning,
    Error
};
