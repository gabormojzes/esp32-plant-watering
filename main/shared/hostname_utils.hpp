#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "shared/string_utils.hpp"

// The hostname rules of RFC 952 as RFC 1123 (section 2.1) relaxes them, for a
// name of a single label. DHCP carries a client's name under the same character
// rules (RFC 2132, section 3.14).
namespace hostname_utils {

// The longest label (RFC 1035, section 2.3.4)
inline constexpr std::size_t MAX_LABEL_LENGTH{63};

// A hostname of one label: 1 to 63 letters, digits and hyphens (RFC 952),
// starting with a letter or digit (RFC 1123, section 2.1) and not ending with a
// hyphen (RFC 952).
constexpr bool is_valid_hostname(std::string_view hostname) {
    return !hostname.empty() && hostname.size() <= MAX_LABEL_LENGTH && hostname.front() != '-' &&
           hostname.back() != '-' && std::ranges::all_of(hostname, [](char character) {
               return string_utils::is_ascii_alphanumeric(character) || character == '-';
           });
}

}  // namespace hostname_utils
