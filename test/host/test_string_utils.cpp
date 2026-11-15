#include <doctest.h>

#include <cstddef>
#include <optional>
#include <string_view>

#include "shared/string_utils.hpp"

using namespace std::string_view_literals;

TEST_CASE("is_ascii_alphanumeric takes letters and digits and nothing else") {
    CHECK(string_utils::is_ascii_alphanumeric('a'));
    CHECK(string_utils::is_ascii_alphanumeric('z'));
    CHECK(string_utils::is_ascii_alphanumeric('A'));
    CHECK(string_utils::is_ascii_alphanumeric('Z'));
    CHECK(string_utils::is_ascii_alphanumeric('0'));
    CHECK(string_utils::is_ascii_alphanumeric('9'));
    CHECK_FALSE(string_utils::is_ascii_alphanumeric('-'));
    CHECK_FALSE(string_utils::is_ascii_alphanumeric(' '));
    CHECK_FALSE(string_utils::is_ascii_alphanumeric('\xC3'));
}

TEST_CASE("trimmed drops spaces and tabs at both ends and keeps them inside") {
    CHECK(string_utils::trimmed(" \tsome value\t ") == "some value");
    CHECK(string_utils::trimmed("value") == "value");
}

TEST_CASE("trimmed leaves nothing of text that is only whitespace") {
    CHECK(string_utils::trimmed(" \t ").empty());
    CHECK(string_utils::trimmed("").empty());
}

TEST_CASE("is_empty counts a null pointer and a bare terminator as empty") {
    CHECK(string_utils::is_empty(nullptr));
    CHECK(string_utils::is_empty(""));
}

TEST_CASE("is_empty counts any character as content, a space included") {
    CHECK_FALSE(string_utils::is_empty("a"));
    CHECK_FALSE(string_utils::is_empty(" "));
    CHECK_FALSE(string_utils::is_empty(" broker"));
}

TEST_CASE("is_constant_time_equal matches the same characters, empty text included") {
    CHECK(string_utils::is_constant_time_equal("token", "token"));
    CHECK(string_utils::is_constant_time_equal("", ""));
}

TEST_CASE("is_constant_time_equal refuses a difference at either end") {
    CHECK_FALSE(string_utils::is_constant_time_equal("token", "xoken"));
    CHECK_FALSE(string_utils::is_constant_time_equal("token", "tokex"));
}

TEST_CASE("is_constant_time_equal refuses a prefix, either way round") {
    CHECK_FALSE(string_utils::is_constant_time_equal("tok", "token"));
    CHECK_FALSE(string_utils::is_constant_time_equal("token", "tok"));
    CHECK_FALSE(string_utils::is_constant_time_equal("", "token"));
}

TEST_CASE("is_constant_time_equal compares bytes, a NUL and bytes above 0x7F included") {
    CHECK(string_utils::is_constant_time_equal("a\x00b"sv, "a\x00b"sv));
    CHECK_FALSE(string_utils::is_constant_time_equal("a\x00b"sv, "a\x00c"sv));
    CHECK_FALSE(string_utils::is_constant_time_equal("\xC3\xA9", "\xC3\xA8"));
}

namespace {

// The code point decode_utf8 reads at the start of `text`, or -1 when it reads none
long first_code_point(std::string_view text) {
    const std::optional<string_utils::CodePoint> code_point{string_utils::decode_utf8(text, 0)};
    return code_point ? static_cast<long>(code_point->value) : -1;
}

std::size_t first_length(std::string_view text) {
    return string_utils::decode_utf8(text, 0)->length;
}

}  // namespace

TEST_CASE("decode_utf8 reads each length at both ends of its range") {
    CHECK(first_code_point("\x00"sv) == 0x00);
    CHECK(first_code_point("\x7F") == 0x7F);
    CHECK(first_code_point("\xC2\x80") == 0x80);
    CHECK(first_code_point("\xDF\xBF") == 0x7FF);
    CHECK(first_code_point("\xE0\xA0\x80") == 0x800);
    CHECK(first_code_point("\xED\x9F\xBF") == 0xD7FF);
    CHECK(first_code_point("\xEE\x80\x80") == 0xE000);
    CHECK(first_code_point("\xEF\xBF\xBF") == 0xFFFF);
    CHECK(first_code_point("\xF0\x90\x80\x80") == 0x10000);
    CHECK(first_code_point("\xF4\x8F\xBF\xBF") == 0x10FFFF);
}

TEST_CASE("decode_utf8 reports how many bytes the code point takes") {
    CHECK(first_length("a") == 1);
    CHECK(first_length("\xC3\xA9") == 2);
    CHECK(first_length("\xE2\x82\xAC") == 3);
    CHECK(first_length("\xF0\x9F\x8C\xB1") == 4);
}

TEST_CASE("decode_utf8 reads the code point at the given offset") {
    const std::optional<string_utils::CodePoint> code_point{
        string_utils::decode_utf8("caf\xC3\xA9", 3)
    };
    REQUIRE(code_point);
    CHECK(code_point->value == 0xE9);
}

TEST_CASE("decode_utf8 refuses bytes that start no sequence") {
    CHECK(first_code_point("\x80") == -1);
    CHECK(first_code_point("\xBF") == -1);
    CHECK(first_code_point("\xC0\x80") == -1);
    CHECK(first_code_point("\xC1\xBF") == -1);
    CHECK(first_code_point("\xF5\x80\x80\x80") == -1);
    CHECK(first_code_point("\xFF") == -1);
}

TEST_CASE("decode_utf8 refuses overlong forms, surrogates and code points past U+10FFFF") {
    CHECK(first_code_point("\xE0\x9F\xBF") == -1);
    CHECK(first_code_point("\xF0\x8F\xBF\xBF") == -1);
    CHECK(first_code_point("\xED\xA0\x80") == -1);
    CHECK(first_code_point("\xED\xBF\xBF") == -1);
    CHECK(first_code_point("\xF4\x90\x80\x80") == -1);
}

TEST_CASE("decode_utf8 refuses a sequence cut short or broken by a wrong byte") {
    CHECK(first_code_point("\xC3") == -1);
    CHECK(first_code_point("\xE2\x82") == -1);
    CHECK(first_code_point("\xF0\x9F\x8C") == -1);
    CHECK(first_code_point("\xC3\x41") == -1);
    CHECK(first_code_point("\xE2\x82\xC0") == -1);
}
