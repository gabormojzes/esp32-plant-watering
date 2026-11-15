#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace json_utils {

// The lowest character a JSON string carries unescaped. The control
// characters below it are allowed only as escapes.
inline constexpr unsigned char FIRST_UNESCAPED{0x20};

// True when `text` holds no quote, backslash or control character, and can go
// into a JSON string unescaped.
constexpr bool is_safe_string(std::string_view text) {
    return std::ranges::none_of(text, [](char character) {
        // Compared unsigned, because a plain char is signed on some targets and
        // would put every byte of a UTF-8 sequence below the bound.
        return character == '"' || character == '\\' ||
               static_cast<unsigned char>(character) < FIRST_UNESCAPED;
    });
}

// True when `json` carries a NUL, raw or as \u0000. A decoded string ends at its
// first NUL, which reads shorter than was sent.
constexpr bool carries_nul(std::string_view json) {
    for (std::size_t i{0}; i < json.size(); ++i) {
        if (json[i] == '\0') {
            return true;
        }
        if (json[i] == '\\') {
            if (json.substr(i + 1, 5) == "u0000") {
                return true;
            }
            // Skips the escaped character, which keeps an escaped backslash
            // followed by the text u0000 from reading as a NUL.
            ++i;
        }
    }
    return false;
}

}  // namespace json_utils
