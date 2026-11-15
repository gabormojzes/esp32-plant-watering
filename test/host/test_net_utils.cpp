#include <doctest.h>

#include <string_view>

#include "shared/net_utils.hpp"

TEST_CASE("format_mac writes lowercase hex pairs joined by colons") {
    CHECK(
        std::string_view{net_utils::format_mac({0x00, 0x00, 0x5E, 0x00, 0x53, 0xAF}).data()} ==
        "00:00:5e:00:53:af"
    );
}

TEST_CASE("format_mac keeps the leading zero of every octet") {
    CHECK(
        std::string_view{net_utils::format_mac({0x00, 0x01, 0x0A, 0x10, 0x00, 0x00}).data()} ==
        "00:01:0a:10:00:00"
    );
}

TEST_CASE("format_mac fills the buffer at the widest address") {
    const net_utils::MacText text{net_utils::format_mac({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})};
    CHECK(std::string_view{text.data()} == "ff:ff:ff:ff:ff:ff");
    CHECK(text.back() == '\0');
}

TEST_CASE("format_ipv4 writes dotted decimal without leading zeros") {
    CHECK(std::string_view{net_utils::format_ipv4({192, 168, 1, 5}).data()} == "192.168.1.5");
    CHECK(std::string_view{net_utils::format_ipv4({10, 0, 0, 1}).data()} == "10.0.0.1");
    CHECK(std::string_view{net_utils::format_ipv4({0, 0, 0, 0}).data()} == "0.0.0.0");
}

TEST_CASE("format_ipv4 writes the two- and three-digit octets whole") {
    CHECK(std::string_view{net_utils::format_ipv4({100, 99, 10, 9}).data()} == "100.99.10.9");
    CHECK(
        std::string_view{net_utils::format_ipv4({109, 190, 200, 250}).data()} == "109.190.200.250"
    );
}

TEST_CASE("format_ipv4 fills the buffer at the widest address") {
    const net_utils::Ipv4Text text{net_utils::format_ipv4({255, 255, 255, 255})};
    CHECK(std::string_view{text.data()} == "255.255.255.255");
    CHECK(text.back() == '\0');
}
