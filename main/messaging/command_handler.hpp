#pragma once

#include <optional>

#include "messaging/command.hpp"
#include "pump_request.hpp"
#include "runtime_settings.hpp"

// Converts each setting a received command carries to its units, validates it,
// and applies it to RuntimeSettings. Shared by every transport and format.
class CommandHandler {
public:
    explicit CommandHandler(RuntimeSettings& runtime_settings);
    // Returns true when at least one setting changed. A field carrying the value
    // already in use applies but changes nothing.
    [[nodiscard]] bool apply(const Command& command);
    // A stop outranks a run carried alongside it, which leaves the pump off when
    // the two disagree.
    static std::optional<PumpRequest> pump_request(const Command& command);

private:
    void apply_sensor_band(const Command::Sensor& sensor);
    void apply_sensor_calibration(const Command::Sensor::Calibration& calibration);
    void apply_pump_duration(std::optional<double> pump_duration_s);
    void apply_pump_wait_time(std::optional<double> pump_wait_time_s);
    void apply_pump_automatic(std::optional<bool> pump_automatic);

    RuntimeSettings& runtime_settings_;
};
