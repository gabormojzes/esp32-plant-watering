#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <tuple>

#include "messaging/command.hpp"
#include "messaging/command_handler.hpp"
#include "messaging/json_codec.hpp"
#include "messaging/payload.hpp"
#include "runtime_settings.hpp"
#include "settings_rules.hpp"
#include "shared/constants.hpp"

using constants::US_PER_S;

// Decodes whatever a transport could deliver and applies what decodes. The
// settings must stay within their bounds whatever arrives.
// libFuzzer calls the entry point by this name.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // A transport refuses anything longer before it reaches the codec.
    if (size > Payload::BUFFER_SIZE) {
        return -1;
    }
    Payload payload{};
    payload.length = static_cast<int>(size);
    std::copy_n(data, size, payload.data.begin());

    const std::optional<Command> command{json_codec::decode_command(payload)};
    if (!command) {
        return 0;
    }

    RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 5.0f,
        .sensor_calibration = {.adc_min = 1000, .adc_max = 3000},
        .pump_duration_us = 5 * US_PER_S,
        .pump_wait_time_us = 60 * US_PER_S,
        .pump_automatic = true,
    }};
    CommandHandler handler{runtime_settings};
    std::ignore = handler.apply(*command);

    const bool in_bounds{
        settings_rules::is_valid_sensor_band(
            runtime_settings.sensor_target(), runtime_settings.sensor_deadband()
        ) &&
        settings_rules::is_valid_sensor_calibration(runtime_settings.sensor_calibration()) &&
        settings_rules::is_valid_pump_duration(runtime_settings.pump_duration()) &&
        settings_rules::is_valid_pump_wait_time(runtime_settings.pump_wait_time())
    };
    if (!in_bounds) {
        std::abort();
    }
    return 0;
}
