#pragma once

#include <esp_adc/adc_oneshot.h>

#include <cstdint>
#include <expected>

// Not thread-safe; every member belongs to the single task that drives the reader.
class AdcReader {
public:
    struct Config {
        adc_unit_t unit;
        adc_channel_t channel;
    };

    explicit AdcReader(const Config& config);
    // Releases the ADC unit.
    ~AdcReader();
    // A copy would release the one claimed unit twice.
    AdcReader(const AdcReader&) = delete;
    AdcReader& operator=(const AdcReader&) = delete;

    // Claims the unit and the channel. Returns false when either cannot be
    // claimed, leaving the reader down until a later init() succeeds. Returns
    // true when already up.
    [[nodiscard]] bool init();
    bool is_ready() const;
    [[nodiscard]] std::expected<std::uint32_t, esp_err_t> read();

private:
    // Releases whatever is claimed. Safe when nothing is.
    void release();
    [[nodiscard]] bool init_unit();
    [[nodiscard]] bool init_channel();

    Config config_{};
    adc_oneshot_unit_handle_t unit_{nullptr};
};
