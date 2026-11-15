#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

// The transports a build can list. An empty list switches messaging off.
enum class MessagingTransport : std::uint8_t {
    Mqtt,
    Http
};

constexpr bool contains(
    std::span<const MessagingTransport> transports, MessagingTransport transport
) {
    return std::ranges::contains(transports, transport);
}
