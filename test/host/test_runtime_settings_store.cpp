#include <doctest.h>
#include <nvs.h>

#include <algorithm>
#include <array>
#include <bit>
#include <optional>
#include <span>
#include <string_view>

#include "fault/fault_definitions.hpp"
#include "fault/fault_manager.hpp"
#include "fault/fault_sink.hpp"
#include "settings_rules.hpp"
#include "shared/constants.hpp"
#include "storage/runtime_settings_store.hpp"

using constants::US_PER_S;

namespace {

constexpr std::uint32_t VERSION{3};
constexpr float TARGET{50.0f};
constexpr float DEADBAND{5.0f};
constexpr std::uint16_t ADC_MIN{1000};
constexpr std::uint16_t ADC_MAX{3000};
constexpr std::int64_t DURATION_US{60 * US_PER_S};
constexpr std::int64_t WAIT_TIME_US{3600 * US_PER_S};
// In every seeded set, a value the defaults lack shows the load landed.
constexpr float STORED_TARGET{42.0f};
constexpr float STORED_DEADBAND{1.5f};
constexpr std::int64_t STORED_DURATION_US{120 * US_PER_S};
constexpr std::int64_t STORED_WAIT_TIME_US{7200 * US_PER_S};
constexpr std::uint32_t STORED_AUTOMATIC{0};
constexpr std::uint32_t STORED_ADC_MIN{1200};
constexpr std::uint32_t STORED_ADC_MAX{2900};

// The store's keys, for seeding and reading the table
constexpr char KEY_VERSION[]{"version"};
constexpr char KEY_SENSOR_TARGET[]{"sensorTarget"};
constexpr char KEY_SENSOR_DEADBAND[]{"sensorDeadband"};
constexpr char KEY_SENSOR_ADC_MIN[]{"sensorAdcMin"};
constexpr char KEY_SENSOR_ADC_MAX[]{"sensorAdcMax"};
constexpr char KEY_PUMP_DURATION_US[]{"pumpDurationUs"};
constexpr char KEY_PUMP_WAIT_TIME_US[]{"pumpWaitTimeUs"};
constexpr char KEY_PUMP_AUTOMATIC[]{"pumpAutomatic"};

// The store's own keys face the same cap here as on the device, where one too
// long is refused rather than stored under a shortened name.
static_assert(
    std::string_view{KEY_VERSION}.size() < NVS_KEY_NAME_MAX_SIZE, "KEY_VERSION is too long for NVS"
);
static_assert(
    std::string_view{KEY_SENSOR_TARGET}.size() < NVS_KEY_NAME_MAX_SIZE,
    "KEY_SENSOR_TARGET is too long for NVS"
);
static_assert(
    std::string_view{KEY_SENSOR_DEADBAND}.size() < NVS_KEY_NAME_MAX_SIZE,
    "KEY_SENSOR_DEADBAND is too long for NVS"
);
static_assert(
    std::string_view{KEY_SENSOR_ADC_MIN}.size() < NVS_KEY_NAME_MAX_SIZE,
    "KEY_SENSOR_ADC_MIN is too long for NVS"
);
static_assert(
    std::string_view{KEY_SENSOR_ADC_MAX}.size() < NVS_KEY_NAME_MAX_SIZE,
    "KEY_SENSOR_ADC_MAX is too long for NVS"
);
static_assert(
    std::string_view{KEY_PUMP_DURATION_US}.size() < NVS_KEY_NAME_MAX_SIZE,
    "KEY_PUMP_DURATION_US is too long for NVS"
);
static_assert(
    std::string_view{KEY_PUMP_WAIT_TIME_US}.size() < NVS_KEY_NAME_MAX_SIZE,
    "KEY_PUMP_WAIT_TIME_US is too long for NVS"
);
static_assert(
    std::string_view{KEY_PUMP_AUTOMATIC}.size() < NVS_KEY_NAME_MAX_SIZE,
    "KEY_PUMP_AUTOMATIC is too long for NVS"
);

// Tracks which codes are active.
struct SinkStub : FaultSink {
    void accept(const Fault& fault) override {
        if (!holds(fault.code) && count < active.size()) {
            active[count++] = fault.code;
        }
    }

    void clear(const char* code) override {
        for (std::size_t i{0}; i < count; ++i) {
            if (std::string_view{active[i]} == code) {
                active[i] = active[--count];
                return;
            }
        }
    }

    bool holds(const char* code) const {
        return std::ranges::any_of(std::span{active}.first(count), [code](const char* held) {
            return std::string_view{held} == code;
        });
    }

    std::array<const char*, 8> active{};
    std::size_t count{0};
};

SinkStub sink{};
const std::array<FaultSink*, 1> SINKS{&sink};

// A store on fresh defaults over an empty table. Every case starts from here.
struct Fixture {
    Fixture() {
        nvs_stub_reset();
        sink = {};
        FaultManager::instance().install(SINKS);
    }

    RuntimeSettings runtime_settings{{
        .sensor_target = TARGET,
        .sensor_deadband = DEADBAND,
        .sensor_calibration = {.adc_min = ADC_MIN, .adc_max = ADC_MAX},
        .pump_duration_us = DURATION_US,
        .pump_wait_time_us = WAIT_TIME_US,
        .pump_automatic = true,
    }};
    RuntimeSettingsStore store{runtime_settings, {.version = VERSION}};
};

// One stored set. An empty field leaves its key unwritten.
struct StoredSet {
    std::optional<std::uint32_t> version{VERSION};
    std::optional<float> target{STORED_TARGET};
    std::optional<float> deadband{STORED_DEADBAND};
    std::optional<std::uint32_t> adc_min{STORED_ADC_MIN};
    std::optional<std::uint32_t> adc_max{STORED_ADC_MAX};
    std::optional<std::int64_t> duration_us{STORED_DURATION_US};
    std::optional<std::int64_t> wait_time_us{STORED_WAIT_TIME_US};
    std::optional<std::uint32_t> automatic{STORED_AUTOMATIC};
};

// Floats are held as their bit pattern, the way the manager writes them.
void seed_float(nvs_handle_t handle, const char* key, float value) {
    nvs_set_u32(handle, key, std::bit_cast<std::uint32_t>(value));
}

void seed_stored_set(const StoredSet& set) {
    nvs_handle_t handle{0};
    nvs_open("runtime", NVS_READWRITE, &handle);
    if (set.version) {
        nvs_set_u32(handle, KEY_VERSION, *set.version);
    }
    if (set.target) {
        seed_float(handle, KEY_SENSOR_TARGET, *set.target);
    }
    if (set.deadband) {
        seed_float(handle, KEY_SENSOR_DEADBAND, *set.deadband);
    }
    if (set.adc_min) {
        nvs_set_u32(handle, KEY_SENSOR_ADC_MIN, *set.adc_min);
    }
    if (set.adc_max) {
        nvs_set_u32(handle, KEY_SENSOR_ADC_MAX, *set.adc_max);
    }
    if (set.duration_us) {
        nvs_set_i64(handle, KEY_PUMP_DURATION_US, *set.duration_us);
    }
    if (set.wait_time_us) {
        nvs_set_i64(handle, KEY_PUMP_WAIT_TIME_US, *set.wait_time_us);
    }
    if (set.automatic) {
        nvs_set_u32(handle, KEY_PUMP_AUTOMATIC, *set.automatic);
    }
}

}  // namespace

TEST_CASE("a fresh device keeps its boot defaults and reports nothing wrong") {
    Fixture fixture{};

    fixture.store.init();

    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(TARGET));
    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    CHECK(fixture.runtime_settings.pump_automatic());
    // A missing value is not a fault. Only a stored value that cannot be read
    // is.
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_STORAGE_UNAVAILABLE.code));
}

TEST_CASE("a stored set under the running version is loaded over the defaults") {
    Fixture fixture{};
    seed_stored_set({});

    fixture.store.init();

    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(STORED_TARGET));
    CHECK(fixture.runtime_settings.sensor_deadband() == doctest::Approx(STORED_DEADBAND));
    CHECK(
        fixture.runtime_settings.sensor_calibration() ==
        SensorCalibration{.adc_min = STORED_ADC_MIN, .adc_max = STORED_ADC_MAX}
    );
    CHECK(fixture.runtime_settings.pump_duration() == STORED_DURATION_US);
    CHECK(fixture.runtime_settings.pump_wait_time() == STORED_WAIT_TIME_US);
    CHECK_FALSE(fixture.runtime_settings.pump_automatic());
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a stored set under another version is dropped for the defaults") {
    Fixture fixture{};
    seed_stored_set({.version = VERSION + 1});

    fixture.store.init();

    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(TARGET));
    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    CHECK(nvs_stub_erase_alls == 1);
    // The set went because it was stale, not because anything failed to read.
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a stored duration out of bounds is refused and its default stands") {
    Fixture fixture{};
    // Past MAX_PUMP_DURATION_US, as flash garbage or an older build could hold.
    seed_stored_set({.duration_us = 999000 * US_PER_S});

    fixture.store.init();

    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    // The rest of the set landed, which shows the value was refused rather than
    // left unloaded.
    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(STORED_TARGET));
    // Refusing a value that was read is not the same as failing to read it.
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a stored wait time out of bounds is refused and its default stands") {
    Fixture fixture{};
    seed_stored_set({.wait_time_us = settings_rules::MAX_PUMP_WAIT_TIME_US + 1});

    fixture.store.init();

    CHECK(fixture.runtime_settings.pump_wait_time() == WAIT_TIME_US);
    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(STORED_TARGET));
    CHECK(fixture.runtime_settings.pump_duration() == STORED_DURATION_US);
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a stored automatic other than 0 or 1 is refused and its default stands") {
    Fixture fixture{};
    seed_stored_set({.automatic = 2});

    fixture.store.init();

    CHECK(fixture.runtime_settings.pump_automatic());
    CHECK(fixture.runtime_settings.pump_duration() == STORED_DURATION_US);
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a set stored before automatic existed keeps its values and the default") {
    Fixture fixture{};
    seed_stored_set({.automatic = std::nullopt});

    fixture.store.init();

    CHECK(fixture.runtime_settings.pump_automatic());
    CHECK(fixture.runtime_settings.pump_duration() == STORED_DURATION_US);
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a stored deadband without its target is set against the target in use") {
    Fixture fixture{};
    seed_stored_set({.target = std::nullopt, .deadband = 10.0f});

    fixture.store.init();

    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(TARGET));
    CHECK(fixture.runtime_settings.sensor_deadband() == doctest::Approx(10.0f));
    // A missing value is not a fault.
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a stored target without its deadband is set against the deadband in use") {
    Fixture fixture{};
    seed_stored_set({.target = 40.0f, .deadband = std::nullopt});

    fixture.store.init();

    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(40.0f));
    CHECK(fixture.runtime_settings.sensor_deadband() == doctest::Approx(DEADBAND));
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a stored band out of range as a pair is refused whole") {
    Fixture fixture{};
    // Either half fits against the other's default. Together the band's top
    // edge passes 100.
    seed_stored_set({.target = 95.0f, .deadband = 8.0f});

    fixture.store.init();

    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(TARGET));
    CHECK(fixture.runtime_settings.sensor_deadband() == doctest::Approx(DEADBAND));
    CHECK(fixture.runtime_settings.pump_duration() == STORED_DURATION_US);
    CHECK(fixture.runtime_settings.pump_wait_time() == STORED_WAIT_TIME_US);
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a set stored before the calibration existed keeps its values and the default") {
    Fixture fixture{};
    seed_stored_set({.adc_min = std::nullopt, .adc_max = std::nullopt});

    fixture.store.init();

    CHECK(
        fixture.runtime_settings.sensor_calibration() ==
        SensorCalibration{.adc_min = ADC_MIN, .adc_max = ADC_MAX}
    );
    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(STORED_TARGET));
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a stored adc_max without its adc_min is set against the adc_min in use") {
    Fixture fixture{};
    seed_stored_set({.adc_min = std::nullopt, .adc_max = 2500});

    fixture.store.init();

    CHECK(
        fixture.runtime_settings.sensor_calibration() ==
        SensorCalibration{.adc_min = ADC_MIN, .adc_max = 2500}
    );
}

TEST_CASE("a stored calibration out of range as a pair is refused whole") {
    Fixture fixture{};
    // Either end fits against the other's default. Together they cross.
    seed_stored_set({.adc_min = 2500, .adc_max = 2000});

    fixture.store.init();

    CHECK(
        fixture.runtime_settings.sensor_calibration() ==
        SensorCalibration{.adc_min = ADC_MIN, .adc_max = ADC_MAX}
    );
    CHECK(fixture.runtime_settings.pump_duration() == STORED_DURATION_US);
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a stored count past 16 bits is refused, never truncated into range") {
    Fixture fixture{};
    // 65536 + 2500 would truncate to 2500, an adc_max in range.
    seed_stored_set({.adc_min = std::nullopt, .adc_max = 65536 + 2500});

    fixture.store.init();

    CHECK(
        fixture.runtime_settings.sensor_calibration() ==
        SensorCalibration{.adc_min = ADC_MIN, .adc_max = ADC_MAX}
    );
}

TEST_CASE("a value that cannot be read raises WSET02 and leaves its default") {
    Fixture fixture{};
    seed_stored_set({});
    nvs_stub_read_error = ESP_ERR_NVS_INVALID_HANDLE;
    nvs_stub_read_error_key = KEY_PUMP_DURATION_US;

    fixture.store.init();

    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    CHECK(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
    // The keys that did answer still landed.
    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(STORED_TARGET));
}

TEST_CASE("an unreadable version stamp drops the set and is reported") {
    Fixture fixture{};
    seed_stored_set({});
    nvs_stub_read_error = ESP_ERR_NVS_INVALID_HANDLE;
    nvs_stub_read_error_key = KEY_VERSION;

    fixture.store.init();

    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(TARGET));
    CHECK(nvs_stub_erase_alls == 1);
    CHECK(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a partition with no free pages is erased and mounted again") {
    Fixture fixture{};
    nvs_stub_flash_init_result = ESP_ERR_NVS_NO_FREE_PAGES;
    nvs_stub_flash_init_recovers = true;

    fixture.store.init();

    CHECK(nvs_stub_flash_erases == 1);
    CHECK(nvs_stub_flash_inits == 2);
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_STORAGE_UNAVAILABLE.code));
}

TEST_CASE("a partition that will not mount even after erasing is reported") {
    Fixture fixture{};
    nvs_stub_flash_init_result = ESP_ERR_NVS_NO_FREE_PAGES;

    fixture.store.init();

    // Erased once, then refused again, after the recovery had its turn.
    CHECK(nvs_stub_flash_erases == 1);
    CHECK(nvs_stub_flash_inits == 2);
    CHECK(sink.holds(fault_definitions::SETTINGS_STORAGE_UNAVAILABLE.code));
}

TEST_CASE("a partition in an NVS format this build cannot read is erased and mounted again") {
    Fixture fixture{};
    nvs_stub_flash_init_result = ESP_ERR_NVS_NEW_VERSION_FOUND;
    nvs_stub_flash_init_recovers = true;

    fixture.store.init();

    CHECK(nvs_stub_flash_erases == 1);
    CHECK(nvs_stub_flash_inits == 2);
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_STORAGE_UNAVAILABLE.code));
}

TEST_CASE("a partition that cannot be erased is reported and not mounted again") {
    Fixture fixture{};
    nvs_stub_flash_init_result = ESP_ERR_NVS_NO_FREE_PAGES;
    nvs_stub_flash_init_recovers = true;
    nvs_stub_flash_erase_result = ESP_ERR_NOT_FOUND;

    fixture.store.init();

    // The mount that would recover is never tried.
    CHECK(nvs_stub_flash_inits == 1);
    CHECK(sink.holds(fault_definitions::SETTINGS_STORAGE_UNAVAILABLE.code));
}

TEST_CASE("a namespace that will not open is reported and leaves the defaults") {
    Fixture fixture{};
    nvs_stub_open_result = ESP_ERR_NVS_INVALID_HANDLE;

    fixture.store.init();

    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    CHECK(sink.holds(fault_definitions::SETTINGS_STORAGE_UNAVAILABLE.code));
    // Nothing was read, and nothing failed to read.
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a change marked without a namespace to hold it is not written either") {
    Fixture fixture{};
    nvs_stub_open_result = ESP_ERR_NVS_INVALID_HANDLE;
    fixture.store.init();
    REQUIRE(sink.holds(fault_definitions::SETTINGS_STORAGE_UNAVAILABLE.code));

    fixture.store.mark_dirty();
    fixture.store.save_if_dirty();

    CHECK(nvs_stub_commits == 0);
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_WRITE_FAILED.code));
}

TEST_CASE("a marked change is written and committed, an unmarked one is not") {
    Fixture fixture{};
    fixture.store.init();
    const int committed_at_load{nvs_stub_commits};

    fixture.store.save_if_dirty();
    CHECK(nvs_stub_commits == committed_at_load);

    REQUIRE(fixture.runtime_settings.set_pump_duration(90 * US_PER_S));
    fixture.store.mark_dirty();
    fixture.store.save_if_dirty();

    CHECK(nvs_stub_commits == committed_at_load + 1);
    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_WRITE_FAILED.code));
    const NvsEntryStub* stored_duration{nvs_stub_find(KEY_PUMP_DURATION_US)};
    REQUIRE(stored_duration != nullptr);
    CHECK(stored_duration->wide_value == 90 * US_PER_S);
    const NvsEntryStub* stored_automatic{nvs_stub_find(KEY_PUMP_AUTOMATIC)};
    REQUIRE(stored_automatic != nullptr);
    CHECK(stored_automatic->value == 1);
    const NvsEntryStub* stored_adc_min{nvs_stub_find(KEY_SENSOR_ADC_MIN)};
    REQUIRE(stored_adc_min != nullptr);
    CHECK(stored_adc_min->value == ADC_MIN);
    const NvsEntryStub* stored_adc_max{nvs_stub_find(KEY_SENSOR_ADC_MAX)};
    REQUIRE(stored_adc_max != nullptr);
    CHECK(stored_adc_max->value == ADC_MAX);
    const NvsEntryStub* stored_version{nvs_stub_find(KEY_VERSION)};
    REQUIRE(stored_version != nullptr);
    CHECK(stored_version->value == VERSION);
}

TEST_CASE("automatic watering turned off is written as 0") {
    Fixture fixture{};
    fixture.store.init();

    fixture.runtime_settings.set_pump_automatic(false);
    fixture.store.mark_dirty();
    fixture.store.save_if_dirty();

    const NvsEntryStub* stored_automatic{nvs_stub_find(KEY_PUMP_AUTOMATIC)};
    REQUIRE(stored_automatic != nullptr);
    CHECK(stored_automatic->value == 0);
}

TEST_CASE("a write that fails leaves the version unstamped and raises WSET03") {
    Fixture fixture{};
    fixture.store.init();
    const int committed_at_load{nvs_stub_commits};

    fixture.store.mark_dirty();
    nvs_stub_write_result = ESP_ERR_NVS_NO_FREE_PAGES;
    fixture.store.save_if_dirty();

    // The store is fresh and holds no stamp. A save that fails stops before the
    // version, and the next boot drops whatever part of the set landed.
    CHECK(nvs_stub_commits == committed_at_load);
    CHECK(nvs_stub_find(KEY_VERSION) == nullptr);
    CHECK(sink.holds(fault_definitions::SETTINGS_WRITE_FAILED.code));
}

TEST_CASE("a commit that fails raises WSET03 on a set that was written") {
    Fixture fixture{};
    fixture.store.init();

    fixture.store.mark_dirty();
    nvs_stub_commit_result = ESP_ERR_NVS_NO_FREE_PAGES;
    fixture.store.save_if_dirty();

    CHECK(nvs_stub_find(KEY_VERSION) != nullptr);
    CHECK(sink.holds(fault_definitions::SETTINGS_WRITE_FAILED.code));
}

TEST_CASE("a save that lands clears a write fault the one before it raised") {
    Fixture fixture{};
    fixture.store.init();

    fixture.store.mark_dirty();
    nvs_stub_write_result = ESP_ERR_NVS_NO_FREE_PAGES;
    fixture.store.save_if_dirty();
    REQUIRE(sink.holds(fault_definitions::SETTINGS_WRITE_FAILED.code));

    nvs_stub_write_result = ESP_OK;
    fixture.store.mark_dirty();
    fixture.store.save_if_dirty();

    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_WRITE_FAILED.code));
}

TEST_CASE("a save that lands clears a read fault the load raised") {
    Fixture fixture{};
    seed_stored_set({});
    nvs_stub_read_error = ESP_ERR_NVS_INVALID_HANDLE;
    nvs_stub_read_error_key = KEY_PUMP_DURATION_US;
    fixture.store.init();
    REQUIRE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));

    fixture.store.mark_dirty();
    fixture.store.save_if_dirty();

    CHECK_FALSE(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}

TEST_CASE("a save that fails leaves a read fault the load raised") {
    Fixture fixture{};
    seed_stored_set({});
    nvs_stub_read_error = ESP_ERR_NVS_INVALID_HANDLE;
    nvs_stub_read_error_key = KEY_PUMP_DURATION_US;
    fixture.store.init();

    fixture.store.mark_dirty();
    nvs_stub_write_result = ESP_ERR_NVS_NO_FREE_PAGES;
    fixture.store.save_if_dirty();

    CHECK(sink.holds(fault_definitions::SETTINGS_READ_FAILED.code));
}
