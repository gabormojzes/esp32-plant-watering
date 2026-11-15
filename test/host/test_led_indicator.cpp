#include <doctest.h>

#include <cstdint>

#include "indicator/led_indicator.hpp"
#include "indicator/status.hpp"
#include "shared/color.hpp"
#include "shared/constants.hpp"

using constants::US_PER_MS;

namespace {

// The indicator's blink half-period, on at phase 0 and off at the next half
constexpr std::int64_t BLINK_HALF_US{250 * US_PER_MS};

}  // namespace

TEST_CASE("booting is steady blue") {
    CHECK(LedIndicator::color_for(Status::Booting, 0) == palette::BLUE);
    CHECK(LedIndicator::color_for(Status::Booting, BLINK_HALF_US) == palette::BLUE);
}

TEST_CASE("nominal is steady green") {
    CHECK(LedIndicator::color_for(Status::Nominal, 0) == palette::GREEN);
    CHECK(LedIndicator::color_for(Status::Nominal, BLINK_HALF_US) == palette::GREEN);
}

TEST_CASE("a warning blinks yellow, never steady") {
    CHECK(LedIndicator::color_for(Status::Warning, 0) == palette::YELLOW);
    CHECK(LedIndicator::color_for(Status::Warning, BLINK_HALF_US) == palette::OFF);
    CHECK(LedIndicator::color_for(Status::Warning, 2 * BLINK_HALF_US) == palette::YELLOW);
}

TEST_CASE("an error blinks red, never steady") {
    CHECK(LedIndicator::color_for(Status::Error, 0) == palette::RED);
    CHECK(LedIndicator::color_for(Status::Error, BLINK_HALF_US) == palette::OFF);
    CHECK(LedIndicator::color_for(Status::Error, 2 * BLINK_HALF_US) == palette::RED);
}
