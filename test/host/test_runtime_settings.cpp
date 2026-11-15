#include <doctest.h>

#include <cstdint>
#include <limits>

#include "hardware/adc_range.hpp"
#include "runtime_settings.hpp"
#include "settings_rules.hpp"

namespace {

constexpr float TARGET{50.0f};
constexpr float DEADBAND{5.0f};
constexpr std::uint16_t ADC_MIN{1000};
constexpr std::uint16_t ADC_MAX{3000};
constexpr std::int64_t DURATION_US{1000};
constexpr std::int64_t WAIT_TIME_US{5000};

constexpr float NOT_A_NUMBER{std::numeric_limits<float>::quiet_NaN()};
constexpr float INFINITY_F{std::numeric_limits<float>::infinity()};

struct Fixture : RuntimeSettings {
    Fixture()
        : RuntimeSettings{Config{
              .sensor_target = TARGET,
              .sensor_deadband = DEADBAND,
              .sensor_calibration = {.adc_min = ADC_MIN, .adc_max = ADC_MAX},
              .pump_duration_us = DURATION_US,
              .pump_wait_time_us = WAIT_TIME_US,
              .pump_automatic = true
          }} {}
};

}  // namespace

TEST_CASE("the constructor takes the configured values as-is") {
    const Fixture fixture{};
    CHECK(fixture.sensor_target() == TARGET);
    CHECK(fixture.sensor_deadband() == DEADBAND);
    CHECK(
        fixture.sensor_calibration() == SensorCalibration{.adc_min = ADC_MIN, .adc_max = ADC_MAX}
    );
    CHECK(fixture.pump_duration() == DURATION_US);
    CHECK(fixture.pump_wait_time() == WAIT_TIME_US);
}

TEST_CASE("set_sensor_band accepts a band filling the percent range") {
    Fixture fixture{};
    CHECK(fixture.set_sensor_band(50.0f, 50.0f));  // 0..100, the widest there is
    CHECK(fixture.sensor_target() == 50.0f);
    CHECK(fixture.sensor_deadband() == 50.0f);
    CHECK(fixture.set_sensor_band(0.0f, 0.0f));  // No band at all, at the bottom end
    CHECK(fixture.sensor_target() == 0.0f);
    CHECK(fixture.sensor_deadband() == 0.0f);
}

TEST_CASE("set_sensor_band rejects a band reaching past either end") {
    Fixture fixture{};
    CHECK_FALSE(fixture.set_sensor_band(10.0f, 25.0f));  // Start threshold below 0
    CHECK_FALSE(fixture.set_sensor_band(90.0f, 25.0f));  // Stop threshold past 100
    CHECK(fixture.sensor_target() == TARGET);
    CHECK(fixture.sensor_deadband() == DEADBAND);
}

TEST_CASE("set_sensor_band rejects a negative deadband") {
    Fixture fixture{};
    CHECK_FALSE(fixture.set_sensor_band(TARGET, -1.0f));  // Would start above it stops
    CHECK(fixture.sensor_deadband() == DEADBAND);
}

TEST_CASE("set_sensor_band rejects non-finite values") {
    Fixture fixture{};
    CHECK_FALSE(fixture.set_sensor_band(NOT_A_NUMBER, DEADBAND));
    CHECK_FALSE(fixture.set_sensor_band(TARGET, INFINITY_F));
    CHECK(fixture.sensor_target() == TARGET);
    CHECK(fixture.sensor_deadband() == DEADBAND);
}

TEST_CASE("set_sensor_band lands a pair the halves could not step to in order") {
    Fixture fixture{};
    REQUIRE(fixture.set_sensor_band(90.0f, 5.0f));
    // Widening against the target still up there overruns the top.
    CHECK_FALSE(fixture.set_sensor_band(90.0f, 40.0f));
    CHECK(fixture.set_sensor_band(50.0f, 40.0f));
    CHECK(fixture.sensor_target() == 50.0f);
    CHECK(fixture.sensor_deadband() == 40.0f);
}

TEST_CASE("set_sensor_calibration accepts the widest pair the ADC can read") {
    Fixture fixture{};
    CHECK(fixture.set_sensor_calibration({.adc_min = 0, .adc_max = adc_range::MAX_RAW}));
    CHECK(
        fixture.sensor_calibration() ==
        SensorCalibration{.adc_min = 0, .adc_max = adc_range::MAX_RAW}
    );
    CHECK(fixture.set_sensor_calibration({.adc_min = 1, .adc_max = 2}));  // The narrowest
    CHECK(fixture.sensor_calibration() == SensorCalibration{.adc_min = 1, .adc_max = 2});
}

TEST_CASE("set_sensor_calibration rejects endpoints that meet or cross") {
    Fixture fixture{};
    CHECK_FALSE(fixture.set_sensor_calibration({.adc_min = 2000, .adc_max = 2000}));
    CHECK_FALSE(fixture.set_sensor_calibration({.adc_min = ADC_MAX, .adc_max = ADC_MIN}));
    CHECK(
        fixture.sensor_calibration() == SensorCalibration{.adc_min = ADC_MIN, .adc_max = ADC_MAX}
    );
}

TEST_CASE("set_sensor_calibration rejects an adc_max past what the ADC returns") {
    Fixture fixture{};
    CHECK_FALSE(
        fixture.set_sensor_calibration({.adc_min = ADC_MIN, .adc_max = adc_range::MAX_RAW + 1})
    );
    CHECK(
        fixture.sensor_calibration() == SensorCalibration{.adc_min = ADC_MIN, .adc_max = ADC_MAX}
    );
}

TEST_CASE("set_sensor_calibration lands a pair the halves could not step to in order") {
    Fixture fixture{};
    // Both ends move above the old adc_max. adc_min alone first would cross it.
    CHECK_FALSE(fixture.set_sensor_calibration({.adc_min = 3500, .adc_max = ADC_MAX}));
    CHECK(fixture.set_sensor_calibration({.adc_min = 3500, .adc_max = 4000}));
    CHECK(fixture.sensor_calibration() == SensorCalibration{.adc_min = 3500, .adc_max = 4000});
}

TEST_CASE("set_pump_duration rejects non-positive values") {
    Fixture fixture{};
    CHECK_FALSE(fixture.set_pump_duration(0));
    CHECK_FALSE(fixture.set_pump_duration(-1));
    CHECK(fixture.pump_duration() == DURATION_US);
    CHECK(fixture.set_pump_duration(2000));
    CHECK(fixture.pump_duration() == 2000);
}

TEST_CASE("set_pump_duration accepts the maximum inclusive and rejects above it") {
    Fixture fixture{};
    CHECK(fixture.set_pump_duration(settings_rules::MAX_PUMP_DURATION_US));
    CHECK(fixture.pump_duration() == settings_rules::MAX_PUMP_DURATION_US);
    CHECK_FALSE(fixture.set_pump_duration(settings_rules::MAX_PUMP_DURATION_US + 1));
    CHECK(fixture.pump_duration() == settings_rules::MAX_PUMP_DURATION_US);
}

TEST_CASE("set_pump_wait_time rejects negatives but allows zero") {
    Fixture fixture{};
    CHECK_FALSE(fixture.set_pump_wait_time(-1));
    CHECK(fixture.pump_wait_time() == WAIT_TIME_US);
    CHECK(fixture.set_pump_wait_time(0));
    CHECK(fixture.pump_wait_time() == 0);
}

TEST_CASE("set_pump_wait_time accepts the maximum inclusive and rejects above it") {
    Fixture fixture{};
    CHECK(fixture.set_pump_wait_time(settings_rules::MAX_PUMP_WAIT_TIME_US));
    CHECK(fixture.pump_wait_time() == settings_rules::MAX_PUMP_WAIT_TIME_US);
    CHECK_FALSE(fixture.set_pump_wait_time(settings_rules::MAX_PUMP_WAIT_TIME_US + 1));
    CHECK(fixture.pump_wait_time() == settings_rules::MAX_PUMP_WAIT_TIME_US);
}
