#include "hardware/adc_reader.hpp"

#include <esp_log.h>

#include <expected>

namespace {

constexpr char TAG[]{"ADC_READER"};

}  // namespace

AdcReader::AdcReader(const Config& config) : config_{config} {}

AdcReader::~AdcReader() {
    release();
}

void AdcReader::release() {
    if (!is_ready()) {
        return;
    }
    // The result needs no checking. A delete fails only for a null unit or one
    // not in use, and neither applies.
    adc_oneshot_del_unit(unit_);
    unit_ = nullptr;
}

bool AdcReader::init() {
    // Claiming a unit twice fails while the live one keeps working, which
    // would report a down reader that is not down.
    if (is_ready()) {
        return true;
    }

    if (!init_unit()) {
        return false;
    }

    if (!init_channel()) {
        // Release the unit claimed above, letting the next init() start from
        // nothing.
        release();
        return false;
    }

    return true;
}

bool AdcReader::init_unit() {
    const adc_oneshot_unit_init_cfg_t unit_config{
        .unit_id = config_.unit,
        // The driver reads 0 as its default clock, which no name spells on every chip.
        // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
        .clk_src = {},
        .ulp_mode = {},
    };
    const esp_err_t err{adc_oneshot_new_unit(&unit_config, &unit_)};
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool AdcReader::init_channel() {
    const adc_oneshot_chan_cfg_t channel_config{
        .atten = ADC_ATTEN_DB_12,  // Widest input range the chip offers
        // The chip's native width. Raw full scale varies per chip.
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    const esp_err_t err{adc_oneshot_config_channel(unit_, config_.channel, &channel_config)};
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool AdcReader::is_ready() const {
    return unit_ != nullptr;
}

std::expected<std::uint32_t, esp_err_t> AdcReader::read() {
    int raw{0};
    const esp_err_t err{adc_oneshot_read(unit_, config_.channel, &raw)};
    if (err != ESP_OK) {
        return std::unexpected(err);
    }
    return static_cast<std::uint32_t>(raw);
}
