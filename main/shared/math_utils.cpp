#include "shared/math_utils.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

std::optional<float> math_utils::percent_of_range(
    std::uint32_t value, std::uint32_t min, std::uint32_t max
) {
    if (min >= max) {
        return std::nullopt;
    }
    value = std::clamp(value, min, max);
    // Divided before scaling, which keeps the ratio from rounding above 1.
    return (static_cast<float>(value - min) / static_cast<float>(max - min)) * 100.0f;
}

std::optional<float> math_utils::percent_of_range_inverted(
    double value, std::uint32_t min, std::uint32_t max
) {
    if (min >= max || !std::isfinite(value)) {
        return std::nullopt;
    }
    // In double, which holds every std::uint32_t exactly
    const double low{static_cast<double>(min)};
    const double high{static_cast<double>(max)};
    value = std::clamp(value, low, high);
    // Divided before scaling, which keeps the ratio from rounding above 1.
    return static_cast<float>(((high - value) / (high - low)) * 100.0);
}

math_utils::Ema::Ema(float alpha)
    : alpha_{alpha}, usable_{alpha >= MIN_ALPHA && alpha <= MAX_ALPHA} {}

std::optional<float> math_utils::Ema::update(float sample) {
    if (!usable_ || !std::isfinite(sample)) {
        return std::nullopt;
    }
    if (!seeded_) {
        value_ = sample;
        seeded_ = true;
    } else {
        value_ += alpha_ * (sample - value_);
    }
    return value_;
}

void math_utils::Ema::reset() {
    seeded_ = false;
}
