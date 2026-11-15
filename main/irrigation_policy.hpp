#pragma once

#include <cstdint>

#include "irrigation_state.hpp"
#include "runtime_settings.hpp"
#include "sensor_snapshot.hpp"

// The rules that decide when to water. A run may start once every permissive
// holds and the reading calls for water. It ends when a trip fires or the reading
// has cleared the band. The band around the target gives the hysteresis that
// keeps a reading near the target from toggling the run. Automatic watering
// turned off is both a permissive and a trip, holding every automatic run back
// and ending one in progress. A manual run ends only once its duration is up.
class IrrigationPolicy {
public:
    struct Config {
        std::int64_t max_reading_age_us;
    };

    IrrigationPolicy(const RuntimeSettings& runtime_settings, const Config& config);

    bool should_start(const SensorSnapshot& sensor_snapshot, std::int64_t now_us) const;
    bool should_stop(
        const SensorSnapshot& sensor_snapshot,
        const IrrigationState& irrigation_state,
        std::int64_t now_us
    ) const;
    // The reading and the automatic setting have no say over a manual run.
    bool should_stop_manual_run(const IrrigationState& irrigation_state) const;

private:
    bool permits_start(const SensorSnapshot& sensor_snapshot, std::int64_t now_us) const;
    bool tripped(
        const SensorSnapshot& sensor_snapshot,
        const IrrigationState& irrigation_state,
        std::int64_t now_us
    ) const;
    bool duration_reached(const IrrigationState& irrigation_state) const;
    bool demands(const SensorSnapshot& sensor_snapshot) const;
    bool demand_met(const SensorSnapshot& sensor_snapshot) const;
    // Both a permissive and a trip. The device never waters on a reading it
    // cannot trust.
    bool trusted(const SensorSnapshot& sensor_snapshot, std::int64_t now_us) const;
    bool stale(const SensorSnapshot& sensor_snapshot, std::int64_t now_us) const;

    const RuntimeSettings& runtime_settings_;
    Config config_{};
};
