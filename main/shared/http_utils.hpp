#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "shared/media_types.hpp"
#include "shared/string_utils.hpp"

namespace http_utils {

// The tokens HTTP matches case-insensitively are ASCII by definition, and no
// locale is involved.
constexpr char to_lower_ascii(char character) {
    if (character >= 'A' && character <= 'Z') {
        return static_cast<char>(character + ('a' - 'A'));
    }
    return character;
}

constexpr bool is_ascii_case_insensitive_equal(std::string_view left, std::string_view right) {
    return std::ranges::equal(left, right, [](char left_character, char right_character) {
        return to_lower_ascii(left_character) == to_lower_ascii(right_character);
    });
}

// Returns the type/subtype of a Content-Type value, without padding or
// parameters. Empty when the value names none. The view points into
// `content_type`.
constexpr std::string_view media_type(std::string_view content_type) {
    constexpr std::string_view whitespace{" \t"};
    constexpr std::string_view type_end{"; \t"};

    const std::size_t begin{content_type.find_first_not_of(whitespace)};
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end{content_type.find_first_of(type_end, begin)};
    const std::size_t length{end == std::string_view::npos ? std::string_view::npos : end - begin};
    return content_type.substr(begin, length);
}

// Case-insensitive, as RFC 9110 specifies, and compared whole, since
// application/jsonl starts like application/json without being it.
constexpr bool is_media_type(std::string_view content_type, std::string_view expected) {
    return is_ascii_case_insensitive_equal(media_type(content_type), expected);
}

// True for the media types the Fetch standard lets a cross-origin page post
// without a preflight.
constexpr bool is_cors_safelisted_media_type(std::string_view content_type) {
    return is_media_type(content_type, media_types::TEXT_PLAIN) ||
           is_media_type(content_type, media_types::APPLICATION_FORM_URLENCODED) ||
           is_media_type(content_type, media_types::MULTIPART_FORM_DATA);
}

// The token68 form RFC 9110 gives a credential after the auth scheme. It is one or
// more letters, digits or -._~+/, followed by any number of = as padding.
constexpr bool is_token68(std::string_view text) {
    const std::size_t body_end{text.find_last_not_of('=')};
    // Empty text, or padding with nothing before it, carries no token.
    if (body_end == std::string_view::npos) {
        return false;
    }
    return std::ranges::all_of(text.substr(0, body_end + 1), [](char character) {
        constexpr std::string_view punctuation{"-._~+/"};
        return string_utils::is_ascii_alphanumeric(character) || punctuation.contains(character);
    });
}

// True when a Sec-Fetch-Site value names another origin as the request's
// initiator, on another site or the same one. "same-origin" and "none", a
// request the user started, are not.
constexpr bool is_from_another_origin(std::string_view sec_fetch_site) {
    return sec_fetch_site == "cross-site" || sec_fetch_site == "same-site";
}

}  // namespace http_utils
