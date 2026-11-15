#pragma once

// Host stub for the ESP-IDF network interface layer. One interface always
// exists.

#include <cstddef>

inline std::size_t esp_netif_get_nr_of_ifs() {
    return 1;
}
