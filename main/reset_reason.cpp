#include "reset_reason.hpp"

#include <esp_system.h>

#include <algorithm>
#include <array>

namespace {

struct Reason {
    esp_reset_reason_t reason{ESP_RST_UNKNOWN};
    const char* name{nullptr};
};

// Every reason ESP-IDF 5.5.6 defines. The unknown one must come first because it is the fallback.
constexpr std::array REASONS{
    Reason{.reason = ESP_RST_UNKNOWN, .name = "unknown"},
    Reason{.reason = ESP_RST_POWERON, .name = "powerOn"},
    Reason{.reason = ESP_RST_EXT, .name = "external"},
    Reason{.reason = ESP_RST_SW, .name = "software"},
    Reason{.reason = ESP_RST_PANIC, .name = "panic"},
    Reason{.reason = ESP_RST_INT_WDT, .name = "interruptWdt"},
    Reason{.reason = ESP_RST_TASK_WDT, .name = "taskWdt"},
    Reason{.reason = ESP_RST_WDT, .name = "otherWdt"},
    Reason{.reason = ESP_RST_DEEPSLEEP, .name = "deepSleep"},
    Reason{.reason = ESP_RST_BROWNOUT, .name = "brownout"},
    Reason{.reason = ESP_RST_SDIO, .name = "sdio"},
    Reason{.reason = ESP_RST_USB, .name = "usb"},
    Reason{.reason = ESP_RST_JTAG, .name = "jtag"},
    Reason{.reason = ESP_RST_EFUSE, .name = "efuse"},
    Reason{.reason = ESP_RST_PWR_GLITCH, .name = "powerGlitch"},
    Reason{.reason = ESP_RST_CPU_LOCKUP, .name = "cpuLockup"},
};

}  // namespace

const char* reset_reason::name() {
    const auto* const found{std::ranges::find(REASONS, esp_reset_reason(), &Reason::reason)};
    // A reason a later ESP-IDF adds reads as unknown until it is listed here.
    return found != REASONS.end() ? found->name : REASONS.front().name;
}
