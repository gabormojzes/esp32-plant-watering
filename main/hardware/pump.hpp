#pragma once

#include "hardware/pump_snapshot.hpp"

class GpioOutput;

// The device's water pump. Not thread-safe; one task owns it and makes every
// call.
class Pump {
public:
    explicit Pump(GpioOutput& gpio_output);
    // Brings the pump up. A pump that cannot switch reports faulted.
    void init();
    // Starts the pump. A faulted pump ignores this and stays off.
    void on();
    void off();
    bool is_on() const;
    bool is_faulted() const;
    PumpSnapshot snapshot() const;

private:
    GpioOutput& gpio_output_;
    bool on_{false};
    bool faulted_{false};
};
