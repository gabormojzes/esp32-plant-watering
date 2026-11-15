#include <doctest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>

#include "shared/math_utils.hpp"

namespace {

// A range width at which scaling before dividing rounds full scale past 100
constexpr std::uint32_t SCALE_FIRST_OVERSHOOT_WIDTH{673103};
constexpr std::uint32_t MAX_WIDTH{std::numeric_limits<std::uint32_t>::max()};
constexpr std::uint32_t HALF_WIDTH{MAX_WIDTH / 2};

constexpr float NOT_A_NUMBER{std::numeric_limits<float>::quiet_NaN()};
constexpr float INFINITY_F{std::numeric_limits<float>::infinity()};

}  // namespace

// The same rules hold at compile time.
static_assert(
    math_utils::is_in_percent_range(50.0f) && math_utils::nearest_tenth(50.0f) == 50.0f,
    "50 is out of range or not kept as is"
);
static_assert(math_utils::nearest_tenth(0.5f) == 0.5f, "0.5 is not kept as is");
static_assert(
    math_utils::nearest_tenth(0.24f) == 0.2f && math_utils::nearest_tenth(0.26f) == 0.3f,
    "0.24 and 0.26 do not round to the nearest tenth"
);
static_assert(math_utils::nearest_tenth(-55.54f) == -55.5f, "-55.54 does not round toward -55.5");
static_assert(math_utils::nearest_tenth(0.3) == 0.3, "0.3 as a double is not kept as is");

TEST_CASE("percent_of_range maps a value to its position in [min, max]") {
    CHECK(math_utils::percent_of_range(0, 0, 100).value() == doctest::Approx(0.0f));
    CHECK(math_utils::percent_of_range(50, 0, 100).value() == doctest::Approx(50.0f));
    CHECK(math_utils::percent_of_range(100, 0, 100).value() == doctest::Approx(100.0f));
    CHECK(math_utils::percent_of_range(1510, 0, 3020).value() == doctest::Approx(50.0f));
}

TEST_CASE("percent_of_range maps a value inside a range that starts above zero") {
    // The position is measured from min, not from zero.
    CHECK(
        math_utils::percent_of_range(1000, 1000, 3000).value() == doctest::Approx(0.0f)
    );  // At min
    CHECK(math_utils::percent_of_range(1500, 1000, 3000).value() == doctest::Approx(25.0f));
    CHECK(math_utils::percent_of_range(2000, 1000, 3000).value() == doctest::Approx(50.0f));
    CHECK(
        math_utils::percent_of_range(3000, 1000, 3000).value() == doctest::Approx(100.0f)
    );  // At max
}

TEST_CASE("percent_of_range clamps values outside the range") {
    CHECK(
        math_utils::percent_of_range(0, 1000, 3000).value() == doctest::Approx(0.0f)
    );  // Below min
    CHECK(
        math_utils::percent_of_range(5000, 1000, 3000).value() == doctest::Approx(100.0f)
    );  // Above max
}

TEST_CASE("percent_of_range reports nothing for a range with no width") {
    CHECK_FALSE(math_utils::percent_of_range(500, 1000, 1000).has_value());  // Degenerate
    CHECK_FALSE(math_utils::percent_of_range(500, 3000, 1000).has_value());  // Reversed
}

TEST_CASE("percent_of_range holds its bounds however wide the range") {
    // Exact, not approximate, because the ends are what the 0..100 promise rests
    // on.
    CHECK(math_utils::percent_of_range(0, 0, MAX_WIDTH).value() == 0.0f);
    CHECK(math_utils::percent_of_range(MAX_WIDTH, 0, MAX_WIDTH).value() == 100.0f);
    CHECK(
        math_utils::percent_of_range(SCALE_FIRST_OVERSHOOT_WIDTH, 0, SCALE_FIRST_OVERSHOOT_WIDTH)
            .value() == 100.0f
    );
    CHECK(math_utils::percent_of_range(HALF_WIDTH, 0, MAX_WIDTH).value() == doctest::Approx(50.0f));
}

TEST_CASE("percent_of_range_inverted maps a value to its distance from max in [min, max]") {
    CHECK(math_utils::percent_of_range_inverted(0, 0, 100).value() == doctest::Approx(100.0f));
    CHECK(math_utils::percent_of_range_inverted(75, 0, 100).value() == doctest::Approx(25.0f));
    CHECK(math_utils::percent_of_range_inverted(100, 0, 100).value() == doctest::Approx(0.0f));
}

TEST_CASE("percent_of_range_inverted maps a value inside a range that starts above zero") {
    // The distance is measured back from max, not from zero.
    CHECK(
        math_utils::percent_of_range_inverted(1000, 1000, 3000).value() == doctest::Approx(100.0f)
    );
    CHECK(
        math_utils::percent_of_range_inverted(1500, 1000, 3000).value() == doctest::Approx(75.0f)
    );
    CHECK(
        math_utils::percent_of_range_inverted(2000, 1000, 3000).value() == doctest::Approx(50.0f)
    );
    CHECK(math_utils::percent_of_range_inverted(3000, 1000, 3000).value() == doctest::Approx(0.0f));
}

TEST_CASE("percent_of_range_inverted clamps values outside the range") {
    CHECK(
        math_utils::percent_of_range_inverted(0, 1000, 3000).value() == doctest::Approx(100.0f)
    );  // Below min
    CHECK(
        math_utils::percent_of_range_inverted(5000, 1000, 3000).value() == doctest::Approx(0.0f)
    );  // Above max
}

TEST_CASE("percent_of_range_inverted reports nothing for a range with no width") {
    CHECK_FALSE(math_utils::percent_of_range_inverted(500, 1000, 1000).has_value());  // Degenerate
    CHECK_FALSE(math_utils::percent_of_range_inverted(500, 3000, 1000).has_value());  // Reversed
}

TEST_CASE("percent_of_range_inverted holds its bounds however wide the range") {
    CHECK(math_utils::percent_of_range_inverted(0, 0, MAX_WIDTH).value() == 100.0f);
    CHECK(math_utils::percent_of_range_inverted(MAX_WIDTH, 0, MAX_WIDTH).value() == 0.0f);
    CHECK(
        math_utils::percent_of_range_inverted(0, 0, SCALE_FIRST_OVERSHOOT_WIDTH).value() == 100.0f
    );
    CHECK(
        math_utils::percent_of_range_inverted(HALF_WIDTH, 0, MAX_WIDTH).value() ==
        doctest::Approx(50.0f)
    );
}

TEST_CASE("percent_of_range_inverted maps a value between two counts") {
    CHECK(math_utils::percent_of_range_inverted(25.5, 0, 100).value() == doctest::Approx(74.5f));
}

TEST_CASE("percent_of_range_inverted reports nothing for a value that is no number") {
    CHECK_FALSE(math_utils::percent_of_range_inverted(NOT_A_NUMBER, 0, 100).has_value());
    CHECK_FALSE(math_utils::percent_of_range_inverted(INFINITY_F, 0, 100).has_value());
    CHECK_FALSE(math_utils::percent_of_range_inverted(-INFINITY_F, 0, 100).has_value());
}

TEST_CASE("whole_count takes a whole number its type holds") {
    CHECK(math_utils::whole_count<std::uint16_t>(0.0).value() == 0);
    CHECK(math_utils::whole_count<std::uint16_t>(-0.0).value() == 0);
    CHECK(math_utils::whole_count<std::uint16_t>(2900.0).value() == 2900);
    CHECK(math_utils::whole_count<std::uint16_t>(65535.0).value() == 65535);
}

TEST_CASE("whole_count refuses a fraction, a negative and a number past its type") {
    CHECK_FALSE(math_utils::whole_count<std::uint16_t>(2900.5));
    CHECK_FALSE(math_utils::whole_count<std::uint16_t>(-1.0));
    CHECK_FALSE(math_utils::whole_count<std::uint16_t>(65536.0));
    CHECK_FALSE(math_utils::whole_count<std::uint16_t>(NOT_A_NUMBER));
    CHECK_FALSE(math_utils::whole_count<std::uint16_t>(INFINITY_F));
}

TEST_CASE("whole_count holds its bounds at the widest type") {
    // The largest double below 2^64, and 2^64 itself, which no std::uint64_t holds
    constexpr double largest_below{18446744073709549568.0};
    CHECK(math_utils::whole_count<std::uint64_t>(largest_below).value() == 18446744073709549568U);
    CHECK_FALSE(math_utils::whole_count<std::uint64_t>(18446744073709551616.0));
}

TEST_CASE("Ema seeds to its first sample") {
    math_utils::Ema ema{0.2f};
    CHECK(ema.update(42.0f).value() == doctest::Approx(42.0f));
}

TEST_CASE("Ema converges toward a new level after seeding") {
    math_utils::Ema ema{0.2f};
    std::ignore = ema.update(0.0f);  // Seed at 0
    // Each sample closes alpha of the gap to it: a fifth of 100, then a fifth
    // of the 80 still to go.
    CHECK(ema.update(100.0f).value() == doctest::Approx(20.0f));
    CHECK(ema.update(100.0f).value() == doctest::Approx(36.0f));
    for (int i{0}; i < 100; i++) {
        std::ignore = ema.update(100.0f);
    }
    CHECK(ema.update(100.0f).value() == doctest::Approx(100.0f));
}

TEST_CASE("Ema with alpha 0 stays at its seed") {
    math_utils::Ema ema{0.0f};
    std::ignore = ema.update(42.0f);  // Seed
    CHECK(ema.update(0.0f).value() == doctest::Approx(42.0f));
    CHECK(ema.update(100.0f).value() == doctest::Approx(42.0f));
}

TEST_CASE("Ema with alpha 1 tracks the input exactly") {
    math_utils::Ema ema{1.0f};
    std::ignore = ema.update(10.0f);  // Seed
    CHECK(ema.update(80.0f).value() == doctest::Approx(80.0f));
    CHECK(ema.update(25.0f).value() == doctest::Approx(25.0f));
}

TEST_CASE("Ema takes the weights at the edge of its bounds") {
    math_utils::Ema low{math_utils::Ema::MIN_ALPHA};
    CHECK(low.update(42.0f).has_value());
    math_utils::Ema high{math_utils::Ema::MAX_ALPHA};
    CHECK(high.update(42.0f).has_value());
}

TEST_CASE("Ema reports nothing when its weight cannot average") {
    // Above 1 overshoots, below 0 moves away, and NaN or infinity weigh nothing.
    // None is corrected, and update() returns nullopt instead.
    for (const float alpha : {2.0f, -1.0f, INFINITY_F, -INFINITY_F, NOT_A_NUMBER}) {
        CAPTURE(alpha);
        math_utils::Ema ema{alpha};
        CHECK_FALSE(ema.update(42.0f).has_value());
        CHECK_FALSE(ema.update(100.0f).has_value());  // Later updates fail on the same weight
    }
}

TEST_CASE("Ema reports nothing for a sample that carries no level") {
    math_utils::Ema ema{0.5f};
    std::ignore = ema.update(40.0f);  // Seed
    CHECK_FALSE(ema.update(NOT_A_NUMBER).has_value());
    CHECK_FALSE(ema.update(INFINITY_F).has_value());
    CHECK_FALSE(ema.update(-INFINITY_F).has_value());
    // The refusals left the average where it was.
    CHECK(ema.update(60.0f).value() == doctest::Approx(50.0f));
}

TEST_CASE("Ema seeds on the first sample it can use") {
    // An unusable sample is no seed either, or the average would start on a
    // level it can never leave.
    math_utils::Ema ema{0.2f};
    CHECK_FALSE(ema.update(NOT_A_NUMBER).has_value());
    CHECK(ema.update(42.0f).value() == doctest::Approx(42.0f));  // Seeds here
}

TEST_CASE("Ema seeds again after a reset") {
    math_utils::Ema ema{0.2f};
    std::ignore = ema.update(0.0f);  // Seed at 0
    ema.reset();
    CHECK(ema.update(100.0f).value() == doctest::Approx(100.0f));
    // And averages on from the new seed.
    CHECK(ema.update(0.0f).value() == doctest::Approx(80.0f));
}

TEST_CASE("is_in_percent_range holds the scale the sensor reports on") {
    CHECK(math_utils::is_in_percent_range(0.0f));
    CHECK(math_utils::is_in_percent_range(100.0f));
    CHECK(math_utils::is_in_percent_range(55.7f));
    CHECK_FALSE(math_utils::is_in_percent_range(-0.1f));
    CHECK_FALSE(math_utils::is_in_percent_range(100.1f));
}

TEST_CASE("is_in_percent_range refuses what is not a level at all") {
    CHECK_FALSE(math_utils::is_in_percent_range(NOT_A_NUMBER));
    CHECK_FALSE(math_utils::is_in_percent_range(INFINITY_F));
    CHECK_FALSE(math_utils::is_in_percent_range(-INFINITY_F));
}

TEST_CASE("nearest_tenth gives back every tenth of the percent scale unchanged") {
    for (int tenths{0}; tenths <= 1000; ++tenths) {
        const float as_float{static_cast<float>(tenths) / 10.0f};
        const double as_double{static_cast<double>(tenths) / 10.0};
        CAPTURE(tenths);
        CHECK(math_utils::nearest_tenth(as_float) == as_float);
        CHECK(math_utils::nearest_tenth(as_double) == as_double);
    }
}

TEST_CASE("nearest_tenth moves a finer value onto the tenth nearest it") {
    CHECK(math_utils::nearest_tenth(55.54f) == 55.5f);
    CHECK(math_utils::nearest_tenth(55.56f) == 55.6f);
    CHECK(math_utils::nearest_tenth(0.04f) == 0.0f);
    CHECK(math_utils::nearest_tenth(0.06f) == 0.1f);
    CHECK(math_utils::nearest_tenth(99.98f) == 100.0f);
    // Either side of zero, by the same distance
    CHECK(math_utils::nearest_tenth(-55.54f) == -55.5f);
    CHECK(math_utils::nearest_tenth(-55.56f) == -55.6f);
}

TEST_CASE("nearest_tenth never moves a value by more than half a tenth") {
    // Nothing lands further from what was asked than half a tenth.
    for (int hundredths{-10000}; hundredths <= 10000; ++hundredths) {
        const float as_float{static_cast<float>(hundredths) / 100.0f};
        const float moved{std::fabs(math_utils::nearest_tenth(as_float) - as_float)};
        CAPTURE(hundredths);
        CHECK(moved <= doctest::Approx(0.05f));
    }
}

TEST_CASE("what nearest_tenth returns is already a tenth") {
    // What it hands back must not move when fed in again.
    for (int hundredths{-10000}; hundredths <= 10000; ++hundredths) {
        const float rounded{math_utils::nearest_tenth(static_cast<float>(hundredths) / 100.0f)};
        CAPTURE(hundredths);
        CHECK(math_utils::nearest_tenth(rounded) == rounded);
    }
}

TEST_CASE("nearest_tenth rounds at any integer part") {
    for (int tenths{0}; tenths <= 400000; ++tenths) {
        const float as_float{static_cast<float>(tenths) / 10.0f};
        CAPTURE(tenths);
        CHECK(math_utils::nearest_tenth(as_float) == as_float);
    }
    // Sizes the type writes without a decimal digit at all
    CHECK(math_utils::nearest_tenth(1e30f) == 1e30f);
    CHECK(math_utils::nearest_tenth(-1e30f) == -1e30f);
}

TEST_CASE("nearest_tenth rounds like correct rounding across the whole type") {
    // Swept over the type's whole range. A prime stride still reaches every
    // exponent.
    const float threshold{1.0f / std::numeric_limits<float>::epsilon()};
    long long disagreements{0};
    for (std::uint64_t bits{0}; bits <= 0xFFFFFFFFULL; bits += 1021) {
        const float value{std::bit_cast<float>(static_cast<std::uint32_t>(bits))};
        if (!std::isfinite(value) || std::fabs(value) >= threshold) {
            continue;
        }
        if (math_utils::nearest_tenth(value) != std::round(value * 10.0f) / 10.0f) {
            ++disagreements;
        }
    }
    CHECK(disagreements == 0);
}

TEST_CASE("nearest_tenth does not let the rounding step round on its own") {
    // Adding a half would carry these up, because the addition rounds first.
    CHECK(math_utils::nearest_tenth(0.049999997f) == 0.0f);
    CHECK(math_utils::nearest_tenth(-0.049999997f) == 0.0f);
    for (int tenths{0}; tenths < 1000; ++tenths) {
        // The largest float below a midpoint belongs to the tenth beneath it
        const float midpoint{(static_cast<float>(tenths) + 0.5f) / 10.0f};
        const float just_below{std::nextafterf(midpoint, 0.0f)};
        CAPTURE(tenths);
        CHECK(math_utils::nearest_tenth(just_below) == std::round(just_below * 10.0f) / 10.0f);
    }
}

TEST_CASE("nearest_tenth settles on a positive zero") {
    // std::round would return -0 here, which prints as "-0.00".
    CHECK(std::signbit(math_utils::nearest_tenth(-0.04f)) == false);
    CHECK(std::signbit(math_utils::nearest_tenth(-0.0f)) == false);
    CHECK(std::signbit(math_utils::nearest_tenth(-0.001)) == false);
    CHECK(math_utils::nearest_tenth(-0.04f) == 0.0f);
}

TEST_CASE("nearest_tenth hands back what it cannot round") {
    // Passed through, since refusing them is the caller's range check.
    CHECK(std::isnan(math_utils::nearest_tenth(NOT_A_NUMBER)));
    CHECK(std::isinf(math_utils::nearest_tenth(INFINITY_F)));
    CHECK(std::isinf(math_utils::nearest_tenth(-INFINITY_F)));
    CHECK_FALSE(math_utils::is_in_percent_range(math_utils::nearest_tenth(NOT_A_NUMBER)));
    CHECK_FALSE(math_utils::is_in_percent_range(math_utils::nearest_tenth(INFINITY_F)));
    // Too large to hold a fraction, and already a tenth rather than overflowed.
    CHECK(
        math_utils::nearest_tenth(std::numeric_limits<float>::max()) ==
        std::numeric_limits<float>::max()
    );
    CHECK(
        math_utils::nearest_tenth(std::numeric_limits<double>::max()) ==
        std::numeric_limits<double>::max()
    );
}

TEST_CASE("decimal_digits counts the digits a value prints with") {
    CHECK(math_utils::decimal_digits(7) == 1);
    CHECK(math_utils::decimal_digits(9) == 1);
    CHECK(math_utils::decimal_digits(10) == 2);
    CHECK(math_utils::decimal_digits(1883) == 4);
    CHECK(math_utils::decimal_digits(65535) == 5);
    CHECK(math_utils::decimal_digits(std::numeric_limits<std::uint32_t>::max()) == 10);
}

TEST_CASE("decimal_digits counts zero as one digit") {
    CHECK(math_utils::decimal_digits(0) == 1);
}
