#include "messaging/command_handler.hpp"

#include <esp_log.h>

#include <cstdint>
#include <optional>

#include "hardware/adc_range.hpp"
#include "settings_rules.hpp"
#include "shared/math_utils.hpp"
#include "shared/time_utils.hpp"

namespace {

constexpr char TAG[]{"COMMAND_HANDLER"};

// Returns the percent a received number sets, or nullopt outside 0..100.
// Rounded to a tenth first, which leaves the rule to judge the value that would
// run. The rule also refuses NaN and infinity.
std::optional<float> settable_percent(double value) {
    const double tenth{math_utils::nearest_tenth(value)};
    if (!settings_rules::is_valid_percent(tenth)) {
        ESP_LOGW(TAG, "%g is not a percent within 0..100", value);
        return std::nullopt;
    }
    return static_cast<float>(tenth);
}

// Returns the raw count a received number sets, or nullopt unless it is a whole
// number a count can hold. A fraction is refused rather than rounded into a count
// nobody read.
std::optional<std::uint16_t> settable_count(double value) {
    const std::optional<std::uint16_t> count{math_utils::whole_count<std::uint16_t>(value)};
    if (!count) {
        ESP_LOGW(TAG, "%g is not a whole raw count", value);
    }
    return count;
}

}  // namespace

CommandHandler::CommandHandler(RuntimeSettings& runtime_settings)
    : runtime_settings_{runtime_settings} {}

bool CommandHandler::apply(const Command& command) {
    const RuntimeSettings::Config before{runtime_settings_.config()};
    apply_sensor_band(command.sensor);
    apply_sensor_calibration(command.sensor.calibration);
    apply_pump_duration(command.pump.duration_s);
    apply_pump_wait_time(command.pump.wait_time_s);
    apply_pump_automatic(command.pump.automatic);
    return runtime_settings_.config() != before;
}

std::optional<PumpRequest> CommandHandler::pump_request(const Command& command) {
    if (command.pump.stop.value_or(false)) {
        ESP_LOGI(TAG, "pump.stop requested");
        return PumpRequest::Stop;
    }
    if (command.pump.run.value_or(false)) {
        ESP_LOGI(TAG, "pump.run requested");
        return PumpRequest::Run;
    }
    return std::nullopt;
}

// Target and deadband are judged as one band, and an invalid value drops both. A
// command carrying only one fills the other from the setting in use.
void CommandHandler::apply_sensor_band(const Command::Sensor& sensor) {
    if (!sensor.target && !sensor.deadband) {
        return;
    }

    std::optional<float> target{};
    if (sensor.target) {
        target = settable_percent(*sensor.target);
        if (!target) {
            return;
        }
    }
    std::optional<float> deadband{};
    if (sensor.deadband) {
        deadband = settable_percent(*sensor.deadband);
        if (!deadband) {
            return;
        }
    }

    const float sensor_target{target.value_or(runtime_settings_.sensor_target())};
    const float sensor_deadband{deadband.value_or(runtime_settings_.sensor_deadband())};
    if (!runtime_settings_.set_sensor_band(sensor_target, sensor_deadband)) {
        ESP_LOGW(
            TAG,
            "sensor.deadband must be at least %g, and sensor.target +/- "
            "deadband within 0..100",
            settings_rules::MIN_SENSOR_DEADBAND
        );
        return;
    }
    ESP_LOGI(TAG, "sensor.target -> %.2f, sensor.deadband -> %.2f", sensor_target, sensor_deadband);
}

// The two ends are judged as one pair, and an invalid value drops both. A command
// carrying only one fills the other from the setting in use.
void CommandHandler::apply_sensor_calibration(const Command::Sensor::Calibration& calibration) {
    if (!calibration.adc_min && !calibration.adc_max) {
        return;
    }

    std::optional<std::uint16_t> adc_min{};
    if (calibration.adc_min) {
        adc_min = settable_count(*calibration.adc_min);
        if (!adc_min) {
            return;
        }
    }
    std::optional<std::uint16_t> adc_max{};
    if (calibration.adc_max) {
        adc_max = settable_count(*calibration.adc_max);
        if (!adc_max) {
            return;
        }
    }

    const SensorCalibration in_use{runtime_settings_.sensor_calibration()};
    const SensorCalibration sensor_calibration{
        .adc_min = adc_min.value_or(in_use.adc_min),
        .adc_max = adc_max.value_or(in_use.adc_max),
    };
    if (!runtime_settings_.set_sensor_calibration(sensor_calibration)) {
        ESP_LOGW(
            TAG,
            "sensor.calibration.adcMin must be below adcMax, and adcMax at most %u",
            static_cast<unsigned>(adc_range::MAX_RAW)
        );
        return;
    }
    ESP_LOGI(
        TAG,
        "sensor.calibration.adcMin -> %u, sensor.calibration.adcMax -> %u",
        static_cast<unsigned>(sensor_calibration.adc_min),
        static_cast<unsigned>(sensor_calibration.adc_max)
    );
}

// Whole seconds only. A fractional value is ignored rather than truncated into a
// setting nobody sent.
void CommandHandler::apply_pump_duration(std::optional<double> pump_duration_s) {
    if (!pump_duration_s) {
        return;
    }
    const std::optional<std::int64_t> pump_duration_us{
        time_utils::whole_seconds_to_us(*pump_duration_s)
    };
    if (!pump_duration_us || !runtime_settings_.set_pump_duration(*pump_duration_us)) {
        // Quoted in whole seconds. Rounding the lower bound up and the upper down
        // gives exactly the whole-second values the setter accepts.
        ESP_LOGW(
            TAG,
            "pump.duration must be whole seconds within %lld..%lld",
            static_cast<long long>(
                time_utils::to_seconds_ceil(settings_rules::MIN_PUMP_DURATION_US)
            ),
            static_cast<long long>(
                time_utils::to_seconds_floor(settings_rules::MAX_PUMP_DURATION_US)
            )
        );
        return;
    }
    ESP_LOGI(
        TAG,
        "pump.duration -> %lld s",
        static_cast<long long>(time_utils::to_seconds_floor(*pump_duration_us))
    );
}

void CommandHandler::apply_pump_wait_time(std::optional<double> pump_wait_time_s) {
    if (!pump_wait_time_s) {
        return;
    }
    const std::optional<std::int64_t> pump_wait_time_us{
        time_utils::whole_seconds_to_us(*pump_wait_time_s)
    };
    if (!pump_wait_time_us || !runtime_settings_.set_pump_wait_time(*pump_wait_time_us)) {
        // Quoted in whole seconds. Rounding the lower bound up and the upper down
        // gives exactly the whole-second values the setter accepts.
        ESP_LOGW(
            TAG,
            "pump.waitTime must be whole seconds within %lld..%lld",
            static_cast<long long>(
                time_utils::to_seconds_ceil(settings_rules::MIN_PUMP_WAIT_TIME_US)
            ),
            static_cast<long long>(
                time_utils::to_seconds_floor(settings_rules::MAX_PUMP_WAIT_TIME_US)
            )
        );
        return;
    }
    ESP_LOGI(
        TAG,
        "pump.waitTime -> %lld s",
        static_cast<long long>(time_utils::to_seconds_floor(*pump_wait_time_us))
    );
}

void CommandHandler::apply_pump_automatic(std::optional<bool> pump_automatic) {
    if (!pump_automatic) {
        return;
    }
    runtime_settings_.set_pump_automatic(*pump_automatic);
    ESP_LOGI(TAG, "pump.automatic -> %s", *pump_automatic ? "true" : "false");
}
