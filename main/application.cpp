#include "application.hpp"

#include <esp_log.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"
#include "messaging/messaging_info.hpp"
#include "system_info.hpp"

namespace {

constexpr char TAG[]{"APPLICATION"};

// Enough to drain a full inbox burst, and few enough that a flooded topic
// cannot delay the irrigation decision.
constexpr int MAX_COMMANDS_PER_TICK{4};

}  // namespace

Application::Application(const Config& config, const Components& components)
    : sensor_sampler_{components.sensor_sampler},
      pump_{components.pump},
      indicator_{components.indicator},

      network_provider_{components.network_provider},
      messaging_{components.messaging},
      web_host_{components.web_host},

      sensor_task_{components.sensor_task},
      irrigation_controller_{components.irrigation_controller},
      command_handler_{components.command_handler},
      runtime_settings_{components.runtime_settings},
      runtime_settings_store_{components.runtime_settings_store},

      telemetry_assembler_{components.telemetry_assembler},
      fault_store_{components.fault_store},

      telemetry_timer_{config.telemetry_interval_us} {}

void Application::init() {
    ESP_LOGI(TAG, "Initialization started");

    // First, to show Booting for the whole of bring-up.
    indicator_.init();

    // Storage before the network provider, whose driver may keep its own data
    // in the flash partition this mounts.
    runtime_settings_store_.init();
    // Before the sensor task starts, which then reads its first count through
    // the stored calibration.
    sensor_sampler_.set_sensor_calibration(runtime_settings_.sensor_calibration());

    // Before messaging and the web host, which need a network interface to
    // exist. Joining a network continues asynchronously.
    network_provider_.init();
    messaging_.start();

    // After messaging, because a transport claims its routes in start() and the
    // host registers them when it starts.
    web_host_.start();

    // Sampling gets its own task because a sample burst blocks for tens of
    // milliseconds, which would stall the control loop.
    if (!sensor_task_.start()) {
        // Without a reading the control loop never starts an automatic run.
        faults::raise(fault_definitions::SENSOR_TASK_START_FAILED);
    }
    pump_.init();

    ESP_LOGI(TAG, "Initialization finished");
}

void Application::tick(std::int64_t now_us) {
    indicator_.update(now_us);

    // Drained before the irrigation decision, which keeps this loop the only writer
    // of irrigation state and applies a fresh command in the same tick.
    if (process_commands(now_us)) {
        runtime_settings_store_.mark_dirty();
        // Before the snapshot below, which then reads through a new calibration.
        sensor_sampler_.set_sensor_calibration(runtime_settings_.sensor_calibration());
    }

    // One snapshot per tick, letting telemetry report the reading the irrigation
    // decision used.
    const SensorSnapshot sensor_snapshot{sensor_sampler_.snapshot()};
    // Polled every tick, which bounds pump stop latency to one loop delay.
    irrigation_controller_.update(sensor_snapshot, now_us);

    // Persisted after the decision, because a flash write must never land between
    // the reading and the pump.
    runtime_settings_store_.save_if_dirty();

    // Sent regardless of the link; messaging drops what it cannot deliver.
    if (telemetry_timer_.ready(now_us)) {
        send_telemetry(sensor_snapshot, now_us);
    }

    if (system_info_timer_.ready(now_us)) {
        system_info::log_memory_stats();
    }

    web_host_.tick(now_us);
}

bool Application::process_commands(std::int64_t now_us) {
    bool changed{false};

    for (int i{0}; i < MAX_COMMANDS_PER_TICK; ++i) {
        const std::optional<Command> command{messaging_.poll()};
        if (!command) {
            break;
        }

        // Applied first, because the opposite order short-circuits once something
        // has changed and drops every command after it.
        changed = command_handler_.apply(*command) || changed;

        // The settings apply first, which gives a run in the same command its new duration
        if (const std::optional<PumpRequest> pump_request{CommandHandler::pump_request(*command)}) {
            irrigation_controller_.request(*pump_request, now_us);
        }
    }

    return changed;
}

void Application::send_telemetry(const SensorSnapshot& sensor_snapshot, std::int64_t now_us) {
    std::array<FaultEntry, Telemetry::MAX_FAULTS> faults{};
    const std::size_t fault_count{fault_store_.snapshot(faults)};

    MessagingInfo messaging_info{};
    messaging_.describe(messaging_info);

    messaging_.send(telemetry_assembler_.assemble(
        runtime_settings_,
        sensor_snapshot,
        pump_.snapshot(),
        irrigation_controller_.state(now_us),
        network_provider_.info(),
        messaging_info,
        system_info::chip_temperature(),
        system_info::heap(),
        std::span{faults}.first(fault_count),
        now_us
    ));
}
