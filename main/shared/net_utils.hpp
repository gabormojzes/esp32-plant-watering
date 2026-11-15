#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Text forms of link-layer and IPv4 addresses
namespace net_utils {

inline constexpr std::size_t MAC_TEXT_BUFFER_SIZE{std::string_view{"aa:bb:cc:dd:ee:ff"}.size() + 1};
inline constexpr std::size_t IPV4_TEXT_BUFFER_SIZE{std::string_view{"255.255.255.255"}.size() + 1};

using MacAddress = std::array<std::uint8_t, 6>;
using Ipv4Address = std::array<std::uint8_t, 4>;
using MacText = std::array<char, MAC_TEXT_BUFFER_SIZE>;
using Ipv4Text = std::array<char, IPV4_TEXT_BUFFER_SIZE>;

// Six octets as lowercase hex pairs joined by colons, the IEEE 802 form
constexpr MacText format_mac(const MacAddress& mac) {
    constexpr std::string_view digits{"0123456789abcdef"};
    MacText text{};
    std::size_t next{0};
    for (const std::uint8_t octet : mac) {
        if (next > 0) {
            text[next++] = ':';
        }
        text[next++] = digits[octet >> 4];
        text[next++] = digits[octet & 0x0F];
    }
    return text;
}

// Four octets in dotted decimal, without leading zeros
constexpr Ipv4Text format_ipv4(const Ipv4Address& address) {
    Ipv4Text text{};
    std::size_t next{0};
    for (const std::uint8_t octet : address) {
        if (next > 0) {
            text[next++] = '.';
        }
        if (octet >= 100) {
            text[next++] = static_cast<char>('0' + (octet / 100));
        }
        if (octet >= 10) {
            text[next++] = static_cast<char>('0' + ((octet / 10) % 10));
        }
        text[next++] = static_cast<char>('0' + (octet % 10));
    }
    return text;
}

}  // namespace net_utils
