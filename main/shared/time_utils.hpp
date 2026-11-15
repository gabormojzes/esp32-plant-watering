#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include "shared/constants.hpp"

namespace time_utils {

constexpr std::int64_t to_seconds_floor(std::int64_t us) {
    const std::int64_t truncated{us / constants::US_PER_S};
    // Division truncates toward zero, leaving a negative span one above its floor.
    return ((us % constants::US_PER_S) < 0) ? (truncated - 1) : truncated;
}

constexpr std::int64_t to_seconds_ceil(std::int64_t us) {
    const std::int64_t truncated{us / constants::US_PER_S};
    // Division truncates toward zero, which is already up for a negative span.
    return ((us % constants::US_PER_S) > 0) ? (truncated + 1) : truncated;
}

// Converts whole seconds to microseconds. Nullopt when the value is not
// integral or the result would overflow int64.
inline std::optional<std::int64_t> whole_seconds_to_us(double seconds) {
    constexpr std::int64_t limit_s{std::numeric_limits<std::int64_t>::max() / constants::US_PER_S};
    if (std::floor(seconds) != seconds || std::abs(seconds) > static_cast<double>(limit_s)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(seconds) * constants::US_PER_S;
}

}  // namespace time_utils
