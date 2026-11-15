#pragma once

#include <cstdint>
#include <optional>

#include "hardware/pump.hpp"
#include "irrigation_policy.hpp"
#include "irrigation_state.hpp"
#include "pump_request.hpp"
#include "runtime_settings.hpp"
#include "sensor_snapshot.hpp"

// The irrigation state machine. Waiting holds through the boot lockout and the wait
// after a stop, Ready asks the policy whether to start, and Running whether to stop.
// A manual run starts from Waiting or Ready alike and runs as RunningManually,
// which asks the policy only whether its duration is up.
class IrrigationController {
public:
    struct Config {
        // Minimum delay after boot before the first automatic run
        std::int64_t boot_lockout_us;
    };

    IrrigationController(
        Pump& pump,
        const IrrigationPolicy& irrigation_policy,
        const RuntimeSettings& runtime_settings,
        const Config& config
    );
    void update(const SensorSnapshot& sensor_snapshot, std::int64_t now_us);
    // Acts on the request at once. A run starts only while the pump is off. A stop
    // ends a run of either kind as if it had finished, and the wait follows.
    void request(PumpRequest pump_request, std::int64_t now_us);
    IrrigationState state(std::int64_t now_us) const;

private:
    enum class Phase : std::uint8_t {
        Waiting,
        Ready,
        Running,
        RunningManually
    };

    bool is_running() const;
    void start_run(Phase run_phase, std::int64_t now_us);
    void stop_run(std::int64_t now_us);
    // Zero between runs
    std::int64_t current_run_time(std::int64_t now_us) const;
    // Time since the last stop, or since boot before the first run. Zero during a
    // run.
    std::int64_t idle_time(std::int64_t now_us) const;
    // Wait left after the last stop, or boot lockout left before the first run.
    // The whole wait during a run.
    std::int64_t remaining_wait_time(std::int64_t now_us) const;
    // Includes a run in progress.
    std::int64_t total_run_time(std::int64_t now_us) const;

    Pump& pump_;
    const IrrigationPolicy& irrigation_policy_;
    const RuntimeSettings& runtime_settings_;
    Config config_{};
    // The lockout runs from boot
    Phase phase_{Phase::Waiting};
    // Start time of the last run, stale once it stops
    std::int64_t last_start_us_{0};
    std::optional<std::int64_t> last_stop_us_;
    std::uint32_t run_count_{0};
    // Completed run time
    std::int64_t total_run_time_us_{0};
};
