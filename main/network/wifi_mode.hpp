#pragma once

#include <cstdint>

// The WiFi role a build runs. ApSta shares the one radio.
enum class WifiMode : std::uint8_t {
    Sta,
    Ap,
    ApSta
};

constexpr bool has_sta(WifiMode mode) {
    return mode == WifiMode::Sta || mode == WifiMode::ApSta;
}

constexpr bool has_ap(WifiMode mode) {
    return mode == WifiMode::Ap || mode == WifiMode::ApSta;
}

constexpr const char* mode_name(WifiMode mode) {
    switch (mode) {
        case WifiMode::Sta:
            return "sta";
        case WifiMode::Ap:
            return "ap";
        case WifiMode::ApSta:
            break;
    }
    return "apsta";
}
