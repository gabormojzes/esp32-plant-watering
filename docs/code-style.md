# Code style

[`.clang-tidy`](../.clang-tidy) checks the case of identifiers and
[`.clang-format`](../.clang-format) the formatting. CI runs both (see [Testing](testing.md)).

## Names

| What | Style | Example |
|---|---|---|
| Source files and their folders | `snake_case` | `irrigation_controller.hpp`, `fuzz_seeds/` |
| Classes, structs, enums, type aliases | `CamelCase` | `IrrigationController`, `SensorSnapshot` |
| Interfaces | no `Interface` or `I` affix | `Indicator`, `WebHost` |
| Null objects | `Null` prefix | `NullIndicator` |
| Enum values | `CamelCase` | `Phase::Waiting`, `WifiMode::ApSta` |
| Functions and methods | `snake_case` | `remaining_wait_time()` |
| Predicates on an argument | `is_` and what the argument is, any adjective first, or whatever else reads as a statement about it | `is_pem(text)`, `is_valid_client_id(client_id)`, `has_sta(mode)`, `carries_nul(json)`, `holds(id)`, `plausible(raw)` |
| Parameters and locals, `constexpr` locals too | `snake_case` | `now_us`, `sensor_snapshot` |
| Private data members | `snake_case_` | `irrigation_policy_`, `last_start_us_` |
| Public struct fields | `snake_case` | `boot_lockout_us` |
| Constants at namespace or class scope | `UPPER_CASE` | `MAX_FAULTS`, `SETTINGS_VERSION` |
| Namespaces | `snake_case` | `math_utils`, `config` |
| Tests | `test_` prefix | `test_irrigation_controller.cpp` |
| Fuzz targets | `fuzz_` prefix | `fuzz_command.cpp` |
| Test cases | a full sentence | `"should_start holds off on a wet reading"` |
| Time units | last, such as `_us`, `_ms`, `_s`; a function names one only when converting to it | `boot_lockout_us`, `period_ms`, `pump_wait_time()`, `to_seconds_floor()` |
| Sizes and lengths | `X_BUFFER_SIZE` sizes an array, `MAX_X_SIZE` caps one it does not hold, terminator included, and `MAX_X_LENGTH` caps text without its terminator | `BODY_BUFFER_SIZE`, `MAX_DEVICE_NAME_SIZE`, `MAX_CLIENT_ID_LENGTH` |
| Acronyms | written as words | `GpioOutput`, `NvsManager` |
| Orchestrators in `main/` | agent nouns | `IrrigationController`, `SensorSampler` |
| `Manager` | a resource wrapper or a dispatch hub | `NvsManager`, `FaultManager` |
| Members | name the object they hold | `sensor_sampler_`, not `sensor_` |
| ESP-IDF stand-ins in `test/host/stubs/` | ESP-IDF's own names and type declarations | `freertos/queue.h` |

## Layout

| What | Rule | Example |
|---|---|---|
| Headers | `.hpp`, with `#pragma once` where it can be | |
| Sources | `.cpp` | |
| Include brackets | angled for the standard library and `.h`, quoted for our `.hpp` | `<cstdint>`, `<esp_log.h>`, `"hardware/pump.hpp"` |
| Include order | own header, `.h`, standard library, ours; each a group, conditional ones last | |
| File-local names | unnamed namespace, not `static` | `namespace { constexpr char TAG[]{"WATCHDOG"}; }` |
| Indent | four spaces, none inside a namespace | |
| Line length | 100 columns as a guide | |
| Initialization | braces | `std::uint32_t run_count_{0};` |
| `[[nodiscard]]` | on a function that reports whether its work succeeded, or that changes state or fills a buffer and returns what it produced; not on one that only computes or observes, nor where dropping the result is a normal use; a result dropped on purpose goes to `std::ignore` | `std::ignore = nvs_.commit();` |
| Numbers | no digit separators, times from the unit constants | `86400 * constants::US_PER_S` |
