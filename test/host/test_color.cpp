#include <doctest.h>

#include "shared/color.hpp"

TEST_CASE("the full level leaves a color as it is") {
    constexpr Color color{.red = 12, .green = 128, .blue = 255};
    CHECK(color.scaled(Color::MAX_LEVEL) == color);
}

TEST_CASE("level zero turns a color off") {
    CHECK(palette::WHITE.scaled(0) == palette::OFF);
}

TEST_CASE("each channel scales on its own") {
    constexpr Color color{.red = 255, .green = 128, .blue = 0};
    CHECK(color.scaled(128) == Color{.red = 128, .green = 64, .blue = 0});
}

TEST_CASE("a channel scaled below one step rounds down to off") {
    constexpr Color color{.red = 7, .green = 0, .blue = 0};
    CHECK(color.scaled(32) == palette::OFF);
}
