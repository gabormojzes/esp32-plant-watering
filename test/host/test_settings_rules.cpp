#include <doctest.h>

#include <limits>

#include "settings_rules.hpp"

TEST_CASE("is_valid_percent admits tenths across the whole percent range") {
    CHECK(settings_rules::is_valid_percent(0.0));
    CHECK(settings_rules::is_valid_percent(55.6));
    CHECK(settings_rules::is_valid_percent(100.0));
    CHECK(settings_rules::is_valid_percent(55.6f));
}

TEST_CASE("is_valid_percent refuses a value finer than a tenth") {
    CHECK_FALSE(settings_rules::is_valid_percent(55.55));
    CHECK_FALSE(settings_rules::is_valid_percent(0.05f));
}

TEST_CASE("is_valid_percent refuses a tenth outside 0..100") {
    CHECK_FALSE(settings_rules::is_valid_percent(-0.1));
    CHECK_FALSE(settings_rules::is_valid_percent(100.1));
}

TEST_CASE("is_valid_percent refuses NaN and infinity") {
    CHECK_FALSE(settings_rules::is_valid_percent(std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(settings_rules::is_valid_percent(std::numeric_limits<double>::infinity()));
    CHECK_FALSE(settings_rules::is_valid_percent(-std::numeric_limits<double>::infinity()));
}
