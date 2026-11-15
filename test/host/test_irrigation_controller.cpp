#include <doctest.h>
#include <driver/gpio.h>

#include "hardware/gpio_output.hpp"
#include "hardware/pump.hpp"
#include "irrigation_controller.hpp"
#include "irrigation_policy.hpp"
#include "runtime_settings.hpp"

namespace {

constexpr gpio_num_t PIN{GPIO_NUM_22};

constexpr std::int64_t DURATION_US{1000};   // Pump runs this long once started
constexpr std::int64_t WAIT_TIME_US{5000};  // Wait before it may run again
constexpr std::int64_t LOCKOUT_US{500};     // No run may start this soon after boot
// Longer than any test clock below, which keeps every reading fresh
constexpr std::int64_t MAX_AGE_US{20000};

// Readings either side of the target and clear of the deadband. Dry wants
// water, and wet does not.
constexpr float TARGET{50.0f};
constexpr float DEADBAND{5.0f};
constexpr SensorSnapshot DRY{.smoothed = 40.0f, .ready = true};
constexpr SensorSnapshot WET{.smoothed = 60.0f, .ready = true};
// A dry reading from a sensor whose latest burst gave nothing usable
constexpr SensorSnapshot FAULTED{.smoothed = 40.0f, .ready = true, .faulted = true};

// The first tick, past LOCKOUT_US and shorter than WAIT_TIME_US to show that no
// wait is owed before the first run
constexpr std::int64_t START_US{1000};

// The controller under test, driving a real pump on a stubbed pin
struct Fixture {
    explicit Fixture(std::int64_t boot_lockout_us = LOCKOUT_US)
        : controller{
              pump,
              irrigation_policy,
              runtime_settings,
              IrrigationController::Config{.boot_lockout_us = boot_lockout_us}
          } {
        gpio_stub_level = 0;
        gpio_stub_set_level_calls = 0;
        pump.init();
        // The claim drives the pin low. Only the writes after it are the controller's.
        gpio_stub_set_level_calls = 0;
    }

    void start_first_run() {
        controller.update(DRY, START_US);
        REQUIRE(pump.is_on());
    }

    GpioOutput gpio_output{GpioOutput::Config{.pin = PIN}};
    Pump pump{gpio_output};
    RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = TARGET,
        .sensor_deadband = DEADBAND,
        .sensor_calibration = {.adc_min = 0, .adc_max = 4095},
        .pump_duration_us = DURATION_US,
        .pump_wait_time_us = WAIT_TIME_US,
        .pump_automatic = true
    }};
    IrrigationPolicy irrigation_policy{
        runtime_settings, IrrigationPolicy::Config{.max_reading_age_us = MAX_AGE_US}
    };
    IrrigationController controller;
};

}  // namespace

TEST_CASE("update starts the pump on the first tick with demand") {
    Fixture fixture{};
    fixture.controller.update(DRY, START_US);  // No stop recorded yet -> no wait owed
    CHECK(fixture.pump.is_on());
    CHECK(fixture.controller.state(START_US).run_count == 1);
}

TEST_CASE("update holds the first run back until the boot lockout elapses") {
    Fixture fixture{};
    fixture.controller.update(DRY, LOCKOUT_US - 1);  // Demand, but the device just booted
    CHECK_FALSE(fixture.pump.is_on());
    fixture.controller.update(DRY, LOCKOUT_US);
    CHECK(fixture.pump.is_on());
}

TEST_CASE("update gates on the lockout even when it outlasts the wait") {
    Fixture fixture{START_US + 1};  // Lockout still owed at the first tick
    fixture.controller.update(DRY, START_US);
    CHECK_FALSE(fixture.pump.is_on());
    fixture.controller.update(DRY, START_US + 1);
    CHECK(fixture.pump.is_on());
}

TEST_CASE("update keeps an idle pump off without demand") {
    Fixture fixture{};
    fixture.controller.update(WET, START_US);
    CHECK_FALSE(fixture.pump.is_on());
}

TEST_CASE("update stops a run when the sensor faults mid-run") {
    Fixture fixture{};
    fixture.start_first_run();
    fixture.controller.update(
        FAULTED, START_US + (DURATION_US / 2)
    );  // Faults before the duration is up
    CHECK_FALSE(fixture.pump.is_on());
}

TEST_CASE("update keeps the pump running until its duration elapses") {
    Fixture fixture{};
    fixture.start_first_run();
    fixture.controller.update(DRY, START_US + DURATION_US - 1);  // Still within the run
    CHECK(fixture.pump.is_on());
    CHECK(fixture.controller.state(START_US + DURATION_US - 1).run_count == 1);  // No re-trigger
}

TEST_CASE("update stops the pump when its run duration elapses") {
    Fixture fixture{};
    fixture.start_first_run();
    fixture.controller.update(DRY, START_US + DURATION_US);
    CHECK_FALSE(fixture.pump.is_on());
}

TEST_CASE("update never runs a faulted pump and counts nothing") {
    // A pump whose pin claim failed refuses requests and stays off.
    gpio_stub_config_result = ESP_FAIL;
    Fixture fixture{};
    gpio_stub_config_result = ESP_OK;
    fixture.controller.update(DRY, START_US);
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(fixture.controller.state(START_US).run_count == 0);
}

TEST_CASE("update drives the pin on transitions only") {
    Fixture fixture{};
    fixture.start_first_run();
    fixture.controller.update(DRY, START_US + 1);  // Demand holds, the run is already on
    fixture.controller.update(DRY, START_US + DURATION_US);
    fixture.controller.update(WET, START_US + DURATION_US + 1);  // Idle, nothing left to stop
    // One write to start and one to stop, since repeats never reach the pin.
    CHECK(gpio_stub_set_level_calls == 2);
}

TEST_CASE("update enforces the wait time between runs and counts the second") {
    Fixture fixture{};
    fixture.start_first_run();
    const std::int64_t first_stop_us{START_US + DURATION_US};
    fixture.controller.update(DRY, first_stop_us);  // Duration elapsed -> stops
    fixture.controller.update(
        DRY, first_stop_us + WAIT_TIME_US - 1
    );  // Wait time pending -> stays off
    CHECK_FALSE(fixture.pump.is_on());
    fixture.controller.update(DRY, first_stop_us + WAIT_TIME_US);  // Wait time over -> runs again
    CHECK(fixture.pump.is_on());
    CHECK(fixture.controller.state(first_stop_us + WAIT_TIME_US).run_count == 2);
}

TEST_CASE("update reopens an ended wait when the wait time is raised") {
    Fixture fixture{};
    fixture.start_first_run();
    const std::int64_t stop_us{START_US + DURATION_US};
    fixture.controller.update(WET, stop_us);                 // Duration elapsed -> stops
    fixture.controller.update(WET, stop_us + WAIT_TIME_US);  // Wait over, no demand -> stays off
    REQUIRE(fixture.runtime_settings.set_pump_wait_time(2 * WAIT_TIME_US));
    fixture.controller.update(
        DRY, stop_us + WAIT_TIME_US + 1
    );  // Demand, but the raised wait holds it
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(
        fixture.controller.state(stop_us + WAIT_TIME_US + 1).remaining_wait_time_us ==
        WAIT_TIME_US - 1
    );
    fixture.controller.update(DRY, stop_us + (2 * WAIT_TIME_US));
    CHECK(fixture.pump.is_on());
}

TEST_CASE("update ends a wait early when the wait time is lowered") {
    Fixture fixture{};
    fixture.start_first_run();
    const std::int64_t stop_us{START_US + DURATION_US};
    fixture.controller.update(DRY, stop_us);  // Duration elapsed -> stops
    REQUIRE(fixture.runtime_settings.set_pump_wait_time(WAIT_TIME_US / 2));
    fixture.controller.update(DRY, stop_us + (WAIT_TIME_US / 2));
    CHECK(fixture.pump.is_on());
}

TEST_CASE("state counts the boot lockout down before the first run") {
    const Fixture fixture{};
    CHECK(fixture.controller.state(0).remaining_wait_time_us == LOCKOUT_US);
    CHECK(fixture.controller.state(LOCKOUT_US / 2).remaining_wait_time_us == LOCKOUT_US / 2);
    CHECK(fixture.controller.state(LOCKOUT_US).remaining_wait_time_us == 0);
}

TEST_CASE("state reports the wait after a stop, the lockout long elapsed") {
    Fixture fixture{};
    fixture.start_first_run();
    const std::int64_t stop_us{START_US + DURATION_US};
    fixture.controller.update(DRY, stop_us);  // Duration elapsed -> stops
    // The lockout ran out before the first run, which leaves the longer wait
    // since the stop.
    CHECK(fixture.controller.state(stop_us).remaining_wait_time_us == WAIT_TIME_US);
}

TEST_CASE("state reports no wait pending before the first run") {
    const Fixture fixture{};
    const IrrigationState s{fixture.controller.state(START_US)};
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(s.run_count == 0);
    CHECK(s.idle_time_us == START_US);  // Idle since boot
    CHECK(s.remaining_wait_time_us == 0);
}

TEST_CASE("state reports a running pump with the full wait ahead") {
    Fixture fixture{};
    fixture.start_first_run();
    const IrrigationState s{fixture.controller.state(START_US + 100)};
    CHECK(fixture.pump.is_on());
    CHECK(s.run_count == 1);
    CHECK(s.idle_time_us == 0);                       // Running, not idle
    CHECK(s.remaining_wait_time_us == WAIT_TIME_US);  // The wait starts at stop, all of it ahead
}

TEST_CASE("state reports the time into a run, zero between runs") {
    Fixture fixture{};
    CHECK(fixture.controller.state(START_US).current_run_time_us == 0);  // Before the first run
    fixture.start_first_run();
    CHECK(fixture.controller.state(START_US + 400).current_run_time_us == 400);
    const std::int64_t stop_us{START_US + DURATION_US};
    fixture.controller.update(DRY, stop_us);  // Duration elapsed -> stops
    CHECK(fixture.controller.state(stop_us + 1000).current_run_time_us == 0);
}

TEST_CASE("state counts the run in progress into the total, then holds it") {
    Fixture fixture{};
    fixture.start_first_run();
    CHECK(fixture.controller.state(START_US).total_run_time_us == 0);
    CHECK(
        fixture.controller.state(START_US + 400).total_run_time_us == 400
    );  // Mid-run, not frozen
    const std::int64_t stop_us{START_US + DURATION_US};
    fixture.controller.update(DRY, stop_us);  // Duration elapsed -> stops
    CHECK(fixture.controller.state(stop_us).total_run_time_us == DURATION_US);
    // Idling adds nothing to the total.
    CHECK(fixture.controller.state(stop_us + WAIT_TIME_US).total_run_time_us == DURATION_US);
}

TEST_CASE("state totals runs of differing duration") {
    Fixture fixture{};
    fixture.start_first_run();
    const std::int64_t first_stop_us{START_US + DURATION_US};
    fixture.controller.update(DRY, first_stop_us);  // Duration elapsed -> stops
    // A longer duration set between the runs, which stops the count from implying
    // the total
    REQUIRE(fixture.runtime_settings.set_pump_duration(3 * DURATION_US));
    const std::int64_t second_start_us{first_stop_us + WAIT_TIME_US};
    fixture.controller.update(DRY, second_start_us);
    CHECK(fixture.pump.is_on());
    const std::int64_t second_stop_us{second_start_us + (3 * DURATION_US)};
    fixture.controller.update(DRY, second_stop_us);  // The new duration elapsed -> stops
    const IrrigationState s{fixture.controller.state(second_stop_us)};
    CHECK(s.run_count == 2);
    CHECK(s.total_run_time_us == 4 * DURATION_US);
}

TEST_CASE("state totals a run cut short by the target, not its full duration") {
    Fixture fixture{};
    fixture.start_first_run();
    const std::int64_t stop_us{START_US + (DURATION_US / 2)};
    fixture.controller.update(WET, stop_us);  // Demand gone before the duration is up
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(fixture.controller.state(stop_us).total_run_time_us == DURATION_US / 2);
}

TEST_CASE("state counts idle up and the wait down from a stop, clamped at zero") {
    Fixture fixture{};
    fixture.start_first_run();
    const std::int64_t stop_us{START_US + DURATION_US};
    fixture.controller.update(DRY, stop_us);  // Duration elapsed -> stops, wait starts here
    const IrrigationState s{fixture.controller.state(stop_us + 1000)};
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(s.idle_time_us == 1000);
    CHECK(s.remaining_wait_time_us == WAIT_TIME_US - 1000);
    CHECK(fixture.controller.state(stop_us + WAIT_TIME_US + 1).remaining_wait_time_us == 0);
}

TEST_CASE("update stops a run when automatic watering is turned off, and the wait follows") {
    Fixture fixture{};
    fixture.start_first_run();
    const std::int64_t stop_us{START_US + (DURATION_US / 2)};
    fixture.runtime_settings.set_pump_automatic(false);
    fixture.controller.update(DRY, stop_us);  // Demand, and the run has time left
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(fixture.controller.state(stop_us).total_run_time_us == DURATION_US / 2);
    CHECK(fixture.controller.state(stop_us).remaining_wait_time_us == WAIT_TIME_US);
}

TEST_CASE("update starts no run while automatic watering is off, and runs once it is back on") {
    Fixture fixture{};
    fixture.runtime_settings.set_pump_automatic(false);
    fixture.controller.update(DRY, START_US);
    CHECK_FALSE(fixture.pump.is_on());
    fixture.runtime_settings.set_pump_automatic(true);
    fixture.controller.update(DRY, START_US + 1);
    CHECK(fixture.pump.is_on());
}

TEST_CASE("request starts a manual run during the boot lockout") {
    Fixture fixture{};
    fixture.controller.request(PumpRequest::Run, 0);  // The lockout has not even started counting
    CHECK(fixture.pump.is_on());
    CHECK(fixture.controller.state(0).run_count == 1);
}

TEST_CASE("request starts a manual run during the wait after a stop") {
    Fixture fixture{};
    fixture.start_first_run();
    const std::int64_t stop_us{START_US + DURATION_US};
    fixture.controller.update(DRY, stop_us);  // Duration elapsed -> stops, wait starts
    fixture.controller.request(PumpRequest::Run, stop_us + 1);
    CHECK(fixture.pump.is_on());
    CHECK(fixture.controller.state(stop_us + 1).run_count == 2);
}

TEST_CASE("request starts a manual run while automatic watering is off") {
    Fixture fixture{};
    fixture.runtime_settings.set_pump_automatic(false);
    fixture.controller.request(PumpRequest::Run, START_US);
    CHECK(fixture.pump.is_on());
}

TEST_CASE("update keeps a manual run going on a wet or faulted reading with automatic off") {
    Fixture fixture{};
    fixture.controller.request(PumpRequest::Run, START_US);
    fixture.runtime_settings.set_pump_automatic(false);
    fixture.controller.update(WET, START_US + 1);
    fixture.controller.update(FAULTED, START_US + DURATION_US - 1);
    CHECK(fixture.pump.is_on());
}

TEST_CASE("update ends a manual run at its run length, and the wait follows") {
    Fixture fixture{};
    fixture.controller.request(PumpRequest::Run, START_US);
    const std::int64_t stop_us{START_US + DURATION_US};
    fixture.controller.update(WET, stop_us);
    CHECK_FALSE(fixture.pump.is_on());
    const IrrigationState s{fixture.controller.state(stop_us)};
    CHECK(s.total_run_time_us == DURATION_US);
    CHECK(s.remaining_wait_time_us == WAIT_TIME_US);
}

TEST_CASE("request ignores a run while one is in progress") {
    Fixture fixture{};
    fixture.start_first_run();
    fixture.controller.request(PumpRequest::Run, START_US + (DURATION_US / 2));
    CHECK(fixture.controller.state(START_US + (DURATION_US / 2)).run_count == 1);
    // The run keeps its start, which ends it at the original time.
    fixture.controller.update(DRY, START_US + DURATION_US);
    CHECK_FALSE(fixture.pump.is_on());
}

TEST_CASE("request stops an automatic run as if it had finished") {
    Fixture fixture{};
    fixture.start_first_run();
    const std::int64_t stop_us{START_US + (DURATION_US / 2)};
    fixture.controller.request(PumpRequest::Stop, stop_us);
    CHECK_FALSE(fixture.pump.is_on());
    const IrrigationState s{fixture.controller.state(stop_us)};
    CHECK(s.total_run_time_us == DURATION_US / 2);
    CHECK(s.remaining_wait_time_us == WAIT_TIME_US);
    fixture.controller.update(DRY, stop_us + 1);  // Demand, but the wait holds it
    CHECK_FALSE(fixture.pump.is_on());
}

TEST_CASE("request stops a manual run") {
    Fixture fixture{};
    fixture.controller.request(PumpRequest::Run, START_US);
    fixture.controller.request(PumpRequest::Stop, START_US + 100);
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(fixture.controller.state(START_US + 100).total_run_time_us == 100);
}

TEST_CASE("update starts an automatic run right after a stop when no wait is set") {
    Fixture fixture{};
    REQUIRE(fixture.runtime_settings.set_pump_wait_time(0));
    fixture.start_first_run();
    fixture.controller.request(PumpRequest::Stop, START_US + 1);
    fixture.controller.update(DRY, START_US + 1);
    CHECK(fixture.pump.is_on());
    CHECK(fixture.controller.state(START_US + 1).run_count == 2);
}

TEST_CASE("request to stop leaves an idle pump alone") {
    Fixture fixture{};
    fixture.controller.request(PumpRequest::Stop, START_US);
    CHECK(gpio_stub_set_level_calls == 0);
    CHECK(fixture.controller.state(START_US).remaining_wait_time_us == 0);  // No stop recorded
}

TEST_CASE("request never runs a faulted pump and counts nothing") {
    gpio_stub_config_result = ESP_FAIL;
    Fixture fixture{};
    gpio_stub_config_result = ESP_OK;
    fixture.controller.request(PumpRequest::Run, START_US);
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(fixture.controller.state(START_US).run_count == 0);
}
