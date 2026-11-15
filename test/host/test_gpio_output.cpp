#include <doctest.h>
#include <driver/gpio.h>

#include "hardware/gpio_output.hpp"

namespace {

constexpr gpio_num_t PIN{GPIO_NUM_22};

// A claimable pin, with the recorded level set high for a write of low to show up.
struct Fixture {
    Fixture() {
        gpio_stub_config_result = ESP_OK;
        gpio_stub_level = 1;
        gpio_stub_set_level_calls = 0;
    }

    GpioOutput output{GpioOutput::Config{.pin = PIN}};
};

}  // namespace

TEST_CASE("init claims the pin and drives it low when told low") {
    const Fixture fixture{};
    CHECK(fixture.output.init(GpioOutput::Level::Low));
    CHECK(gpio_stub_level == 0);
}

TEST_CASE("init drives high for a load that rests there") {
    const Fixture fixture{};
    // Start low, letting the write of high show.
    gpio_stub_level = 0;
    CHECK(fixture.output.init(GpioOutput::Level::High));
    CHECK(gpio_stub_level == 1);
}

TEST_CASE("init reports a pin it cannot claim and drives nothing") {
    const Fixture fixture{};
    gpio_stub_config_result = ESP_FAIL;
    CHECK_FALSE(fixture.output.init(GpioOutput::Level::Low));
    CHECK(gpio_stub_set_level_calls == 0);
}

TEST_CASE("init refuses a pin no output can be driven on") {
    // The fixture only resets the recorded pin state. The output under test is
    // its own, on a pin the device does not have.
    const Fixture fixture{};
    const GpioOutput unfitted{GpioOutput::Config{.pin = GPIO_NUM_NC}};
    // Not even attempted, because the pin bit mask is built by shifting by the
    // pin number, which this one lacks.
    CHECK_FALSE(unfitted.init(GpioOutput::Level::Low));
    CHECK(gpio_stub_set_level_calls == 0);
}

TEST_CASE("high and low drive the pin") {
    const Fixture fixture{};
    fixture.output.set_high();
    CHECK(gpio_stub_level == 1);
    fixture.output.set_low();
    CHECK(gpio_stub_level == 0);
}

TEST_CASE("every call reaches the pin") {
    const Fixture fixture{};
    fixture.output.set_high();
    fixture.output.set_high();
    CHECK(gpio_stub_set_level_calls == 2);
}
