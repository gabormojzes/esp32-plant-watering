#include <doctest.h>

#include <array>
#include <string_view>

#include "shared/mqtt_utils.hpp"

using namespace std::string_view_literals;

namespace {

// One character past the longest string MQTT allows
std::array<char, mqtt_utils::MAX_STRING_LENGTH + 1> past_longest(char character) {
    std::array<char, mqtt_utils::MAX_STRING_LENGTH + 1> text{};
    text.fill(character);
    return text;
}

}  // namespace

TEST_CASE("is_noncharacter finds both non-character ranges") {
    CHECK(mqtt_utils::is_noncharacter(0xFDD0));
    CHECK(mqtt_utils::is_noncharacter(0xFDEF));
    CHECK(mqtt_utils::is_noncharacter(0xFFFE));
    CHECK(mqtt_utils::is_noncharacter(0xFFFF));
    CHECK(mqtt_utils::is_noncharacter(0x1FFFE));
    CHECK(mqtt_utils::is_noncharacter(0x10FFFF));
}

TEST_CASE("is_noncharacter passes the code points around them") {
    CHECK_FALSE(mqtt_utils::is_noncharacter(0xFDCF));
    CHECK_FALSE(mqtt_utils::is_noncharacter(0xFDF0));
    CHECK_FALSE(mqtt_utils::is_noncharacter(0xFFFD));
    CHECK_FALSE(mqtt_utils::is_noncharacter(0x10000));
    CHECK_FALSE(mqtt_utils::is_noncharacter('a'));
}

TEST_CASE("is_unwanted_code_point finds the null character and both control ranges") {
    CHECK(mqtt_utils::is_unwanted_code_point(0x00));
    CHECK(mqtt_utils::is_unwanted_code_point(0x01));
    CHECK(mqtt_utils::is_unwanted_code_point('\t'));
    CHECK(mqtt_utils::is_unwanted_code_point(0x1F));
    CHECK(mqtt_utils::is_unwanted_code_point(0x7F));
    CHECK(mqtt_utils::is_unwanted_code_point(0x9F));
    CHECK(mqtt_utils::is_unwanted_code_point(0xFFFF));
}

TEST_CASE("is_unwanted_code_point passes the printable code points beside them") {
    CHECK_FALSE(mqtt_utils::is_unwanted_code_point(' '));
    CHECK_FALSE(mqtt_utils::is_unwanted_code_point('~'));
    CHECK_FALSE(mqtt_utils::is_unwanted_code_point(0xA0));
    // U+FEFF, which MQTT keeps as a character [MQTT-1.5.3-3]
    CHECK_FALSE(mqtt_utils::is_unwanted_code_point(0xFEFF));
}

TEST_CASE("is_valid_string takes well-formed UTF-8 free of unwanted code points") {
    CHECK(mqtt_utils::is_valid_string(""));
    CHECK(mqtt_utils::is_valid_string("home/device1/status"));
    CHECK(mqtt_utils::is_valid_string("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x8C\xB1"));
}

TEST_CASE("is_valid_string refuses ill-formed UTF-8") {
    CHECK_FALSE(mqtt_utils::is_valid_string("a\xFF"));
    CHECK_FALSE(mqtt_utils::is_valid_string("caf\xC3"));
    CHECK_FALSE(mqtt_utils::is_valid_string("\xED\xA0\x80"));
}

TEST_CASE("is_valid_string refuses the null character, controls and non-characters") {
    CHECK_FALSE(mqtt_utils::is_valid_string("a\0b"sv));
    CHECK_FALSE(mqtt_utils::is_valid_string("a\tb"));
    CHECK_FALSE(mqtt_utils::is_valid_string("a\nb"));
    CHECK_FALSE(mqtt_utils::is_valid_string("a\x7F"));
    CHECK_FALSE(mqtt_utils::is_valid_string("\xC2\x85"));
    CHECK_FALSE(mqtt_utils::is_valid_string("\xEF\xBF\xBF"));
}

TEST_CASE("is_valid_string holds a string to 65535 bytes") {
    const std::array<char, mqtt_utils::MAX_STRING_LENGTH + 1> text{past_longest('s')};
    CHECK(mqtt_utils::is_valid_string({text.data(), mqtt_utils::MAX_STRING_LENGTH}));
    CHECK_FALSE(mqtt_utils::is_valid_string({text.data(), text.size()}));
}

TEST_CASE("is_valid_topic_name takes the topics a client may publish to") {
    CHECK(mqtt_utils::is_valid_topic_name("home/device1/telemetry"));
    CHECK(mqtt_utils::is_valid_topic_name("/leading/and/trailing/"));
    CHECK(mqtt_utils::is_valid_topic_name("with space"));
    CHECK(mqtt_utils::is_valid_topic_name("caf\xC3\xA9"));
    CHECK(mqtt_utils::is_valid_topic_name("home/$/device"));
    CHECK(mqtt_utils::is_valid_topic_name("x"));
}

TEST_CASE("is_valid_topic_name refuses an empty topic, wildcards and a leading $") {
    CHECK_FALSE(mqtt_utils::is_valid_topic_name(""));
    CHECK_FALSE(mqtt_utils::is_valid_topic_name("home/+/telemetry"));
    CHECK_FALSE(mqtt_utils::is_valid_topic_name("home/#"));
    CHECK_FALSE(mqtt_utils::is_valid_topic_name("$SYS/device"));
}

TEST_CASE("is_valid_topic_name refuses what is no string") {
    CHECK_FALSE(mqtt_utils::is_valid_topic_name("home/device1\t"));
    CHECK_FALSE(mqtt_utils::is_valid_topic_name("home/\xFF"));
    const std::array<char, mqtt_utils::MAX_STRING_LENGTH + 1> topic{past_longest('t')};
    CHECK_FALSE(mqtt_utils::is_valid_topic_name({topic.data(), topic.size()}));
}

TEST_CASE("is_valid_client_id takes 1 to 23 letters and digits") {
    CHECK(mqtt_utils::is_valid_client_id("Device1"));
    CHECK(mqtt_utils::is_valid_client_id("x"));
    CHECK(mqtt_utils::is_valid_client_id("abcdefghijklmnopqrstuvw"));
    CHECK(mqtt_utils::is_valid_client_id("0123456789ABCDEFGHIJKLM"));
}

TEST_CASE("is_valid_client_id refuses an empty id, a longer one and any other character") {
    CHECK_FALSE(mqtt_utils::is_valid_client_id(""));
    CHECK_FALSE(mqtt_utils::is_valid_client_id("abcdefghijklmnopqrstuvwx"));
    CHECK_FALSE(mqtt_utils::is_valid_client_id("plant-1"));
    CHECK_FALSE(mqtt_utils::is_valid_client_id("plant_1"));
    CHECK_FALSE(mqtt_utils::is_valid_client_id("plant 1"));
    CHECK_FALSE(mqtt_utils::is_valid_client_id("caf\xC3\xA9"));
}
