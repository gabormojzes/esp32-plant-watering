#include <doctest.h>

#include "shared/json_utils.hpp"

TEST_CASE("is_safe_string accepts text a JSON string carries as it stands") {
    CHECK(json_utils::is_safe_string(""));
    CHECK(json_utils::is_safe_string("Device 1"));
    CHECK(json_utils::is_safe_string("/plants/device-1_a.b"));
}

TEST_CASE("is_safe_string accepts the bytes a multi-byte character is spelled in") {
    CHECK(json_utils::is_safe_string("caf\xC3\xA9"));
    CHECK(json_utils::is_safe_string("\xE6\xA4\x8D\xE7\x89\xA9"));
}

TEST_CASE("is_safe_string rejects a quote, wherever it sits") {
    CHECK_FALSE(json_utils::is_safe_string("\""));
    CHECK_FALSE(json_utils::is_safe_string("\"Device 1"));
    CHECK_FALSE(json_utils::is_safe_string("Device \"1\""));
    CHECK_FALSE(json_utils::is_safe_string("Device 1\""));
}

TEST_CASE("is_safe_string rejects a backslash, wherever it sits") {
    CHECK_FALSE(json_utils::is_safe_string("\\"));
    CHECK_FALSE(json_utils::is_safe_string("\\Device 1"));
    CHECK_FALSE(json_utils::is_safe_string("Device\\1"));
    CHECK_FALSE(json_utils::is_safe_string("Device 1\\"));
}

TEST_CASE("is_safe_string rejects a control character, wherever it sits") {
    CHECK_FALSE(json_utils::is_safe_string("\t"));
    CHECK_FALSE(json_utils::is_safe_string("\nDevice 1"));
    CHECK_FALSE(json_utils::is_safe_string("Device\t1"));
    CHECK_FALSE(json_utils::is_safe_string("Device 1\r"));
    CHECK_FALSE(json_utils::is_safe_string(std::string_view{"Device\0 1", 9}));
}

TEST_CASE("is_safe_string takes the space the control characters end at") {
    CHECK(json_utils::is_safe_string(" "));
}

TEST_CASE("carries_nul finds a NUL spelled as an escape, wherever it sits") {
    CHECK(json_utils::carries_nul(R"("\u0000")"));
    CHECK(json_utils::carries_nul(R"({"password":"pass\u0000junk"})"));
    CHECK(json_utils::carries_nul(R"({"user":"\u0000"})"));
    CHECK(json_utils::carries_nul(R"(\u0000)"));
}

TEST_CASE("carries_nul finds a raw NUL, wherever it sits") {
    constexpr char inside[]{"{\"password\":\"pass\0junk\"}"};
    CHECK(json_utils::carries_nul(std::string_view{"\0", 1}));
    CHECK(json_utils::carries_nul(std::string_view{inside, std::size(inside) - 1}));
}

// Backslashes escape each other in pairs, which makes u0000 an escape only behind
// an odd run of them.
TEST_CASE("carries_nul reads u0000 behind an even run of backslashes as text") {
    CHECK_FALSE(json_utils::carries_nul(R"("\\u0000")"));
    CHECK_FALSE(json_utils::carries_nul(R"("\\\\u0000")"));
    CHECK(json_utils::carries_nul(R"("\\\u0000")"));
}

TEST_CASE("carries_nul passes JSON without a NUL") {
    CHECK_FALSE(json_utils::carries_nul(""));
    CHECK_FALSE(json_utils::carries_nul(R"({"user":"plant","password":"watering"})"));
    CHECK_FALSE(json_utils::carries_nul(R"("\u0001\u0010")"));
    CHECK_FALSE(json_utils::carries_nul(R"("u0000")"));
    CHECK_FALSE(json_utils::carries_nul(R"("\"u0000")"));
}

TEST_CASE("carries_nul passes an escape cut short at the end") {
    CHECK_FALSE(json_utils::carries_nul(R"(\u000)"));
    CHECK_FALSE(json_utils::carries_nul(R"(\)"));
}
