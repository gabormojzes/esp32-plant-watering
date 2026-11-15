#pragma once

#include <optional>

// One received command as sent. A field is set only when the payload carried
// it, and holds the value as sent, unconverted and unvalidated: a number in
// seconds, percent or raw counts, or a flag.
struct Command {
    struct Sensor {
        struct Calibration {
            std::optional<double> adc_min;
            std::optional<double> adc_max;
        };

        std::optional<double> target;
        std::optional<double> deadband;
        Calibration calibration{};
    };

    struct Pump {
        std::optional<double> duration_s;
        std::optional<double> wait_time_s;
        std::optional<bool> automatic;
        std::optional<bool> run;
        std::optional<bool> stop;
    };

    Sensor sensor{};
    Pump pump{};
};
