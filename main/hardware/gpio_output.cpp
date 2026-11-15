#include "hardware/gpio_output.hpp"

#include <driver/gpio.h>
#include <esp_log.h>
#include <soc/soc_caps.h>

namespace {

constexpr char TAG[]{"GPIO_OUTPUT"};

}  // namespace

GpioOutput::GpioOutput(const Config& config) : config_{config} {}

bool GpioOutput::init(Level level) const {
    if (!configure_pin()) {
        return false;
    }
    switch (level) {
        case Level::High:
            set_high();
            break;
        case Level::Low:
            set_low();
            break;
    }
    return true;
}

bool GpioOutput::configure_pin() const {
    // Rejected before the mask is built, because a negative pin would make the
    // shift undefined before gpio_config could refuse it.
    const bool drivable{GPIO_IS_VALID_OUTPUT_GPIO(config_.pin)};
    if (!drivable) {
        ESP_LOGE(TAG, "Pin %d cannot drive an output", static_cast<int>(config_.pin));
        return false;
    }
    const gpio_config_t pin_config{
        .pin_bit_mask = 1ULL << config_.pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
        // Hysteresis only filters what a pin reads, and this one is only driven.
        .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE,
#endif
    };
    const esp_err_t err{gpio_config(&pin_config)};
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Pin claim failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

// Both setters drop gpio_set_level's result. It refuses only a pin no output can drive, and
// init() never claims one.
void GpioOutput::set_high() const {
    gpio_set_level(config_.pin, 1);
}

void GpioOutput::set_low() const {
    gpio_set_level(config_.pin, 0);
}
