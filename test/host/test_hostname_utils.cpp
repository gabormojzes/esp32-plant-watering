#include <doctest.h>

#include <array>
#include <string_view>

#include "shared/hostname_utils.hpp"

TEST_CASE("is_valid_hostname takes letters, digits and inner hyphens") {
    CHECK(hostname_utils::is_valid_hostname("a"));
    CHECK(hostname_utils::is_valid_hostname("plant-watering"));
    CHECK(hostname_utils::is_valid_hostname("Plant-Watering-2"));
    CHECK(hostname_utils::is_valid_hostname("a--b"));
}

TEST_CASE("is_valid_hostname takes a leading digit, as RFC 1123 allows") {
    CHECK(hostname_utils::is_valid_hostname("2nd-pot"));
    CHECK(hostname_utils::is_valid_hostname("42"));
}

TEST_CASE("is_valid_hostname refuses an empty name") {
    CHECK_FALSE(hostname_utils::is_valid_hostname(""));
}

TEST_CASE("is_valid_hostname refuses a hyphen at either end") {
    CHECK_FALSE(hostname_utils::is_valid_hostname("-pot"));
    CHECK_FALSE(hostname_utils::is_valid_hostname("pot-"));
    CHECK_FALSE(hostname_utils::is_valid_hostname("-"));
}

TEST_CASE("is_valid_hostname refuses any other character") {
    CHECK_FALSE(hostname_utils::is_valid_hostname("plant watering"));
    CHECK_FALSE(hostname_utils::is_valid_hostname("plant_watering"));
    CHECK_FALSE(hostname_utils::is_valid_hostname("plant.local"));
    CHECK_FALSE(hostname_utils::is_valid_hostname("caf\xC3\xA9"));
}

TEST_CASE("is_valid_hostname takes 63 characters and refuses 64") {
    std::array<char, hostname_utils::MAX_LABEL_LENGTH + 1> name{};
    name.fill('a');
    CHECK(hostname_utils::is_valid_hostname({name.data(), hostname_utils::MAX_LABEL_LENGTH}));
    CHECK_FALSE(hostname_utils::is_valid_hostname({name.data(), name.size()}));
}
