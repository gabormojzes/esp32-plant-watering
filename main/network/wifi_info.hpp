#pragma once

#include <cstdint>
#include <optional>

#include "shared/net_utils.hpp"

struct WifiInfo {
    struct Station {
        // Empty while the radio is down
        std::optional<net_utils::MacAddress> mac;
        // The joined access point's MAC; empty when not joined
        std::optional<net_utils::MacAddress> bssid;
        // dBm of the joined access point; empty when not joined
        std::optional<std::int8_t> rssi;
        // Links regained after a drop; empty while the radio is down
        std::optional<std::uint32_t> reconnects;
        // Empty until DHCP assigns one
        std::optional<net_utils::Ipv4Address> ipv4;
    };

    struct AccessPoint {
        std::uint8_t clients{0};
        // Empty while the radio is down
        std::optional<net_utils::Ipv4Address> ipv4;
    };

    // The role the radio runs
    const char* mode{""};
    // Empty while the radio is down, and while a station alone is still looking
    // for its network
    std::optional<std::uint8_t> channel;
    // Each part is there exactly when the mode runs it
    std::optional<Station> station;
    std::optional<AccessPoint> access_point;
};
