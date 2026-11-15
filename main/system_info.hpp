#pragma once

#include <optional>

#include "heap_info.hpp"

namespace system_info {

// The chip family the firmware was built for, which is the one it runs on
const char* chip_model();
// The silicon revision as vMajor.minor, fixed for the whole boot
const char* chip_revision();
// The leading hex digits of the firmware ELF's SHA-256, as a core dump records them
const char* firmware_build();
// The silicon's own temperature in °C, which runs above the air around it.
// Nullopt on a chip without the sensor, or when a reading fails.
std::optional<float> chip_temperature();
// Walks the internal and PSRAM heap regions, holding each one's lock while it does.
HeapInfo heap();
void log_memory_stats();

}  // namespace system_info
