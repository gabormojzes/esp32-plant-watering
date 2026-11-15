#pragma once

#include <esp_err.h>
#include <led_strip_types.h>
#include <soc/gpio_num.h>

#include "shared/color.hpp"

class RgbLed {
public:
    struct Config {
        gpio_num_t pin;
    };

    explicit RgbLed(const Config& config);
    // Frees the peripheral the strip holds.
    ~RgbLed();
    // A copy would free the peripheral twice.
    RgbLed(const RgbLed&) = delete;
    RgbLed& operator=(const RgbLed&) = delete;

    void init();
    void set(Color color);

private:
    // Creates the strip on the chip's RMT backend, or on SPI when the chip has no
    // RMT.
    [[nodiscard]] esp_err_t claim(const led_strip_config_t& strip_config);
    // Releases the strip and leaves the LED dark. Safe when no strip is claimed.
    void release();
    void write(Color color);

    Config config_{};
    led_strip_handle_t strip_{nullptr};
    // Last color confirmed on the strip. Starts at full white, which set()'s
    // brightness scaling never produces, forcing the first set() to write.
    Color color_{palette::WHITE};
    // Reports a failing write once, not on every set() that retries it
    bool write_failed_{false};
};
