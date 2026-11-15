#include "sensor_sampler.hpp"

#include <esp_log.h>

#include <optional>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"

namespace {

constexpr char TAG[]{"SENSOR_SAMPLER"};

}  // namespace

SensorSampler::SensorSampler(Sensor& sensor, const Config& config)
    : sensor_{sensor}, sensor_calibration_{config.sensor_calibration}, ema_{config.ema_alpha} {}

void SensorSampler::tick(std::int64_t now_us) {
    if (!sensor_.is_ready() && !retry_timer_.ready(now_us)) {
        return;
    }
    if (const std::optional<std::uint32_t> raw{sample()}) {
        record(*raw, now_us);
    } else {
        // The sensor raises its own fault for a failed bring-up or burst.
        counts_.failed = true;
        restart_smoothing();
    }
    publish();
}

std::optional<std::uint32_t> SensorSampler::sample() {
    // A sensor brought up on this tick is read on it too; the claim needs no settling time.
    if (!sensor_.is_ready() && !sensor_.init()) {
        return std::nullopt;
    }
    return sensor_.read();
}

void SensorSampler::record(std::uint32_t raw, std::int64_t now_us) {
    counts_.raw = raw;
    // A count arrived. Whether the calibration can read it is judged again in
    // snapshot(), against the calibration in use then.
    counts_.failed = false;
    if (!mappable(raw, sensor_calibration_.load())) {
        restart_smoothing();
        return;
    }
    const std::optional<float> smoothed{smooth(raw)};
    if (!smoothed) {
        counts_.failed = true;
        return;
    }
    counts_ = {
        .smoothed = *smoothed,
        .latest = raw,
        .raw = raw,
        .sampled_at_us = now_us,
        .ready = true,
        .failed = false,
    };
}

void SensorSampler::set_sensor_calibration(const SensorCalibration& sensor_calibration) {
    sensor_calibration_.store(sensor_calibration);
    mappable(published_.load().raw, sensor_calibration);
}

bool SensorSampler::mappable(std::uint32_t raw, const SensorCalibration& sensor_calibration) {
    const bool plausible{sensor_calibration.plausible(raw)};
    if (!plausible) {
        faults::raise(fault_definitions::SENSOR_OUT_OF_RANGE);
        ESP_LOGE(
            TAG,
            "Reading out of range: raw=%lu, max=%u",
            static_cast<unsigned long>(raw),
            static_cast<unsigned>(sensor_calibration.adc_max)
        );
    } else {
        faults::clear(fault_definitions::SENSOR_OUT_OF_RANGE);
    }

    const bool calibrated{sensor_calibration.percent(raw).has_value()};
    if (!calibrated) {
        faults::raise(fault_definitions::SENSOR_CALIBRATION_INVALID);
        ESP_LOGE(
            TAG,
            "Calibration range %u..%u maps nothing",
            static_cast<unsigned>(sensor_calibration.adc_min),
            static_cast<unsigned>(sensor_calibration.adc_max)
        );
    } else {
        faults::clear(fault_definitions::SENSOR_CALIBRATION_INVALID);
    }

    return plausible && calibrated;
}

std::optional<float> SensorSampler::smooth(std::uint32_t raw) {
    const std::optional<float> smoothed{ema_.update(static_cast<float>(raw))};
    if (!smoothed) {
        faults::raise(fault_definitions::SENSOR_SMOOTHED_UNAVAILABLE);
        ESP_LOGE(TAG, "Smoothed reading unavailable");
    } else {
        faults::clear(fault_definitions::SENSOR_SMOOTHED_UNAVAILABLE);
    }
    return smoothed;
}

void SensorSampler::restart_smoothing() {
    ema_.reset();
}

void SensorSampler::publish() {
    published_.store(counts_);
}

SensorSnapshot SensorSampler::snapshot() const {
    const Counts counts{published_.load()};
    const SensorCalibration sensor_calibration{sensor_calibration_.load()};
    // The newest count is judged here, against the calibration in use now
    // rather than the one it was sampled under.
    const bool faulted{
        counts.failed || !sensor_calibration.plausible(counts.raw) ||
        !sensor_calibration.percent(counts.raw).has_value()
    };
    if (!counts.ready) {
        return {.raw = counts.raw, .faulted = faulted};
    }
    const std::optional<float> latest{sensor_calibration.percent(counts.latest)};
    const std::optional<float> smoothed{sensor_calibration.percent(counts.smoothed)};
    if (!latest || !smoothed) {
        return {.raw = counts.raw, .faulted = true};
    }
    return {
        .latest = *latest,
        .smoothed = *smoothed,
        .sampled_at_us = counts.sampled_at_us,
        .raw = counts.raw,
        .ready = true,
        .faulted = faulted,
    };
}
