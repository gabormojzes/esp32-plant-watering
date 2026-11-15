#include "hardware/pump.hpp"

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"
#include "hardware/gpio_output.hpp"

Pump::Pump(GpioOutput& gpio_output) : gpio_output_{gpio_output} {}

void Pump::init() {
    // Active high; low is off
    const bool output_ready{gpio_output_.init(GpioOutput::Level::Low)};
    // Without its pin the pump cannot switch, and on() then does nothing while
    // is_on() stays false.
    faulted_ = !output_ready;
    if (faulted_) {
        faults::raise(fault_definitions::PUMP_BRING_UP_FAILED);
    }
}

void Pump::on() {
    if (faulted_ || on_) {
        return;
    }
    gpio_output_.set_high();
    on_ = true;
}

void Pump::off() {
    if (!on_) {
        return;
    }
    gpio_output_.set_low();
    on_ = false;
}

bool Pump::is_on() const {
    return on_;
}

bool Pump::is_faulted() const {
    return faulted_;
}

PumpSnapshot Pump::snapshot() const {
    return {.on = on_, .faulted = faulted_};
}
