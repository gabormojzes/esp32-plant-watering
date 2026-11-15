#include <doctest.h>
#include <esp_adc/adc_oneshot.h>

#include <array>

#include "fault/fault_manager.hpp"
#include "fault/fault_sink.hpp"
#include "fault/fault_store.hpp"
#include "sensor_sampler.hpp"
#include "shared/constants.hpp"

using constants::US_PER_S;

namespace {

constexpr std::uint16_t ADC_MIN{0};
constexpr std::uint16_t ADC_MAX{2000};
// Above ADC_MAX, read as a disconnected sensor
constexpr int OUT_OF_RANGE{2100};
// More failures than a burst has reads, whatever its length
constexpr int EVERY_READ{10000};
// The fixture's smoothing weight, a fifth of each new burst
constexpr float EMA_ALPHA{0.2f};
// Past the smoothing's upper bound, which makes it refuse every reading
constexpr float REFUSED_ALPHA{2.0f};
// Past the sampler's bring-up retry interval, whatever it holds
constexpr std::int64_t AFTER_RETRY_US{60 * US_PER_S};

// The stub's conversions are global, and every fixture resets them.
struct Fixture {
    explicit Fixture(
        float ema_alpha = EMA_ALPHA,
        std::uint16_t adc_min = ADC_MIN,
        std::uint16_t adc_max = ADC_MAX
    )
        : sampler{
              sensor,
              {.sensor_calibration = {.adc_min = adc_min, .adc_max = adc_max},
               .ema_alpha = ema_alpha}
          } {
        adc_stub_reset();
    }

    AdcReader adc_reader{{.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_0}};
    Sensor sensor{adc_reader};
    SensorSampler sampler;
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

TEST_CASE("a sampler whose sensor will not come up is faulted and not ready") {
    Fixture fixture{};
    adc_stub_new_unit_result = ESP_FAIL;
    fixture.sampler.tick(0);  // The retry timer fires on its first poll
    const SensorSnapshot s{fixture.sampler.snapshot()};
    CHECK_FALSE(s.ready);
    CHECK(s.faulted);
}

TEST_CASE("a sampler does not sample while its sensor is down") {
    Fixture fixture{};
    adc_stub_new_unit_result = ESP_FAIL;
    adc_stub_read_value = 1000;
    fixture.sampler.tick(0);
    fixture.sampler.tick(1);
    CHECK_FALSE(fixture.sampler.snapshot().ready);
}

TEST_CASE("a sampler whose sensor comes up late starts sampling") {
    Fixture fixture{};
    adc_stub_new_unit_result = ESP_FAIL;
    fixture.sampler.tick(0);

    adc_stub_new_unit_result = ESP_OK;
    adc_stub_read_value = 1000;
    fixture.sampler.tick(AFTER_RETRY_US);  // The retry brings it up and samples
    CHECK(fixture.sampler.snapshot().ready);
}

TEST_CASE("a sampler publishes the moisture a burst read") {
    Fixture fixture{};
    adc_stub_read_value = 1000;
    fixture.sampler.tick(0);  // The first tick brings the sensor up and samples
    const SensorSnapshot s{fixture.sampler.snapshot()};
    CHECK(s.ready);
    CHECK_FALSE(s.faulted);
    CHECK(s.raw == 1000);
    CHECK(s.latest == doctest::Approx(50.0f));
}

TEST_CASE("the wet endpoint of the calibration reads as fully moist") {
    Fixture fixture{};
    adc_stub_read_value = ADC_MIN;
    fixture.sampler.tick(0);
    CHECK(fixture.sampler.snapshot().latest == doctest::Approx(100.0f));
}

TEST_CASE("the dry endpoint of the calibration reads as fully dry") {
    Fixture fixture{};
    adc_stub_read_value = ADC_MAX;
    fixture.sampler.tick(0);
    const SensorSnapshot s{fixture.sampler.snapshot()};
    // An unpublished snapshot also reads 0%, which leaves ready as the only sign
    // the reading landed.
    CHECK(s.ready);
    CHECK(s.latest == doctest::Approx(0.0f));
}

TEST_CASE("a calibration that maps nothing leaves the raw count on its own") {
    // Endpoints that meet span no range
    Fixture fixture{EMA_ALPHA, ADC_MAX, ADC_MAX};
    adc_stub_read_value = ADC_MAX;
    fixture.sampler.tick(0);
    const SensorSnapshot s{fixture.sampler.snapshot()};
    CHECK(s.faulted);
    CHECK_FALSE(s.ready);
    CHECK(s.raw == ADC_MAX);
}

TEST_CASE("the smoothed reading seeds on the first burst and trails the ones after it") {
    Fixture fixture{};
    adc_stub_read_value = ADC_MIN;
    fixture.sampler.tick(0);
    CHECK(fixture.sampler.snapshot().smoothed == doctest::Approx(100.0f));

    adc_stub_read_value = ADC_MAX;
    fixture.sampler.tick(1);
    const SensorSnapshot s{fixture.sampler.snapshot()};
    CHECK(s.latest == doctest::Approx(0.0f));
    CHECK(s.smoothed == doctest::Approx(80.0f));
}

TEST_CASE("a sampler stamps a reading with the tick it was taken on") {
    Fixture fixture{};
    adc_stub_read_value = 1000;
    const std::int64_t tick_us{12345};
    fixture.sampler.tick(tick_us);
    CHECK(fixture.sampler.snapshot().sampled_at_us == tick_us);
}

TEST_CASE("a burst that yields nothing keeps the stamp of the one that did") {
    Fixture fixture{};
    adc_stub_read_value = 1000;
    const std::int64_t reading_us{12345};
    fixture.sampler.tick(reading_us);

    adc_stub_read_failures = EVERY_READ;
    fixture.sampler.tick(reading_us + 1000);
    CHECK(fixture.sampler.snapshot().sampled_at_us == reading_us);
}

TEST_CASE("a burst that loses every read faults and keeps the last reading") {
    Fixture fixture{};
    adc_stub_read_value = 1000;
    fixture.sampler.tick(0);

    adc_stub_read_failures = EVERY_READ;
    fixture.sampler.tick(1);

    const SensorSnapshot s{fixture.sampler.snapshot()};
    CHECK(s.faulted);
    CHECK(s.ready);
    CHECK(s.raw == 1000);
    CHECK(s.latest == doctest::Approx(50.0f));
}

TEST_CASE("a reading past the calibration range faults the sampler and still reports its raw") {
    Fixture fixture{};
    adc_stub_read_value = OUT_OF_RANGE;
    fixture.sampler.tick(0);
    const SensorSnapshot s{fixture.sampler.snapshot()};
    CHECK(s.faulted);
    CHECK(s.raw == OUT_OF_RANGE);
}

TEST_CASE("a reading one count above the dry endpoint faults the sampler") {
    Fixture fixture{};
    adc_stub_read_value = ADC_MAX + 1;
    fixture.sampler.tick(0);
    CHECK(fixture.sampler.snapshot().faulted);
}

TEST_CASE("a reading past the calibration range never reaches the smoothed reading") {
    Fixture fixture{};
    adc_stub_read_value = 1000;
    fixture.sampler.tick(0);
    const float seeded{fixture.sampler.snapshot().smoothed};

    adc_stub_read_value = OUT_OF_RANGE;
    fixture.sampler.tick(1);
    // Mapping it would clamp it to bone dry and drag the smoothed reading toward a run.
    CHECK(fixture.sampler.snapshot().smoothed == doctest::Approx(seeded));
}

TEST_CASE("the smoothed reading starts over after a reading past the calibration range") {
    Fixture fixture{};
    adc_stub_read_value = ADC_MIN;
    fixture.sampler.tick(0);

    adc_stub_read_value = OUT_OF_RANGE;
    fixture.sampler.tick(1);

    adc_stub_read_value = 1000;
    fixture.sampler.tick(2);
    // Trailing on from before the gap would read 90%.
    CHECK(fixture.sampler.snapshot().smoothed == doctest::Approx(50.0f));
}

TEST_CASE("the smoothed reading starts over after a burst that loses every read") {
    Fixture fixture{};
    adc_stub_read_value = ADC_MIN;
    fixture.sampler.tick(0);

    adc_stub_read_failures = EVERY_READ;
    fixture.sampler.tick(1);

    adc_stub_read_failures = 0;
    adc_stub_read_value = 1000;
    fixture.sampler.tick(2);
    CHECK(fixture.sampler.snapshot().smoothed == doctest::Approx(50.0f));
}

TEST_CASE("a reading back in range clears the fault") {
    Fixture fixture{};
    adc_stub_read_value = OUT_OF_RANGE;
    fixture.sampler.tick(0);
    CHECK(fixture.sampler.snapshot().faulted);

    adc_stub_read_value = 1000;
    fixture.sampler.tick(1);
    CHECK_FALSE(fixture.sampler.snapshot().faulted);
}

TEST_CASE("a sampler whose smoothing refuses its weight is faulted and reports only the raw") {
    Fixture fixture{REFUSED_ALPHA};
    adc_stub_read_value = 1000;
    fixture.sampler.tick(0);
    const SensorSnapshot s{fixture.sampler.snapshot()};
    CHECK(s.faulted);
    // A reading it cannot smooth is no reading, and only its raw count lands.
    CHECK_FALSE(s.ready);
    CHECK(s.raw == 1000);
}

TEST_CASE("a sampler whose smoothing refuses its weight keeps refusing") {
    Fixture fixture{REFUSED_ALPHA};
    adc_stub_read_value = 1000;
    fixture.sampler.tick(0);
    adc_stub_read_value = ADC_MIN;  // A reading the calibration maps perfectly well
    fixture.sampler.tick(1);
    CHECK_FALSE(fixture.sampler.snapshot().ready);
}

TEST_CASE("a new calibration maps the readings through it with no new burst") {
    Fixture fixture{};
    adc_stub_read_value = 1000;
    fixture.sampler.tick(0);
    REQUIRE(fixture.sampler.snapshot().latest == doctest::Approx(50.0f));

    fixture.sampler.set_sensor_calibration({.adc_min = ADC_MIN, .adc_max = 4000});
    const SensorSnapshot s{fixture.sampler.snapshot()};
    CHECK(s.latest == doctest::Approx(75.0f));
    CHECK(s.smoothed == doctest::Approx(75.0f));
    CHECK(s.sampled_at_us == 0);  // Still the burst that was read
}

TEST_CASE("the smoothed reading keeps its history through a new calibration") {
    Fixture fixture{};
    adc_stub_read_value = ADC_MIN;
    fixture.sampler.tick(0);
    adc_stub_read_value = ADC_MAX;
    fixture.sampler.tick(1);  // The smoothed reading sits a fifth of the way to ADC_MAX, at 400

    fixture.sampler.set_sensor_calibration({.adc_min = ADC_MIN, .adc_max = 4000});
    CHECK(fixture.sampler.snapshot().smoothed == doctest::Approx(90.0f));
}

TEST_CASE("an adc_max lowered under the newest reading faults it at once") {
    const FaultFixture faults{};
    Fixture fixture{};
    adc_stub_read_value = 1000;
    fixture.sampler.tick(0);
    REQUIRE_FALSE(faults.fault_store.worst_severity());

    fixture.sampler.set_sensor_calibration({.adc_min = ADC_MIN, .adc_max = 900});
    CHECK(fixture.sampler.snapshot().faulted);
    CHECK(faults.fault_store.worst_severity() == Severity::Error);
}

TEST_CASE("an adc_max raised over an out-of-range reading clears it at once") {
    const FaultFixture faults{};
    Fixture fixture{};
    adc_stub_read_value = 1000;
    fixture.sampler.tick(0);
    adc_stub_read_value = OUT_OF_RANGE;
    fixture.sampler.tick(1);
    REQUIRE(fixture.sampler.snapshot().faulted);
    REQUIRE(faults.fault_store.worst_severity() == Severity::Error);

    fixture.sampler.set_sensor_calibration({.adc_min = ADC_MIN, .adc_max = 4000});
    const SensorSnapshot s{fixture.sampler.snapshot()};
    CHECK_FALSE(s.faulted);
    CHECK_FALSE(faults.fault_store.worst_severity());
    // The reading turned away never reached the smoothed reading, which still holds the one before.
    CHECK(s.latest == doctest::Approx(75.0f));
    CHECK(s.sampled_at_us == 0);
}
