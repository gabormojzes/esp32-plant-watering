#pragma once

#include <cstdint>
#include <optional>

#include "shared/math_utils.hpp"

// The sensor's raw counts at the two ends of the moisture scale. A drier soil
// reads a higher count.
struct SensorCalibration {
    std::uint16_t adc_min;  // Read in water, mapped to 100%
    std::uint16_t adc_max;  // Read dry, mapped to 0%

    bool operator==(const SensorCalibration&) const = default;

    // Maps a count, smoothed or not, to moisture %, clamped to the
    // endpoints. Nullopt when the endpoints map nothing.
    std::optional<float> percent(double raw) const {
        return math_utils::percent_of_range_inverted(raw, adc_min, adc_max);
    }

    // A count above adc_max is a disconnected sensor, read through its pull-up, not
    // a drier soil.
    bool plausible(std::uint32_t raw) const {
        return raw <= adc_max;
    }
};
