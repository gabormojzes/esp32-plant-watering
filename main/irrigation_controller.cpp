#include "irrigation_controller.hpp"

#include <esp_log.h>

#include <algorithm>

namespace {

constexpr char TAG[]{"IRRIGATION_CONTROLLER"};

}  // namespace

IrrigationController::IrrigationController(
    Pump& pump,
    const IrrigationPolicy& irrigation_policy,
    const RuntimeSettings& runtime_settings,
    const Config& config
)
    : pump_{pump},
      irrigation_policy_{irrigation_policy},
      runtime_settings_{runtime_settings},
      config_{config} {}

void IrrigationController::update(const SensorSnapshot& sensor_snapshot, std::int64_t now_us) {
    const IrrigationState irrigation_state{state(now_us)};
    switch (phase_) {
        case Phase::Waiting:
            if (irrigation_state.remaining_wait_time_us != 0) {
                break;
            }
            phase_ = Phase::Ready;
            [[fallthrough]];
        case Phase::Ready:
            // A wait time raised after the wait ended reopens it.
            if (irrigation_state.remaining_wait_time_us != 0) {
                phase_ = Phase::Waiting;
                break;
            }
            if (irrigation_policy_.should_start(sensor_snapshot, now_us)) {
                start_run(Phase::Running, now_us);
            }
            break;
        case Phase::Running:
            if (irrigation_policy_.should_stop(sensor_snapshot, irrigation_state, now_us)) {
                stop_run(now_us);
            }
            break;
        case Phase::RunningManually:
            if (irrigation_policy_.should_stop_manual_run(irrigation_state)) {
                stop_run(now_us);
            }
            break;
    }
}

void IrrigationController::request(PumpRequest pump_request, std::int64_t now_us) {
    switch (pump_request) {
        case PumpRequest::Run:
            if (is_running()) {
                ESP_LOGD(TAG, "Run request ignored, a run is in progress");
                return;
            }
            start_run(Phase::RunningManually, now_us);
            break;
        case PumpRequest::Stop:
            if (is_running()) {
                stop_run(now_us);
            }
            break;
    }
}

IrrigationState IrrigationController::state(std::int64_t now_us) const {
    return {
        .run_count = run_count_,
        .current_run_time_us = current_run_time(now_us),
        .idle_time_us = idle_time(now_us),
        .remaining_wait_time_us = remaining_wait_time(now_us),
        .total_run_time_us = total_run_time(now_us),
    };
}

bool IrrigationController::is_running() const {
    return phase_ == Phase::Running || phase_ == Phase::RunningManually;
}

void IrrigationController::start_run(Phase run_phase, std::int64_t now_us) {
    // Called without a fault check. A faulted pump ignores it.
    pump_.on();
    // A pump that did not switch on started no run.
    if (!pump_.is_on()) {
        return;
    }
    phase_ = run_phase;
    last_start_us_ = now_us;
    ++run_count_;
}

void IrrigationController::stop_run(std::int64_t now_us) {
    pump_.off();
    total_run_time_us_ += current_run_time(now_us);
    phase_ = Phase::Waiting;
    last_stop_us_ = now_us;
}

std::int64_t IrrigationController::current_run_time(std::int64_t now_us) const {
    return is_running() ? now_us - last_start_us_ : 0;
}

std::int64_t IrrigationController::idle_time(std::int64_t now_us) const {
    if (is_running()) {
        return 0;
    }
    return now_us - last_stop_us_.value_or(0);
}

std::int64_t IrrigationController::remaining_wait_time(std::int64_t now_us) const {
    if (is_running()) {
        // The wait counts from the stop, which leaves all of it ahead.
        return runtime_settings_.pump_wait_time();
    }
    if (!last_stop_us_) {
        // now_us counts from boot and is itself the elapsed part of the lockout.
        return std::max<std::int64_t>(config_.boot_lockout_us - now_us, 0);
    }
    return std::max<std::int64_t>(
        runtime_settings_.pump_wait_time() - (now_us - *last_stop_us_), 0
    );
}

std::int64_t IrrigationController::total_run_time(std::int64_t now_us) const {
    return total_run_time_us_ + current_run_time(now_us);
}
