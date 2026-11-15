#pragma once

#include <soc/gpio_num.h>

#include <cstdint>

// One pin driven as a push-pull output
class GpioOutput {
public:
    enum class Level : std::uint8_t {
        Low,
        High
    };

    struct Config {
        gpio_num_t pin;
    };

    explicit GpioOutput(const Config& config);
    // Claims the pin and drives it to `level`. Returns false when the pin cannot
    // be claimed.
    [[nodiscard]] bool init(Level level) const;
    // Only on a pin init() claimed
    void set_high() const;
    void set_low() const;

private:
    [[nodiscard]] bool configure_pin() const;

    Config config_{};
};
