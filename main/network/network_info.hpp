#pragma once

#include <optional>

#include "network/wifi_info.hpp"

// What the built-in network provider reports about itself. The part for a
// provider that is not built in stays empty.
struct NetworkInfo {
    std::optional<WifiInfo> wifi;
};
