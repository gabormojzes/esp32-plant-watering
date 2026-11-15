#include "hardware/sensor.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <expected>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"

namespace {

constexpr char TAG[]{"SENSOR"};

// Only a delay because CONFIG_FREERTOS_HZ is 1000. At ESP-IDF's default 100 Hz
// this rounds to vTaskDelay(0), and the burst stops being spaced.
constexpr std::uint32_t SAMPLE_DELAY_MS{1};

}  // namespace

Sensor::Sensor(AdcReader& adc_reader) : adc_reader_{adc_reader} {}

bool Sensor::init() {
    const bool adc_ready{adc_reader_.init()};
    if (!adc_ready) {
        // The reader reports the cause. This fault reports that sampling is down.
        faults::raise(fault_definitions::SENSOR_ADC_INIT_FAILED);
    } else {
        faults::clear(fault_definitions::SENSOR_ADC_INIT_FAILED);
    }
    return adc_ready;
}

bool Sensor::is_ready() const {
    return adc_reader_.is_ready();
}

std::optional<std::uint32_t> Sensor::read() {
    return finalize_burst(perform_burst_read());
}

Sensor::BurstReads Sensor::perform_burst_read() {
    BurstReads burst{};
    for (std::uint8_t i{0}; i < SAMPLES; i++) {
        const std::expected<std::uint32_t, esp_err_t> raw{adc_reader_.read()};
        if (raw) {
            burst.sum += *raw;
            burst.success_count++;
        } else {
            burst.last_error = raw.error();
        }
        if (i < SAMPLES - 1) {
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_DELAY_MS));
        }
    }
    return burst;
}

std::optional<std::uint32_t> Sensor::finalize_burst(const BurstReads& burst) {
    const bool all_reads_failed{burst.success_count == 0};
    if (all_reads_failed) {
        faults::raise(fault_definitions::SENSOR_READS_FAILED);
        ESP_LOGE(
            TAG,
            "All %u ADC reads failed: %s",
            static_cast<unsigned>(SAMPLES),
            esp_err_to_name(burst.last_error)
        );
        return std::nullopt;
    }
    faults::clear(fault_definitions::SENSOR_READS_FAILED);
    if (burst.success_count < SAMPLES) {
        ESP_LOGW(
            TAG,
            "%u of %u ADC reads failed: %s",
            static_cast<unsigned>(SAMPLES - burst.success_count),
            static_cast<unsigned>(SAMPLES),
            esp_err_to_name(burst.last_error)
        );
    }
    return burst.sum / burst.success_count;
}
