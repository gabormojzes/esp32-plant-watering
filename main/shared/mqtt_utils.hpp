#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>

#include "shared/string_utils.hpp"

// The rules MQTT 3.1.1 (OASIS Standard, 29 October 2014) sets for the strings a
// client sends. Bracketed ids name the spec's normative statements.
namespace mqtt_utils {

// The longest UTF-8 encoded string, since its length travels in two bytes
// (section 1.5.3)
inline constexpr std::size_t MAX_STRING_LENGTH{65535};
// The longest client id every broker must accept [MQTT-3.1.3-5]
inline constexpr std::size_t MAX_CLIENT_ID_LENGTH{23};

// The non-characters of the Unicode Standard: U+FDD0 to U+FDEF, and the last two
// code points of every plane, U+FFFE and U+FFFF among them.
constexpr bool is_noncharacter(char32_t code_point) {
    return (code_point >= 0xFDD0 && code_point <= 0xFDEF) || (code_point & 0xFFFE) == 0xFFFE;
}

// A code point MQTT keeps out of a string. The null character is forbidden
// [MQTT-1.5.3-2]. The control characters U+0001 to U+001F and U+007F to U+009F
// and the non-characters SHOULD NOT appear, and a broker MAY close the
// connection over one (section 1.5.3).
constexpr bool is_unwanted_code_point(char32_t code_point) {
    return code_point <= 0x1F || (code_point >= 0x7F && code_point <= 0x9F) ||
           is_noncharacter(code_point);
}

// A UTF-8 encoded string as section 1.5.3 defines it: at most 65535 bytes,
// well-formed UTF-8 [MQTT-1.5.3-1], and no unwanted code point. A broker closes
// the connection over ill-formed UTF-8 or a null character.
constexpr bool is_valid_string(std::string_view text) {
    if (text.size() > MAX_STRING_LENGTH) {
        return false;
    }
    std::size_t offset{0};
    while (offset < text.size()) {
        const std::optional<string_utils::CodePoint> code_point{
            string_utils::decode_utf8(text, offset)
        };
        if (!code_point || is_unwanted_code_point(code_point->value)) {
            return false;
        }
        offset += code_point->length;
    }
    return true;
}

// A Topic Name, which a client publishes to and names its will with: a string
// [MQTT-4.7.3-3] of at least one character [MQTT-4.7.3-1], without the + and #
// wildcards [MQTT-4.7.1-1] [MQTT-3.3.2-2]. A leading $ is refused too, since
// topics starting with it belong to the broker and filters starting with a
// wildcard never match them [MQTT-4.7.2-1].
constexpr bool is_valid_topic_name(std::string_view topic) {
    return !topic.empty() && !topic.starts_with('$') && !topic.contains('+') &&
           !topic.contains('#') && is_valid_string(topic);
}

// The strictest client id: 1 to 23 letters and digits, which every broker must
// accept [MQTT-3.1.3-5]. Any other string is valid too [MQTT-3.1.3-4], but a
// broker may refuse it.
constexpr bool is_valid_client_id(std::string_view client_id) {
    return !client_id.empty() && client_id.size() <= MAX_CLIENT_ID_LENGTH &&
           std::ranges::all_of(client_id, string_utils::is_ascii_alphanumeric);
}

}  // namespace mqtt_utils
