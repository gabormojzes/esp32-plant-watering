#pragma once

#include <cstdint>

#include "sensor_calibration.hpp"
#include "settings_rules.hpp"

// The values a running device can change, seeded from boot defaults.
// Setters refuse an invalid value and return false, keeping every stored value
// valid. Not thread-safe; every value belongs to the single task that sets and
// reads it.
class RuntimeSettings {
public:
    struct Config {
        float sensor_target;
        float sensor_deadband;
        SensorCalibration sensor_calibration;
        std::int64_t pump_duration_us;
        std::int64_t pump_wait_time_us;
        bool pump_automatic;

        bool operator==(const Config&) const = default;
    };

    explicit RuntimeSettings(const Config& config)
        : sensor_target_{config.sensor_target},
          sensor_deadband_{config.sensor_deadband},
          sensor_calibration_{config.sensor_calibration},
          pump_duration_us_{config.pump_duration_us},
          pump_wait_time_us_{config.pump_wait_time_us},
          pump_automatic_{config.pump_automatic} {}

    // Target and deadband bound each other and are set as one, because a pair
    // that is valid together can be refused when its halves land one at a time.
    [[nodiscard]] bool set_sensor_band(float sensor_target, float sensor_deadband) {
        if (!settings_rules::is_valid_sensor_band(sensor_target, sensor_deadband)) {
            return false;
        }
        sensor_target_ = sensor_target;
        sensor_deadband_ = sensor_deadband;
        return true;
    }

    // The two ends bound each other and are set as one, for the same reason as
    // the band.
    [[nodiscard]] bool set_sensor_calibration(const SensorCalibration& sensor_calibration) {
        if (!settings_rules::is_valid_sensor_calibration(sensor_calibration)) {
            return false;
        }
        sensor_calibration_ = sensor_calibration;
        return true;
    }

    [[nodiscard]] bool set_pump_duration(std::int64_t pump_duration_us) {
        if (!settings_rules::is_valid_pump_duration(pump_duration_us)) {
            return false;
        }
        pump_duration_us_ = pump_duration_us;
        return true;
    }

    [[nodiscard]] bool set_pump_wait_time(std::int64_t pump_wait_time_us) {
        if (!settings_rules::is_valid_pump_wait_time(pump_wait_time_us)) {
            return false;
        }
        pump_wait_time_us_ = pump_wait_time_us;
        return true;
    }

    // Either value is valid, which leaves nothing to refuse.
    void set_pump_automatic(bool pump_automatic) {
        pump_automatic_ = pump_automatic;
    }

    float sensor_target() const {
        return sensor_target_;
    }

    float sensor_deadband() const {
        return sensor_deadband_;
    }

    SensorCalibration sensor_calibration() const {
        return sensor_calibration_;
    }

    std::int64_t pump_duration() const {
        return pump_duration_us_;
    }

    std::int64_t pump_wait_time() const {
        return pump_wait_time_us_;
    }

    bool pump_automatic() const {
        return pump_automatic_;
    }

    // Every value in use, in the shape the class is seeded from
    Config config() const {
        return {
            .sensor_target = sensor_target_,
            .sensor_deadband = sensor_deadband_,
            .sensor_calibration = sensor_calibration_,
            .pump_duration_us = pump_duration_us_,
            .pump_wait_time_us = pump_wait_time_us_,
            .pump_automatic = pump_automatic_,
        };
    }

private:
    float sensor_target_{0.0f};
    float sensor_deadband_{0.0f};
    SensorCalibration sensor_calibration_{};
    std::int64_t pump_duration_us_{0};
    std::int64_t pump_wait_time_us_{0};
    bool pump_automatic_{false};
};
