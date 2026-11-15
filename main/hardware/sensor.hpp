#pragma once

#include <cstdint>
#include <optional>

#include "hardware/adc_reader.hpp"

// The device's moisture sensor, read as a raw ADC count. Not thread-safe; one
// task owns it and makes every call.
class Sensor {
public:
    explicit Sensor(AdcReader& adc_reader);
    // Brings the sensor up. Returns false when the ADC cannot be claimed, and
    // true when already up.
    [[nodiscard]] bool init();
    bool is_ready() const;
    // Reads one burst of samples and returns their average, or nullopt when no
    // read succeeded. Blocks for the length of the burst.
    [[nodiscard]] std::optional<std::uint32_t> read();

private:
    struct BurstReads {
        // Samples are under 16 bits, and SAMPLES of them cannot overflow the sum.
        std::uint32_t sum{0};
        std::uint8_t success_count{0};
        // Error from the last failed read. One is kept for the whole burst, whose
        // failed reads share a cause.
        esp_err_t last_error{ESP_OK};
    };

    // A spaced burst averages out the capacitive sensor's oscillator ripple
    static constexpr std::uint8_t SAMPLES{64};

    [[nodiscard]] static std::optional<std::uint32_t> finalize_burst(const BurstReads& burst);

    [[nodiscard]] BurstReads perform_burst_read();

    AdcReader& adc_reader_;
};
