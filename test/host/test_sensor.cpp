#include <doctest.h>
#include <esp_adc/adc_oneshot.h>

#include <array>

#include "fault/fault_manager.hpp"
#include "fault/fault_sink.hpp"
#include "fault/fault_store.hpp"
#include "hardware/sensor.hpp"

namespace {

// More failures than a burst has reads, whatever its length
constexpr int EVERY_READ{10000};

// The stub's conversions are global, and every fixture resets them.
struct Fixture {
    Fixture() {
        adc_stub_reset();
    }

    AdcReader adc_reader{{.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_0}};
    Sensor sensor{adc_reader};
};

// A store installed for one case and uninstalled with it, because a later case
// must not raise into a store that is gone.
struct FaultFixture {
    FaultFixture() {
        FaultManager::instance().install(sinks);
    }

    ~FaultFixture() {
        FaultManager::instance().install({});
    }

    FaultFixture(const FaultFixture&) = delete;
    FaultFixture& operator=(const FaultFixture&) = delete;

    FaultStore fault_store{};
    const std::array<FaultSink*, 1> sinks{&fault_store};
};

}  // namespace

TEST_CASE("a sensor whose ADC will not come up reports the failure") {
    Fixture fixture{};
    adc_stub_new_unit_result = ESP_FAIL;
    CHECK_FALSE(fixture.sensor.init());
    CHECK_FALSE(fixture.sensor.is_ready());
}

TEST_CASE("a sensor whose channel will not configure reports the failure") {
    Fixture fixture{};
    adc_stub_config_channel_result = ESP_FAIL;
    CHECK_FALSE(fixture.sensor.init());
    CHECK_FALSE(fixture.sensor.is_ready());
}

TEST_CASE("bringing a working sensor up again leaves it working") {
    Fixture fixture{};
    CHECK(fixture.sensor.init());
    CHECK(fixture.sensor.init());
    CHECK(fixture.sensor.is_ready());
}

TEST_CASE("a read reports the burst average") {
    Fixture fixture{};
    adc_stub_read_value = 1000;
    REQUIRE(fixture.sensor.init());
    const std::optional<std::uint32_t> raw{fixture.sensor.read()};
    REQUIRE(raw);
    CHECK(*raw == 1000);
}

TEST_CASE("a burst that loses some reads still averages the rest") {
    Fixture fixture{};
    adc_stub_read_value = 1000;
    adc_stub_read_failures = 10;  // A handful fail, the rest of the burst survives
    REQUIRE(fixture.sensor.init());
    const std::optional<std::uint32_t> raw{fixture.sensor.read()};
    REQUIRE(raw);
    CHECK(*raw == 1000);
}

TEST_CASE("a burst that loses every read reads nothing") {
    Fixture fixture{};
    adc_stub_read_value = 1000;
    adc_stub_read_failures = EVERY_READ;
    REQUIRE(fixture.sensor.init());
    CHECK_FALSE(fixture.sensor.read());
}

TEST_CASE("a sensor that comes up on a later attempt clears its init fault") {
    Fixture fixture{};
    const FaultFixture faults{};
    adc_stub_new_unit_result = ESP_FAIL;
    CHECK_FALSE(fixture.sensor.init());
    REQUIRE(faults.fault_store.worst_severity() == Severity::Error);

    adc_stub_new_unit_result = ESP_OK;
    REQUIRE(fixture.sensor.init());

    CHECK_FALSE(faults.fault_store.worst_severity());
}

TEST_CASE("a burst that reads again clears the fault a lost burst raised") {
    Fixture fixture{};
    const FaultFixture faults{};
    adc_stub_read_value = 1000;
    adc_stub_read_failures = EVERY_READ;
    REQUIRE(fixture.sensor.init());
    CHECK_FALSE(fixture.sensor.read());
    REQUIRE(faults.fault_store.worst_severity() == Severity::Error);

    adc_stub_read_failures = 0;
    REQUIRE(fixture.sensor.read());

    CHECK_FALSE(faults.fault_store.worst_severity());
}
