#include <doctest.h>
#include <driver/gpio.h>

#include "hardware/gpio_output.hpp"
#include "hardware/pump.hpp"

namespace {

constexpr gpio_num_t PIN{GPIO_NUM_22};

// A pump over a claimable pin. For a faulted pump, a case fails the claim
// before calling init().
struct Fixture {
    Fixture() {
        gpio_stub_config_result = ESP_OK;
        gpio_stub_level = 1;
        gpio_stub_set_level_calls = 0;
    }

    GpioOutput gpio_output{GpioOutput::Config{.pin = PIN}};
    Pump pump{gpio_output};
};

}  // namespace

TEST_CASE("a pump starts off with its pin low") {
    Fixture fixture{};
    fixture.pump.init();
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(gpio_stub_level == 0);
}

TEST_CASE("a healthy pump is not faulted") {
    Fixture fixture{};
    fixture.pump.init();
    CHECK_FALSE(fixture.pump.is_faulted());
}

TEST_CASE("a pump whose bring-up failed reports faulted") {
    Fixture fixture{};
    gpio_stub_config_result = ESP_FAIL;
    fixture.pump.init();
    CHECK(fixture.pump.is_faulted());
}

TEST_CASE("a pump whose bring-up failed ignores requests and stays off") {
    Fixture fixture{};
    gpio_stub_config_result = ESP_FAIL;
    fixture.pump.init();
    gpio_stub_config_result = ESP_OK;
    fixture.pump.on();
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(gpio_stub_set_level_calls == 0);  // The pin was never driven
}

TEST_CASE("on and off switch the pump") {
    Fixture fixture{};
    fixture.pump.init();
    fixture.pump.on();
    CHECK(fixture.pump.is_on());
    CHECK(gpio_stub_level == 1);
    fixture.pump.off();
    CHECK_FALSE(fixture.pump.is_on());
    CHECK(gpio_stub_level == 0);
}

TEST_CASE("on and off are idempotent") {
    Fixture fixture{};
    fixture.pump.init();
    gpio_stub_set_level_calls = 0;  // Discount the write init made
    fixture.pump.on();
    fixture.pump.on();
    CHECK(fixture.pump.is_on());
    fixture.pump.off();
    fixture.pump.off();
    CHECK_FALSE(fixture.pump.is_on());
    // The pump holds the level the output does not, giving one write each way.
    CHECK(gpio_stub_set_level_calls == 2);
}

TEST_CASE("snapshot carries the pump's on and fault state") {
    Fixture fixture{};
    fixture.pump.init();
    fixture.pump.on();
    const PumpSnapshot s{fixture.pump.snapshot()};
    CHECK(s.on);
    CHECK_FALSE(s.faulted);
}
