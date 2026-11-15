#include "irrigation_policy.hpp"

IrrigationPolicy::IrrigationPolicy(const RuntimeSettings& runtime_settings, const Config& config)
    : runtime_settings_{runtime_settings}, config_{config} {}

bool IrrigationPolicy::should_start(
    const SensorSnapshot& sensor_snapshot, std::int64_t now_us
) const {
    return permits_start(sensor_snapshot, now_us) && demands(sensor_snapshot);
}

bool IrrigationPolicy::should_stop(
    const SensorSnapshot& sensor_snapshot,
    const IrrigationState& irrigation_state,
    std::int64_t now_us
) const {
    return tripped(sensor_snapshot, irrigation_state, now_us) || demand_met(sensor_snapshot);
}

bool IrrigationPolicy::should_stop_manual_run(const IrrigationState& irrigation_state) const {
    return duration_reached(irrigation_state);
}

bool IrrigationPolicy::permits_start(
    const SensorSnapshot& sensor_snapshot, std::int64_t now_us
) const {
    return runtime_settings_.pump_automatic() && trusted(sensor_snapshot, now_us);
}

bool IrrigationPolicy::tripped(
    const SensorSnapshot& sensor_snapshot,
    const IrrigationState& irrigation_state,
    std::int64_t now_us
) const {
    return !runtime_settings_.pump_automatic() || !trusted(sensor_snapshot, now_us) ||
           duration_reached(irrigation_state);
}

bool IrrigationPolicy::duration_reached(const IrrigationState& irrigation_state) const {
    return irrigation_state.current_run_time_us >= runtime_settings_.pump_duration();
}

bool IrrigationPolicy::demands(const SensorSnapshot& sensor_snapshot) const {
    return sensor_snapshot.smoothed <
           runtime_settings_.sensor_target() - runtime_settings_.sensor_deadband();
}

bool IrrigationPolicy::demand_met(const SensorSnapshot& sensor_snapshot) const {
    return sensor_snapshot.smoothed >=
           runtime_settings_.sensor_target() + runtime_settings_.sensor_deadband();
}

bool IrrigationPolicy::trusted(const SensorSnapshot& sensor_snapshot, std::int64_t now_us) const {
    return sensor_snapshot.ready && !sensor_snapshot.faulted && !stale(sensor_snapshot, now_us);
}

bool IrrigationPolicy::stale(const SensorSnapshot& sensor_snapshot, std::int64_t now_us) const {
    return (now_us - sensor_snapshot.sampled_at_us) > config_.max_reading_age_us;
}
