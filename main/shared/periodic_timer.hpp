#pragma once

#include <cstdint>
#include <optional>

// A polled fixed-cadence timer. ready() is true on the first poll and then
// whenever the interval has elapsed, phase-locked to the first fire. A
// non-positive interval disables the timer. now_us is never negative.
class PeriodicTimer {
public:
    explicit PeriodicTimer(std::int64_t interval_us) : interval_us_{interval_us} {}

    [[nodiscard]] bool ready(std::int64_t now_us) {
        if (interval_us_ <= 0) {
            return false;
        }

        if (!interval_start_us_) {  // First call fires immediately and anchors the schedule
            interval_start_us_ = now_us;
            return true;
        }

        const std::int64_t elapsed_us{now_us - *interval_start_us_};
        if (elapsed_us < interval_us_) {
            return false;
        }

        // Skips missed intervals instead of firing them in a burst, staying
        // phase-locked to the original anchor.
        *interval_start_us_ += elapsed_us - (elapsed_us % interval_us_);
        return true;
    }

private:
    std::int64_t interval_us_{0};
    // The start of the interval now running, which never passes now_us. Empty
    // until the first fire.
    std::optional<std::int64_t> interval_start_us_;
};
