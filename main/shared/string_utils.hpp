#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace string_utils {

constexpr bool is_empty(const char* text) {
    return text == nullptr || text[0] == '\0';
}

constexpr bool is_ascii_alphanumeric(char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z');
}

// Returns `text` without leading or trailing spaces and tabs. The view points
// into `text`.
constexpr std::string_view trimmed(std::string_view text) {
    constexpr std::string_view whitespace{" \t"};
    const std::size_t begin{text.find_first_not_of(whitespace)};
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end{text.find_last_not_of(whitespace)};
    return text.substr(begin, end - begin + 1);
}

// True when both hold the same characters. Text of equal length is compared in
// full, which keeps the time taken from revealing how much of a secret matched.
// Text of unequal length is refused at once, since a length is no secret.
constexpr bool is_constant_time_equal(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    unsigned char difference{0};
    for (std::size_t i{0}; i < left.size(); ++i) {
        difference |= static_cast<unsigned char>(left[i]) ^ static_cast<unsigned char>(right[i]);
    }
    return difference == 0;
}

struct CodePoint {
    char32_t value{0};
    // Bytes its UTF-8 encoding takes, 1 to 4
    std::size_t length{0};
};

// Decodes the code point whose UTF-8 encoding starts at `offset`. Nullopt when
// the bytes there are not one of the well-formed sequences of the Unicode
// Standard's Table 3-7, which leaves out overlong forms, surrogates and
// anything past U+10FFFF. `offset` must be inside `text`.
constexpr std::optional<CodePoint> decode_utf8(std::string_view text, std::size_t offset) {
    const auto byte_at{[text](std::size_t index) {
        return static_cast<unsigned char>(text[index]);
    }};
    const unsigned char lead{byte_at(offset)};
    if (lead <= 0x7F) {
        return CodePoint{.value = lead, .length = 1};
    }

    // Each lead byte fixes the length and the range its second byte may take.
    // Every later byte runs 0x80 to 0xBF.
    std::size_t length{0};
    unsigned char second_min{0x80};
    unsigned char second_max{0xBF};
    char32_t value{0};
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        value = lead & 0x1Fu;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        value = lead & 0x0Fu;
        if (lead == 0xE0) {
            // E0 80 to E0 9F would be overlong.
            second_min = 0xA0;
        } else if (lead == 0xED) {
            // ED A0 to ED BF would encode the surrogates.
            second_max = 0x9F;
        }
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        value = lead & 0x07u;
        if (lead == 0xF0) {
            // F0 80 to F0 8F would be overlong.
            second_min = 0x90;
        } else if (lead == 0xF4) {
            // F4 90 and above would encode past U+10FFFF.
            second_max = 0x8F;
        }
    } else {
        // 0x80 to 0xC1 and 0xF5 to 0xFF start no well-formed sequence.
        return std::nullopt;
    }

    if (text.size() - offset < length) {
        return std::nullopt;
    }
    for (std::size_t index{1}; index < length; ++index) {
        const unsigned char byte{byte_at(offset + index)};
        const unsigned char min{index == 1 ? second_min : static_cast<unsigned char>(0x80)};
        const unsigned char max{index == 1 ? second_max : static_cast<unsigned char>(0xBF)};
        if (byte < min || byte > max) {
            return std::nullopt;
        }
        value = (value << 6) | (byte & 0x3Fu);
    }
    return CodePoint{.value = value, .length = length};
}

}  // namespace string_utils
