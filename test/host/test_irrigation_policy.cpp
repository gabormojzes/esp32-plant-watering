#include <doctest.h>

#include "irrigation_policy.hpp"
#include "irrigation_state.hpp"
#include "runtime_settings.hpp"
#include "sensor_snapshot.hpp"

namespace {

constexpr float TARGET{50.0f};
constexpr float DEADBAND{5.0f};
constexpr std::int64_t DURATION_US{1000};
constexpr std::int64_t MAX_AGE_US{20000};
// Within MAX_AGE_US of zero, which leaves a reading sampled at zero fresh
constexpr std::int64_t NOW_US{1000};

// Readings either side of the target and clear of the deadband. Dry wants
// water, and wet does not.
constexpr SensorSnapshot DRY{.smoothed = 40.0f, .ready = true};
constexpr SensorSnapshot WET{.smoothed = 60.0f, .ready = true};
// The deadband edges and a reading between them. The low edge starts no run,
// and the high one ends one.
constexpr SensorSnapshot BAND_LOW{.smoothed = TARGET - DEADBAND, .ready = true};
constexpr SensorSnapshot IN_BAND{.smoothed = (TARGET + DEADBAND) - 1.0f, .ready = true};
constexpr SensorSnapshot BAND_HIGH{.smoothed = TARGET + DEADBAND, .ready = true};
// A dry reading the sensor has not produced a first sample for yet
constexpr SensorSnapshot NOT_READY{.smoothed = 40.0f, .ready = false};
// A dry reading from a sensor whose latest burst gave nothing usable
constexpr SensorSnapshot FAULTED{.smoothed = 40.0f, .ready = true, .faulted = true};

// A run halfway through its duration
constexpr IrrigationState MID_RUN{.current_run_time_us = DURATION_US / 2};

struct Fixture {
    RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = TARGET,
        .sensor_deadband = DEADBAND,
        .sensor_calibration = {.adc_min = 0, .adc_max = 4095},
        .pump_duration_us = DURATION_US,
        .pump_wait_time_us = 0,
        .pump_automatic = true,
    }};
    IrrigationPolicy policy{
        runtime_settings, IrrigationPolicy::Config{.max_reading_age_us = MAX_AGE_US}
    };
};

}  // namespace

TEST_CASE("should_start starts on a reading below the band") {
    const Fixture fixture{};
    CHECK(fixture.policy.should_start(DRY, NOW_US));
}

TEST_CASE("should_start holds off on a reading at the low band edge") {
    const Fixture fixture{};
    CHECK_FALSE(fixture.policy.should_start(BAND_LOW, NOW_US));
}

TEST_CASE("should_start holds off on a wet reading") {
    const Fixture fixture{};
    CHECK_FALSE(fixture.policy.should_start(WET, NOW_US));
}

TEST_CASE("should_start holds off on a reading that is not ready") {
    const Fixture fixture{};
    CHECK_FALSE(fixture.policy.should_start(NOT_READY, NOW_US));
}

TEST_CASE("should_start holds off on a reading from a faulted sensor") {
    const Fixture fixture{};
    CHECK_FALSE(fixture.policy.should_start(FAULTED, NOW_US));
}

TEST_CASE("should_start holds off on a reading that has aged out") {
    const Fixture fixture{};
    const SensorSnapshot aged{.smoothed = 40.0f, .sampled_at_us = 0, .ready = true};
    CHECK_FALSE(fixture.policy.should_start(aged, MAX_AGE_US + 1));
}

TEST_CASE("should_start starts on a reading exactly at the age limit") {
    const Fixture fixture{};
    // A microsecond younger than the case above
    const SensorSnapshot at_limit{.smoothed = 40.0f, .sampled_at_us = 1, .ready = true};
    CHECK(fixture.policy.should_start(at_limit, MAX_AGE_US + 1));
}

TEST_CASE("should_start follows a band change") {
    Fixture fixture{};
    REQUIRE(
        fixture.runtime_settings.set_sensor_band(30.0f, DEADBAND)
    );  // Below the dry reading -> no demand anymore
    CHECK_FALSE(fixture.policy.should_start(DRY, NOW_US));
}

TEST_CASE("should_stop keeps a run going past the target until the band is cleared") {
    const Fixture fixture{};
    CHECK_FALSE(fixture.policy.should_stop(IN_BAND, MID_RUN, NOW_US));
    CHECK(fixture.policy.should_stop(BAND_HIGH, MID_RUN, NOW_US));
}

TEST_CASE("should_stop keeps a dry run going until its duration is up") {
    const Fixture fixture{};
    CHECK_FALSE(fixture.policy.should_stop(DRY, {.current_run_time_us = DURATION_US - 1}, NOW_US));
    CHECK(fixture.policy.should_stop(DRY, {.current_run_time_us = DURATION_US}, NOW_US));
}

TEST_CASE("should_stop ends a run on a reading that is not ready") {
    const Fixture fixture{};
    CHECK(fixture.policy.should_stop(NOT_READY, MID_RUN, NOW_US));
}

TEST_CASE("should_stop ends a run on a reading from a faulted sensor") {
    const Fixture fixture{};
    CHECK(fixture.policy.should_stop(FAULTED, MID_RUN, NOW_US));
}

TEST_CASE("should_stop ends a run on a reading that has aged out") {
    const Fixture fixture{};
    const SensorSnapshot aged{.smoothed = 40.0f, .sampled_at_us = 0, .ready = true};
    CHECK(fixture.policy.should_stop(aged, MID_RUN, MAX_AGE_US + 1));
}

TEST_CASE("should_start holds off while automatic watering is off") {
    Fixture fixture{};
    fixture.runtime_settings.set_pump_automatic(false);
    CHECK_FALSE(fixture.policy.should_start(DRY, NOW_US));
}

TEST_CASE("should_stop ends a run once automatic watering is turned off") {
    Fixture fixture{};
    fixture.runtime_settings.set_pump_automatic(false);
    CHECK(fixture.policy.should_stop(DRY, MID_RUN, NOW_US));
}

TEST_CASE("should_stop_manual_run keeps a manual run going until its run length is up") {
    const Fixture fixture{};
    CHECK_FALSE(fixture.policy.should_stop_manual_run(MID_RUN));
    CHECK(
        fixture.policy.should_stop_manual_run(IrrigationState{.current_run_time_us = DURATION_US})
    );
}

TEST_CASE("should_stop_manual_run keeps a manual run going while automatic watering is off") {
    Fixture fixture{};
    fixture.runtime_settings.set_pump_automatic(false);
    CHECK_FALSE(fixture.policy.should_stop_manual_run(MID_RUN));
}
