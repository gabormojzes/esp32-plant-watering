#pragma once

#include <concepts>
#include <cstdint>

#include "hardware/adc_range.hpp"
#include "sensor_calibration.hpp"
#include "shared/constants.hpp"
#include "shared/math_utils.hpp"

// What a runtime setting may hold. Each rule is written as what it admits, which
// leaves NaN out.
namespace settings_rules {

// No upper bound of its own. The widest deadband depends on the target, since the
// band must fit the percent range.
inline constexpr float MIN_SENSOR_DEADBAND{0.0f};
inline constexpr std::int64_t MIN_PUMP_DURATION_US{1};  // Any positive duration
inline constexpr std::int64_t MAX_PUMP_DURATION_US{300 * constants::US_PER_S};
inline constexpr std::int64_t MIN_PUMP_WAIT_TIME_US{0};  // Back-to-back runs allowed
inline constexpr std::int64_t MAX_PUMP_WAIT_TIME_US{86400 * constants::US_PER_S};

template <std::floating_point T>
constexpr bool is_valid_percent(T value) {
    return math_utils::is_in_percent_range(value) && math_utils::nearest_tenth(value) == value;
}

constexpr bool is_valid_sensor_band(float sensor_target, float sensor_deadband) {
    return sensor_deadband >= MIN_SENSOR_DEADBAND &&
           math_utils::is_in_percent_range(sensor_target - sensor_deadband) &&
           math_utils::is_in_percent_range(sensor_target + sensor_deadband);
}

constexpr bool is_valid_sensor_calibration(const SensorCalibration& sensor_calibration) {
    return sensor_calibration.adc_min < sensor_calibration.adc_max &&
           sensor_calibration.adc_max <= adc_range::MAX_RAW;
}

constexpr bool is_valid_pump_duration(std::int64_t pump_duration_us) {
    return pump_duration_us >= MIN_PUMP_DURATION_US && pump_duration_us <= MAX_PUMP_DURATION_US;
}

constexpr bool is_valid_pump_wait_time(std::int64_t pump_wait_time_us) {
    return pump_wait_time_us >= MIN_PUMP_WAIT_TIME_US && pump_wait_time_us <= MAX_PUMP_WAIT_TIME_US;
}

}  // namespace settings_rules
