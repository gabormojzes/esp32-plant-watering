#include <doctest.h>

#include <cstdint>
#include <limits>

#include "shared/time_utils.hpp"

using constants::US_PER_S;

namespace {

// The most whole seconds a microsecond value can carry. Past it the conversion
// leaves int64 either way it is made.
constexpr std::int64_t MAX_WHOLE_S{std::numeric_limits<std::int64_t>::max() / US_PER_S};
// Neither end divides evenly, leaving a partial second to round at both
constexpr std::int64_t WIDEST_US{std::numeric_limits<std::int64_t>::max()};
constexpr std::int64_t NARROWEST_US{std::numeric_limits<std::int64_t>::min()};

constexpr double NOT_A_NUMBER{std::numeric_limits<double>::quiet_NaN()};
constexpr double INFINITY_S{std::numeric_limits<double>::infinity()};

}  // namespace

TEST_CASE("to_seconds_floor returns the whole seconds of an exact multiple") {
    CHECK(time_utils::to_seconds_floor(0) == 0);
    CHECK(time_utils::to_seconds_floor(US_PER_S) == 1);
    CHECK(time_utils::to_seconds_floor(90 * US_PER_S) == 90);
}

TEST_CASE("to_seconds_floor drops the partial second") {
    CHECK(time_utils::to_seconds_floor(1) == 0);                   // Just started
    CHECK(time_utils::to_seconds_floor(US_PER_S - 1) == 0);        // Just short of one
    CHECK(time_utils::to_seconds_floor(US_PER_S / 2) == 0);        // Half
    CHECK(time_utils::to_seconds_floor((2 * US_PER_S) + 1) == 2);  // Just past two
    CHECK(time_utils::to_seconds_floor((3 * US_PER_S) - 1) == 2);  // Just short of three
}

TEST_CASE("to_seconds_floor rounds a negative span down, not toward zero") {
    CHECK(time_utils::to_seconds_floor(-US_PER_S) == -1);              // Exact multiple
    CHECK(time_utils::to_seconds_floor(-1) == -1);                     // The smallest partial
    CHECK(time_utils::to_seconds_floor(-(US_PER_S / 2)) == -1);        // Half
    CHECK(time_utils::to_seconds_floor((-US_PER_S) - 1) == -2);        // Just past one
    CHECK(time_utils::to_seconds_floor((-(3 * US_PER_S)) / 2) == -2);  // One and a half
    CHECK(time_utils::to_seconds_floor((-(2 * US_PER_S)) + 1) == -2);  // Just short of two
}

TEST_CASE("to_seconds_floor carries the whole range") {
    CHECK(time_utils::to_seconds_floor(MAX_WHOLE_S * US_PER_S) == MAX_WHOLE_S);  // Exact multiple
    CHECK(time_utils::to_seconds_floor(WIDEST_US) == MAX_WHOLE_S);
    CHECK(time_utils::to_seconds_floor(NARROWEST_US) == (-MAX_WHOLE_S) - 1);  // Away from zero
}

TEST_CASE("to_seconds_ceil returns the whole seconds of an exact multiple") {
    CHECK(time_utils::to_seconds_ceil(0) == 0);  // Nothing to round up
    CHECK(time_utils::to_seconds_ceil(US_PER_S) == 1);
    CHECK(time_utils::to_seconds_ceil(90 * US_PER_S) == 90);
}

TEST_CASE("to_seconds_ceil counts any partial second as one") {
    CHECK(time_utils::to_seconds_ceil(1) == 1);                   // The smallest partial
    CHECK(time_utils::to_seconds_ceil(US_PER_S / 2) == 1);        // Half
    CHECK(time_utils::to_seconds_ceil(US_PER_S - 1) == 1);        // Just short of one
    CHECK(time_utils::to_seconds_ceil(US_PER_S + 1) == 2);        // Just past one
    CHECK(time_utils::to_seconds_ceil((2 * US_PER_S) + 1) == 3);  // Just past two
}

TEST_CASE("to_seconds_ceil reads zero only when nothing is left") {
    // The other side of counting any partial second as one, where only an empty
    // span rounds to nothing.
    CHECK(time_utils::to_seconds_ceil(0) == 0);
    CHECK(time_utils::to_seconds_ceil(1) != 0);
}

TEST_CASE("to_seconds_ceil rounds a negative span up, toward zero") {
    CHECK(time_utils::to_seconds_ceil(-US_PER_S) == -1);              // Exact multiple
    CHECK(time_utils::to_seconds_ceil(-1) == 0);                      // The smallest partial
    CHECK(time_utils::to_seconds_ceil(-(US_PER_S / 2)) == 0);         // Half
    CHECK(time_utils::to_seconds_ceil((-(3 * US_PER_S)) / 2) == -1);  // One and a half
    CHECK(time_utils::to_seconds_ceil((-(2 * US_PER_S)) - 1) == -2);  // Just past two
}

TEST_CASE("to_seconds_ceil carries the whole range") {
    CHECK(time_utils::to_seconds_ceil(MAX_WHOLE_S * US_PER_S) == MAX_WHOLE_S);  // Exact multiple
    CHECK(time_utils::to_seconds_ceil(WIDEST_US) == MAX_WHOLE_S + 1);  // Rounded away from zero
    CHECK(time_utils::to_seconds_ceil(NARROWEST_US) == -MAX_WHOLE_S);
}

TEST_CASE("whole_seconds_to_us converts an integral value") {
    CHECK(time_utils::whole_seconds_to_us(0.0).value() == 0);
    CHECK(time_utils::whole_seconds_to_us(1.0).value() == US_PER_S);
    CHECK(time_utils::whole_seconds_to_us(3600.0).value() == 3600 * US_PER_S);
    CHECK(
        time_utils::whole_seconds_to_us(-5.0).value() == (-5) * US_PER_S
    );  // Negatives convert too
}

TEST_CASE("whole_seconds_to_us reports nothing for a fractional value") {
    // Truncating would apply a different setting than the one sent.
    CHECK_FALSE(time_utils::whole_seconds_to_us(0.5).has_value());
    CHECK_FALSE(time_utils::whole_seconds_to_us(1.5).has_value());
    CHECK_FALSE(time_utils::whole_seconds_to_us(-0.5).has_value());
    CHECK_FALSE(time_utils::whole_seconds_to_us(3600.000001).has_value());  // Barely fractional
}

TEST_CASE("whole_seconds_to_us reports nothing past the microsecond range") {
    CHECK(
        time_utils::whole_seconds_to_us(static_cast<double>(MAX_WHOLE_S)).value() ==
        MAX_WHOLE_S * US_PER_S
    );
    CHECK_FALSE(time_utils::whole_seconds_to_us(static_cast<double>(MAX_WHOLE_S) + 1).has_value());
    // Refused at the same distance either side.
    CHECK(
        time_utils::whole_seconds_to_us(-static_cast<double>(MAX_WHOLE_S)).value() ==
        (-MAX_WHOLE_S) * US_PER_S
    );
    CHECK_FALSE(
        time_utils::whole_seconds_to_us((-static_cast<double>(MAX_WHOLE_S)) - 1).has_value()
    );
}

TEST_CASE("whole_seconds_to_us reports nothing for a non-finite value") {
    CHECK_FALSE(time_utils::whole_seconds_to_us(NOT_A_NUMBER).has_value());
    CHECK_FALSE(time_utils::whole_seconds_to_us(INFINITY_S).has_value());
    CHECK_FALSE(time_utils::whole_seconds_to_us(-INFINITY_S).has_value());
}

TEST_CASE("a value through whole_seconds_to_us comes back out of to_seconds_floor") {
    // Whole seconds round to themselves both ways.
    for (const double seconds : {0.0, 1.0, 60.0, 900.0, 3600.0, 2592000.0, -1.0, -3600.0}) {
        CAPTURE(seconds);
        const std::int64_t us{time_utils::whole_seconds_to_us(seconds).value()};
        CHECK(time_utils::to_seconds_floor(us) == static_cast<std::int64_t>(seconds));
        CHECK(time_utils::to_seconds_ceil(us) == static_cast<std::int64_t>(seconds));
    }
}
