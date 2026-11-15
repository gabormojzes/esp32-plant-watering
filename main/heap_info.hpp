#pragma once

#include <cstdint>
#include <optional>

// One kind of heap memory, in bytes apart from the block count
struct HeapUsage {
    // Every region's whole size, the heap's own bookkeeping included, which keeps
    // free below it
    std::uint32_t total{0};
    std::uint32_t free{0};
    // The lowest free since boot. Each region keeps its own low, reached at its
    // own moment, and their sum can read a little low but never high.
    std::uint32_t min_free{0};
    // The largest single allocation that can succeed now
    std::uint32_t largest_free_block{0};
    std::uint32_t free_blocks{0};
};

struct HeapInfo {
    // Byte-addressable internal RAM
    HeapUsage internal{};
    // Empty when no PSRAM is in the heap
    std::optional<HeapUsage> psram;
};
