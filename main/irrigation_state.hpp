#pragma once

#include <cstdint>

// One self-consistent view of the irrigation controller's state
struct IrrigationState {
    std::uint32_t run_count{0};
    std::int64_t current_run_time_us{0};
    std::int64_t idle_time_us{0};
    std::int64_t remaining_wait_time_us{0};
    std::int64_t total_run_time_us{0};
};
