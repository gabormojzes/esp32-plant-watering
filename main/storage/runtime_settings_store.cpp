#include "storage/runtime_settings_store.hpp"

#include <esp_err.h>
#include <esp_log.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <tuple>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"

namespace {

constexpr char TAG[]{"RUNTIME_SETTINGS_STORE"};

// NVS refuses a name longer than 15 characters rather than shortening it
constexpr char NAMESPACE[]{"runtime"};
constexpr char KEY_VERSION[]{"version"};
constexpr char KEY_SENSOR_TARGET[]{"sensorTarget"};
constexpr char KEY_SENSOR_DEADBAND[]{"sensorDeadband"};
constexpr char KEY_SENSOR_ADC_MIN[]{"sensorAdcMin"};
constexpr char KEY_SENSOR_ADC_MAX[]{"sensorAdcMax"};
constexpr char KEY_PUMP_DURATION_US[]{"pumpDurationUs"};
constexpr char KEY_PUMP_WAIT_TIME_US[]{"pumpWaitTimeUs"};
constexpr char KEY_PUMP_AUTOMATIC[]{"pumpAutomatic"};

// A missing key is normal on a fresh device or after a version bump, and stays
// silent. Any other error means a stored value could not be read, and the
// default standing in for it is flagged.
template <typename T>
[[nodiscard]] std::optional<T> stored(
    const std::expected<T, esp_err_t>& read, const char* key, bool& unreadable
) {
    if (read.has_value()) {
        return *read;
    }
    if (read.error() != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Stored %s unreadable: %s", key, esp_err_to_name(read.error()));
        unreadable = true;
    }
    return std::nullopt;
}

}  // namespace

RuntimeSettingsStore::RuntimeSettingsStore(RuntimeSettings& runtime_settings, const Config& config)
    : runtime_settings_{runtime_settings}, config_{config} {}

void RuntimeSettingsStore::init() {
    const bool storage_available{NvsManager::init_flash() && nvs_.open(NAMESPACE)};
    if (!storage_available) {
        // The values in use still apply. They will not survive a reboot.
        faults::raise(fault_definitions::SETTINGS_STORAGE_UNAVAILABLE);
        ESP_LOGE(TAG, "Storage unavailable, changes will not persist");
        return;
    }
    load();
}

void RuntimeSettingsStore::load() const {
    bool unreadable{false};

    if (stored(nvs_.get_u32(KEY_VERSION), KEY_VERSION, unreadable) == config_.version) {
        // Through the setters, which bound corrupt flash values like any other
        // input. The band is set as a pair, because each half judged against the
        // other's default could refuse a pair that is valid together.
        const std::optional<float> sensor_target{
            stored(nvs_.get_float(KEY_SENSOR_TARGET), KEY_SENSOR_TARGET, unreadable)
        };
        const std::optional<float> sensor_deadband{
            stored(nvs_.get_float(KEY_SENSOR_DEADBAND), KEY_SENSOR_DEADBAND, unreadable)
        };
        if (sensor_target || sensor_deadband) {
            if (!runtime_settings_.set_sensor_band(
                    sensor_target.value_or(runtime_settings_.sensor_target()),
                    sensor_deadband.value_or(runtime_settings_.sensor_deadband())
                )) {
                ESP_LOGW(
                    TAG,
                    "Stored %s/%s rejected, keeping defaults",
                    KEY_SENSOR_TARGET,
                    KEY_SENSOR_DEADBAND
                );
            }
        }
        load_sensor_calibration(unreadable);
        if (const std::optional<std::int64_t> pump_duration_us{
                stored(nvs_.get_i64(KEY_PUMP_DURATION_US), KEY_PUMP_DURATION_US, unreadable)
            }) {
            if (!runtime_settings_.set_pump_duration(*pump_duration_us)) {
                ESP_LOGW(TAG, "Stored %s rejected, keeping default", KEY_PUMP_DURATION_US);
            }
        }
        if (const std::optional<std::int64_t> pump_wait_time_us{
                stored(nvs_.get_i64(KEY_PUMP_WAIT_TIME_US), KEY_PUMP_WAIT_TIME_US, unreadable)
            }) {
            if (!runtime_settings_.set_pump_wait_time(*pump_wait_time_us)) {
                ESP_LOGW(TAG, "Stored %s rejected, keeping default", KEY_PUMP_WAIT_TIME_US);
            }
        }
        // Stored as 0 or 1, and any other value is refused like one out of
        // bounds.
        if (const std::optional<std::uint32_t> pump_automatic{
                stored(nvs_.get_u32(KEY_PUMP_AUTOMATIC), KEY_PUMP_AUTOMATIC, unreadable)
            }) {
            if (*pump_automatic <= 1) {
                runtime_settings_.set_pump_automatic(*pump_automatic == 1);
            } else {
                ESP_LOGW(TAG, "Stored %s rejected, keeping default", KEY_PUMP_AUTOMATIC);
            }
        }
    } else {
        // With no set, or one from another version, erase it; the next save
        // writes this version. An unreadable stamp also lands here, dropping a
        // set whose version cannot be confirmed.
        ESP_LOGI(
            TAG,
            "No stored set for version %u, using defaults",
            static_cast<unsigned>(config_.version)
        );
        // The results need no checking. A set the erase leaves behind fails the
        // version check again on the next boot, and the manager logs the failure
        // itself.
        std::ignore = nvs_.erase_all();
        std::ignore = nvs_.commit();
    }

    // One fault for the whole load, because a value that exists but cannot be
    // read is not the fresh-device case logged above.
    if (unreadable) {
        faults::raise(fault_definitions::SETTINGS_READ_FAILED);
    }
}

// Set as a pair, for the reason load() gives for the band.
void RuntimeSettingsStore::load_sensor_calibration(bool& unreadable) const {
    // A count past 16 bits is refused like one out of bounds, never truncated into
    // one in bounds.
    const std::optional<std::uint32_t> stored_adc_min{
        stored(nvs_.get_u32(KEY_SENSOR_ADC_MIN), KEY_SENSOR_ADC_MIN, unreadable)
    };
    const std::optional<std::uint32_t> stored_adc_max{
        stored(nvs_.get_u32(KEY_SENSOR_ADC_MAX), KEY_SENSOR_ADC_MAX, unreadable)
    };
    if (stored_adc_min || stored_adc_max) {
        const SensorCalibration in_use{runtime_settings_.sensor_calibration()};
        const std::uint32_t adc_min{stored_adc_min.value_or(in_use.adc_min)};
        const std::uint32_t adc_max{stored_adc_max.value_or(in_use.adc_max)};
        const bool counts_fit{
            std::max(adc_min, adc_max) <= std::numeric_limits<std::uint16_t>::max()
        };
        const bool landed{
            counts_fit && runtime_settings_.set_sensor_calibration({
                              .adc_min = static_cast<std::uint16_t>(adc_min),
                              .adc_max = static_cast<std::uint16_t>(adc_max),
                          })
        };
        if (!landed) {
            ESP_LOGW(
                TAG,
                "Stored %s/%s rejected, keeping defaults",
                KEY_SENSOR_ADC_MIN,
                KEY_SENSOR_ADC_MAX
            );
        }
    }
}

void RuntimeSettingsStore::mark_dirty() {
    dirty_ = true;
}

void RuntimeSettingsStore::save_if_dirty() {
    if (!dirty_) {
        return;
    }
    if (nvs_.is_open()) {
        save();
    }
    // Cleared even when nothing was written, since a retry would only fail again.
    dirty_ = false;
}

void RuntimeSettingsStore::save() const {
    // The version goes last. Before the first stamp, a save cut short leaves no
    // version and is dropped on the next boot. Once stamped, a save cut short
    // reads back as a mix of two generations. That is harmless, because load()
    // bounds every value through the setters, the band and the calibration each
    // as a pair.
    const bool written{
        nvs_.set_float(KEY_SENSOR_TARGET, runtime_settings_.sensor_target()) &&
        nvs_.set_float(KEY_SENSOR_DEADBAND, runtime_settings_.sensor_deadband()) &&
        nvs_.set_u32(KEY_SENSOR_ADC_MIN, runtime_settings_.sensor_calibration().adc_min) &&
        nvs_.set_u32(KEY_SENSOR_ADC_MAX, runtime_settings_.sensor_calibration().adc_max) &&
        nvs_.set_i64(KEY_PUMP_DURATION_US, runtime_settings_.pump_duration()) &&
        nvs_.set_i64(KEY_PUMP_WAIT_TIME_US, runtime_settings_.pump_wait_time()) &&
        nvs_.set_u32(KEY_PUMP_AUTOMATIC, runtime_settings_.pump_automatic() ? 1 : 0) &&
        nvs_.set_u32(KEY_VERSION, config_.version) && nvs_.commit()
    };
    if (written) {
        faults::clear(fault_definitions::SETTINGS_WRITE_FAILED);
        // Every stored value was just rewritten, and none is left unreadable.
        faults::clear(fault_definitions::SETTINGS_READ_FAILED);
    } else {
        faults::raise(fault_definitions::SETTINGS_WRITE_FAILED);
    }
}
