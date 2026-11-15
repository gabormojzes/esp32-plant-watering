#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "hardware/sensor.hpp"
#include "sensor_calibration.hpp"
#include "sensor_snapshot.hpp"
#include "shared/constants.hpp"
#include "shared/math_utils.hpp"
#include "shared/periodic_timer.hpp"

// Samples the sensor, smooths its raw counts, and publishes them. Counts become
// moisture only when read, through the calibration in use then. One task ticks
// it, and set_sensor_calibration() and snapshot() are safe from any.
class SensorSampler {
public:
    struct Config {
        // In use until set_sensor_calibration() replaces it
        SensorCalibration sensor_calibration;
        // Weight of each new burst in the smoothed count
        float ema_alpha;
    };

    SensorSampler(Sensor& sensor, const Config& config);
    // Takes one sample burst, retrying bring-up while the sensor is down.
    void tick(std::int64_t now_us);
    // The next snapshot() reads through the new calibration, and the newest
    // count is judged against it at once.
    void set_sensor_calibration(const SensorCalibration& sensor_calibration);
    SensorSnapshot snapshot() const;

private:
    // Bring-up can heal and is retried at this interval
    static constexpr std::int64_t INIT_RETRY_INTERVAL_US{30 * constants::US_PER_S};

    // What the sensor has measured, in raw counts
    struct Counts {
        float smoothed{0.0f};           // Smoothed from the counts that reached it
        std::uint32_t latest{0};        // Count of the last burst that reached `smoothed`
        std::uint32_t raw{0};           // Newest count, even one kept out of `smoothed`
        std::int64_t sampled_at_us{0};  // When `latest` was read
        bool ready{false};              // True once a count has reached `smoothed`
        bool failed{false};             // Sensor down, burst lost or smoothing refused
    };

    // Reads one burst, bringing the sensor up first when it is down. Nullopt when
    // either fails.
    [[nodiscard]] std::optional<std::uint32_t> sample();
    // Takes a raw count into counts_. A count the calibration cannot map stays
    // out of `smoothed`, and is still carried for field calibration.
    void record(std::uint32_t raw, std::int64_t now_us);
    // True when the calibration maps the count to moisture. Raises the fault for
    // each check it fails and clears the others.
    static bool mappable(std::uint32_t raw, const SensorCalibration& sensor_calibration);
    // Folds the count into the smoothed one. Nullopt when the weight cannot smooth.
    [[nodiscard]] std::optional<float> smooth(std::uint32_t raw);
    // Lets the next count that reaches `smoothed` start it afresh. Called for
    // every tick that yields none, since the soil may have changed unseen.
    void restart_smoothing();
    void publish();

    Sensor& sensor_;
    // Written by set_sensor_calibration(), read by tick() and snapshot()
    std::atomic<SensorCalibration> sensor_calibration_;
    // Written by tick(), read by snapshot() and set_sensor_calibration(). Not lock-free
    // on ESP, where every access runs in a critical section.
    std::atomic<Counts> published_;
    // Only tick() touches these. published_ holds a copy of counts_.
    Counts counts_{};
    math_utils::Ema ema_;
    PeriodicTimer retry_timer_{INIT_RETRY_INTERVAL_US};
};
