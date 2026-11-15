#pragma once

#include <array>
#include <cstddef>

// One encoded document in transit through a messaging transport
struct Payload {
    // Fits the widest telemetry frame, a full fault list included, with headroom
    // for new fields.
    static constexpr std::size_t BUFFER_SIZE{2304};

    int length{0};
    // Only the first `length` bytes are valid
    std::array<char, BUFFER_SIZE> data{};
};
