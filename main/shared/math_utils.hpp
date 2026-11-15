#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace math_utils {

// True for 0..100. NaN and infinity are false.
template <std::floating_point T>
constexpr bool is_in_percent_range(T value) {
    return value >= T{0} && value <= T{100};
}

// Rounds `value` to the nearest tenth, ties away from zero, never returning -0.
// Hand-rolled because std::round is not constexpr on every toolchain here, and this
// also runs at compile time.
template <std::floating_point T>
constexpr T nearest_tenth(T value) {
    // Where the type stops holding fractions. Below it, scaling stays in std::int64_t.
    constexpr T holds_fractions{T{1} / std::numeric_limits<T>::epsilon()};

    static_assert(
        holds_fractions <= static_cast<T>(std::numeric_limits<std::int64_t>::max()) / T{10},
        "scaling leaves std::int64_t before this type runs out of fractional values"
    );

    // At or above it every value is whole already. NaN and infinity land here too.
    const T magnitude{value < T{0} ? -value : value};

    if (!(magnitude < holds_fractions)) {
        return value;
    }

    const T scaled{value * T{10}};
    const T truncated{static_cast<T>(static_cast<std::int64_t>(scaled))};

    // Compared against a half rather than adding one. The fraction is exact, but
    // the addition can round up and carry to the wrong tenth.
    const T fraction{scaled - truncated};
    const T fraction_size{fraction < T{0} ? -fraction : fraction};
    const T away_from_zero{scaled < T{0} ? T{-1} : T{1}};
    const T rounded{fraction_size >= T{0.5} ? truncated + away_from_zero : truncated};
    return rounded / T{10};
}

// Zero counts as one digit.
constexpr std::size_t decimal_digits(std::uint32_t value) {
    std::size_t digits{1};
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

// Returns where `value` sits in [min, max] as 0..100, clamping values outside
// it. Nullopt when min is not below max.
std::optional<float> percent_of_range(std::uint32_t value, std::uint32_t min, std::uint32_t max);

// As percent_of_range, but measured back from max: 100 at min, 0 at max. The
// value may carry a fraction. Nullopt also for a value that is NaN or infinite.
std::optional<float> percent_of_range_inverted(double value, std::uint32_t min, std::uint32_t max);

// The value as a whole number of type T. Nullopt when it carries a fraction, is
// negative, NaN or infinite, or T cannot hold it.
template <std::unsigned_integral T>
std::optional<T> whole_count(double value) {
    // One past T's maximum, a power of two a double holds exactly at every width
    const double limit{static_cast<double>(std::numeric_limits<T>::max()) + 1.0};
    // Written as what it admits, which leaves NaN out
    const bool whole{value >= 0.0 && value < limit && std::floor(value) == value};
    if (!whole) {
        return std::nullopt;
    }
    return static_cast<T>(value);
}

// Exponential moving average, seeded with the first usable sample rather than
// zero
class Ema {
public:
    static constexpr float MIN_ALPHA{0.0f};
    static constexpr float MAX_ALPHA{1.0f};

    // `alpha` weighs the newest sample, from MIN_ALPHA, which holds the seed, to
    // MAX_ALPHA, which tracks the input. One outside those bounds is kept as
    // given, not corrected.
    explicit Ema(float alpha);

    // Advances the average and returns it. Returns nullopt, leaving the average
    // unchanged, for an alpha outside the bounds or a non-finite sample.
    [[nodiscard]] std::optional<float> update(float sample);
    // Forgets the average. The next usable sample seeds it again.
    void reset();

private:
    float alpha_{0.0f};
    bool usable_{false};
    float value_{0.0f};
    bool seeded_{false};
};

}  // namespace math_utils
