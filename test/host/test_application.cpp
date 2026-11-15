#include <doctest.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <nvs.h>

#include <array>
#include <cstddef>
#include <optional>

#include "application.hpp"
#include "fake_web_host.hpp"
#include "hardware/adc_reader.hpp"
#include "hardware/gpio_output.hpp"
#include "hardware/sensor.hpp"
#include "irrigation_policy.hpp"
#include "network/null_network_provider.hpp"
#include "shared/constants.hpp"
#include "system_info.hpp"

using constants::US_PER_S;

// The device's own figures, which the host has none of.
std::optional<float> system_info::chip_temperature() {
    return std::nullopt;
}

HeapInfo system_info::heap() {
    return {};
}

void system_info::log_memory_stats() {}

namespace {

constexpr float TARGET{50.0f};
constexpr float DEADBAND{5.0f};
constexpr std::uint16_t ADC_MIN{1000};
constexpr std::uint16_t ADC_MAX{3000};
constexpr double DURATION_S{5.0};
constexpr std::int64_t DURATION_US{5 * US_PER_S};
constexpr std::int64_t WAIT_TIME_US{60 * US_PER_S};
// A duration a command sets, longer than the one in use
constexpr double NEW_DURATION_S{20.0};
constexpr std::int64_t NEW_DURATION_US{20 * US_PER_S};
constexpr float NEW_TARGET{40.0f};

// The first tick of every case
constexpr std::int64_t START_US{1 * US_PER_S};
// The most commands Application takes in one tick
constexpr std::size_t COMMANDS_PER_TICK{4};

struct FakeIndicator : Indicator {
    void init() override {}
    void update(std::int64_t /*now_us*/) override {}
};

// Hands over the commands a test queued, one per poll, in order.
class FakeMessaging : public Messaging {
public:
    void start() override {}

    [[nodiscard]] std::optional<Command> poll() override {
        if (next_ == count_) {
            return std::nullopt;
        }
        return commands_[next_++];
    }

    void send(const Telemetry& /*telemetry*/) override {}
    void describe(MessagingInfo& /*messaging_info*/) const override {}

    void queue(const Command& command) {
        REQUIRE(count_ < commands_.size());
        commands_[count_++] = command;
    }

    std::size_t pending() const {
        return count_ - next_;
    }

private:
    std::array<Command, 8> commands_{};
    std::size_t count_{0};
    std::size_t next_{0};
};

Command duration_command(double duration_s) {
    Command command{};
    command.pump.duration_s = duration_s;
    return command;
}

Command target_command(double target) {
    Command command{};
    command.sensor.target = target;
    return command;
}

Command run_command() {
    Command command{};
    command.pump.run = true;
    return command;
}

Command stop_command() {
    Command command{};
    command.pump.stop = true;
    return command;
}

// An initialized Application over real components, on stubbed hardware and
// flash. No sensor task runs, which leaves the pump to requests alone.
struct Fixture {
    Fixture() {
        adc_stub_reset();
        nvs_stub_reset();
        gpio_stub_level = 0;
        application.init();
        // Bring-up commits when it erases a table it cannot use. Only the commits
        // after it are a tick's.
        nvs_stub_commits = 0;
    }

    AdcReader adc_reader{{.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_0}};
    Sensor sensor{adc_reader};
    SensorSampler sensor_sampler{
        sensor, {.sensor_calibration = {.adc_min = ADC_MIN, .adc_max = ADC_MAX}, .ema_alpha = 0.2f}
    };
    PeriodicTask<SensorSampler> sensor_task{{.period_ms = 1000, .name = "sensor"}, sensor_sampler};
    GpioOutput gpio_output{GpioOutput::Config{.pin = GPIO_NUM_22}};
    Pump pump{gpio_output};
    FakeIndicator indicator{};
    NullNetworkProvider network_provider{};
    FakeMessaging messaging{};
    FakeWebHost web_host{};
    RuntimeSettings runtime_settings{{
        .sensor_target = TARGET,
        .sensor_deadband = DEADBAND,
        .sensor_calibration = {.adc_min = ADC_MIN, .adc_max = ADC_MAX},
        .pump_duration_us = DURATION_US,
        .pump_wait_time_us = WAIT_TIME_US,
        .pump_automatic = true,
    }};
    RuntimeSettingsStore runtime_settings_store{runtime_settings, {.version = 1}};
    IrrigationPolicy irrigation_policy{runtime_settings, {.max_reading_age_us = 60 * US_PER_S}};
    IrrigationController irrigation_controller{
        pump, irrigation_policy, runtime_settings, {.boot_lockout_us = 0}
    };
    CommandHandler command_handler{runtime_settings};
    TelemetryAssembler telemetry_assembler{
        "Device1", "0.0.0", "0000000", "ESP32-C6", "v0.1", "powerOn", false
    };
    FaultStore fault_store{};
    Application application{
        {.telemetry_interval_us = 10 * US_PER_S},
        {
            .sensor_sampler = sensor_sampler,
            .pump = pump,
            .indicator = indicator,
            .network_provider = network_provider,
            .messaging = messaging,
            .web_host = web_host,
            .sensor_task = sensor_task,
            .irrigation_controller = irrigation_controller,
            .command_handler = command_handler,
            .runtime_settings = runtime_settings,
            .runtime_settings_store = runtime_settings_store,
            .telemetry_assembler = telemetry_assembler,
            .fault_store = fault_store,
        }
    };
};

}  // namespace

TEST_CASE("a tick without a command changes nothing and writes nothing") {
    Fixture fixture{};
    fixture.application.tick(START_US);
    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(nvs_stub_commits == 0);
}

TEST_CASE("a setting a command changes is in use and stored within the tick") {
    Fixture fixture{};
    fixture.messaging.queue(duration_command(NEW_DURATION_S));
    fixture.application.tick(START_US);
    CHECK(fixture.runtime_settings.pump_duration() == NEW_DURATION_US);
    CHECK(nvs_stub_commits > 0);
}

TEST_CASE("a command repeating the setting in use writes nothing") {
    Fixture fixture{};
    fixture.messaging.queue(duration_command(DURATION_S));
    fixture.application.tick(START_US);
    CHECK(nvs_stub_commits == 0);
}

TEST_CASE("a stored setting is not written again by the ticks after it") {
    Fixture fixture{};
    fixture.messaging.queue(duration_command(NEW_DURATION_S));
    fixture.application.tick(START_US);
    const int commits{nvs_stub_commits};
    fixture.application.tick(START_US + 1);
    CHECK(nvs_stub_commits == commits);
}

TEST_CASE("a refused setting changes nothing and writes nothing") {
    Fixture fixture{};
    fixture.messaging.queue(duration_command(0.5));
    fixture.application.tick(START_US);
    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    CHECK(nvs_stub_commits == 0);
}

TEST_CASE("every command of a tick is applied, the ones after a change included") {
    Fixture fixture{};
    fixture.messaging.queue(duration_command(NEW_DURATION_S));
    fixture.messaging.queue(target_command(NEW_TARGET));
    fixture.application.tick(START_US);
    CHECK(fixture.runtime_settings.pump_duration() == NEW_DURATION_US);
    CHECK(fixture.runtime_settings.sensor_target() == NEW_TARGET);
}

TEST_CASE("a change is stored when a later command of the tick changes nothing") {
    Fixture fixture{};
    fixture.messaging.queue(duration_command(NEW_DURATION_S));
    fixture.messaging.queue(run_command());
    fixture.application.tick(START_US);
    CHECK(nvs_stub_commits > 0);
}

TEST_CASE("a run request starts the pump within the tick and stores nothing") {
    Fixture fixture{};
    fixture.messaging.queue(run_command());
    fixture.application.tick(START_US);
    CHECK(fixture.pump.is_on());
    CHECK(nvs_stub_commits == 0);
}

TEST_CASE("a stop request ends the run within the tick") {
    Fixture fixture{};
    fixture.messaging.queue(run_command());
    fixture.application.tick(START_US);
    REQUIRE(fixture.pump.is_on());
    fixture.messaging.queue(stop_command());
    fixture.application.tick(START_US + 1);
    CHECK_FALSE(fixture.pump.is_on());
}

TEST_CASE("a run carried with a new duration lasts the new duration") {
    Fixture fixture{};
    Command command{duration_command(NEW_DURATION_S)};
    command.pump.run = true;
    fixture.messaging.queue(command);
    fixture.application.tick(START_US);
    REQUIRE(fixture.pump.is_on());
    fixture.application.tick(START_US + NEW_DURATION_US - 1);
    CHECK(fixture.pump.is_on());
    fixture.application.tick(START_US + NEW_DURATION_US);
    CHECK_FALSE(fixture.pump.is_on());
}

TEST_CASE("a tick takes a bounded number of commands and the next takes the rest") {
    Fixture fixture{};
    for (std::size_t i{0}; i < COMMANDS_PER_TICK + 2; ++i) {
        fixture.messaging.queue(target_command(NEW_TARGET));
    }
    fixture.application.tick(START_US);
    CHECK(fixture.messaging.pending() == 2);
    fixture.application.tick(START_US + 1);
    CHECK(fixture.messaging.pending() == 0);
}

TEST_CASE("a new calibration maps the count already read within the tick") {
    Fixture fixture{};
    // Halfway between the endpoints in use, and a third of the way up the new pair
    adc_stub_read_value = 2000;
    fixture.sensor_sampler.tick(START_US);
    REQUIRE(fixture.sensor_sampler.snapshot().smoothed == doctest::Approx(50.0));

    Command command{};
    command.sensor.calibration.adc_max = 4000.0;
    fixture.messaging.queue(command);
    fixture.application.tick(START_US);
    CHECK(fixture.sensor_sampler.snapshot().smoothed == doctest::Approx(66.67).epsilon(0.001));
}
