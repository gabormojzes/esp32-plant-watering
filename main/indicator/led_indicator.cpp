#include "indicator/led_indicator.hpp"

#include "hardware/rgb_led.hpp"

void LedIndicator::init() {
    led_.init();
    led_.set(look_for(Status::Booting).color);
}

void LedIndicator::update(std::int64_t now_us) {
    led_.set(color_for(chain_.status(), now_us));
}
