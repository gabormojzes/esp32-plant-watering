#pragma once

// Host stub for the ESP-IDF hardware RNG, on the include path of the test
// targets that mint session ids. It fills from a counter rather than noise,
// letting a test rely on two ids differing without relying on what either is.

#include <cstddef>
#include <cstdint>

inline void esp_fill_random(void* buf, std::size_t len) {
    static std::uint8_t next{0};
    auto* bytes{static_cast<std::uint8_t*>(buf)};
    for (std::size_t i{0}; i < len; ++i) {
        bytes[i] = next++;
    }
}
