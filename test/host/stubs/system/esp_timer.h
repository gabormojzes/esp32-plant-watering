#pragma once

// Host stub for the ESP-IDF monotonic clock, on the include path of the test
// targets that expire sessions. Time stands still until a test moves it.

#include <cstdint>

inline std::int64_t esp_timer_stub_now_us{0};

inline std::int64_t esp_timer_get_time() {
    return esp_timer_stub_now_us;
}
