#pragma once

#include <cstdint>

// One self-consistent view of the sensor's readings. A faulted view may carry a
// newer raw count.
struct SensorSnapshot {
    float latest{0.0f};             // Moisture % of the latest usable burst
    float smoothed{0.0f};           // Smoothed moisture %
    std::int64_t sampled_at_us{0};  // When the moisture behind `smoothed` was read
    std::uint32_t raw{0};           // Raw sensor count, exposed for field calibration
    bool ready{false};              // True once a real reading has landed
    bool faulted{false};            // Latest attempt yielded no moisture worth trusting
};
