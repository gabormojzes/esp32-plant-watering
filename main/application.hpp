#pragma once

#include <cstdint>

#include "fault/fault_store.hpp"
#include "hardware/pump.hpp"
#include "indicator/indicator.hpp"
#include "irrigation_controller.hpp"
#include "messaging/command_handler.hpp"
#include "messaging/messaging.hpp"
#include "messaging/telemetry_assembler.hpp"
#include "network/network_provider.hpp"
#include "periodic_task.hpp"
#include "runtime_settings.hpp"
#include "sensor_sampler.hpp"
#include "shared/constants.hpp"
#include "shared/periodic_timer.hpp"
#include "storage/runtime_settings_store.hpp"
#include "web/web_host.hpp"

class Application {
public:
    struct Config {
        std::int64_t telemetry_interval_us;
    };

    struct Components {
        SensorSampler& sensor_sampler;
        Pump& pump;
        Indicator& indicator;

        NetworkProvider& network_provider;
        Messaging& messaging;
        WebHost& web_host;

        PeriodicTask<SensorSampler>& sensor_task;
        IrrigationController& irrigation_controller;
        CommandHandler& command_handler;
        const RuntimeSettings& runtime_settings;
        RuntimeSettingsStore& runtime_settings_store;

        TelemetryAssembler& telemetry_assembler;
        const FaultStore& fault_store;
    };

    Application(const Config& config, const Components& components);
    void init();
    void tick(std::int64_t now_us);

private:
    static constexpr std::int64_t SYSTEM_INFO_INTERVAL_US{10 * constants::US_PER_S};

    // True when at least one command changed a persisted setting.
    [[nodiscard]] bool process_commands(std::int64_t now_us);
    void send_telemetry(const SensorSnapshot& sensor_snapshot, std::int64_t now_us);

    SensorSampler& sensor_sampler_;
    Pump& pump_;
    Indicator& indicator_;

    NetworkProvider& network_provider_;
    Messaging& messaging_;
    WebHost& web_host_;

    PeriodicTask<SensorSampler>& sensor_task_;
    IrrigationController& irrigation_controller_;
    CommandHandler& command_handler_;
    const RuntimeSettings& runtime_settings_;
    RuntimeSettingsStore& runtime_settings_store_;

    TelemetryAssembler& telemetry_assembler_;
    const FaultStore& fault_store_;

    PeriodicTimer telemetry_timer_;
    PeriodicTimer system_info_timer_{SYSTEM_INFO_INTERVAL_US};
};
