#pragma once

#include <cstdint>

// Ordered with the worst of a set as the maximum.
enum class Severity : std::uint8_t {
    Warning,
    Error
};

struct Fault {
    const char* code{nullptr};
    Severity severity{};
    const char* message{nullptr};
};

// A fault and whether it still holds. Inactive means raised and since cleared.
struct FaultEntry {
    Fault fault{};
    bool active{false};
};
