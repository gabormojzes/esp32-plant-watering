#include "hardware/rgb_led.hpp"

#include <esp_log.h>
#include <led_strip.h>
#include <soc/soc_caps.h>

#include <cstdint>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"

#if SOC_RMT_SUPPORTED
#include <led_strip_rmt.h>
#else
#include <led_strip_spi.h>
#endif

namespace {

constexpr char TAG[]{"RGB_LED"};

// The strip API drives a chain. This device lights one pixel.
constexpr std::uint32_t LED_COUNT{1};
// Dim level on the 0..255 scale
constexpr std::uint8_t BRIGHTNESS{32};

#if SOC_RMT_SUPPORTED
// 10 MHz gives 0.1 µs RMT ticks, fine enough for WS2812 pulse timing.
constexpr std::uint32_t RMT_RESOLUTION_HZ{10000000};
#endif

}  // namespace

RgbLed::RgbLed(const Config& config) : config_{config} {}

RgbLed::~RgbLed() {
    release();
}

void RgbLed::release() {
    if (strip_ == nullptr) {
        return;
    }
    // The strip keeps its last color, which a clear turns off before the peripheral is
    // freed. The result needs no checking. A failed clear only leaves the color lit, and
    // the strip is freed either way.
    led_strip_clear(strip_);
    const esp_err_t err{led_strip_del(strip_)};
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Strip release failed: %s", esp_err_to_name(err));
    }
    strip_ = nullptr;
}

void RgbLed::init() {
    const led_strip_config_t strip_config{
        .strip_gpio_num = config_.pin,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
        .timings = {},  // Read only for a custom model. WS2812 brings its own.
        .flags = {},
    };
    const esp_err_t err{claim(strip_config)};
    const bool strip_ready{err == ESP_OK};
    if (!strip_ready) {
        // The LED cannot show this fault, because no color reaches a strip that is
        // down.
        faults::raise(fault_definitions::STATUS_LED_INIT_FAILED);
        ESP_LOGE(TAG, "Strip init failed: %s", esp_err_to_name(err));
        strip_ = nullptr;
        return;
    }
    // The strip keeps its color through a chip reset, and would go on showing the old one.
    const esp_err_t clear_err{led_strip_clear(strip_)};
    if (clear_err != ESP_OK) {
        ESP_LOGW(TAG, "Strip clear failed: %s", esp_err_to_name(clear_err));
    }
}

void RgbLed::set(Color color) {
    const Color scaled{color.scaled(BRIGHTNESS)};
    // Compared after scaling with what the strip last got, which touches the
    // hardware only when the written color changes.
    if (strip_ != nullptr && scaled != color_) {
        write(scaled);
    }
}

void RgbLed::write(Color color) {
    // Recorded only once the write succeeds. A failed write leaves color_ stale,
    // and the next set() retries.
    if (led_strip_set_pixel(strip_, 0, color.red, color.green, color.blue) != ESP_OK ||
        led_strip_refresh(strip_) != ESP_OK) {
        if (!write_failed_) {
            ESP_LOGW(TAG, "Write failed, retrying");
            write_failed_ = true;
        }
        return;
    }
    write_failed_ = false;
    color_ = color;
}

esp_err_t RgbLed::claim(const led_strip_config_t& strip_config) {
#if SOC_RMT_SUPPORTED
    const led_strip_rmt_config_t rmt_config{
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = {},
        .flags = {},
    };
    return led_strip_new_rmt_device(&strip_config, &rmt_config, &strip_);
#else
    // The whole bus goes to the LED, though only its MOSI line carries the
    // signal. One pixel is 9 bytes on the wire, within the 64 a transfer
    // without DMA may carry.
    const led_strip_spi_config_t spi_config{
        .clk_src = SPI_CLK_SRC_DEFAULT,
        .spi_bus = SPI2_HOST,
        .flags = {.with_dma = false},
    };
    return led_strip_new_spi_device(&strip_config, &spi_config, &strip_);
#endif
}
