#include "watchdog.hpp"

#include <esp_log.h>
#include <esp_task_wdt.h>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"

namespace {

constexpr char TAG[]{"WATCHDOG"};

}  // namespace

void watchdog::subscribe_current_task() {
    // The task watchdog is initialized from sdkconfig (CONFIG_ESP_TASK_WDT_INIT)
    const esp_err_t err{esp_task_wdt_add(nullptr)};
    if (err == ESP_OK) {
        return;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Task watchdog not initialized, check CONFIG_ESP_TASK_WDT_INIT");
    } else {
        ESP_LOGE(TAG, "esp_task_wdt_add failed: %s", esp_err_to_name(err));
    }
    // The task then runs unwatched, and a hang in it is never reset. Never cleared,
    // because every task shares this code.
    faults::raise(fault_definitions::WATCHDOG_SUBSCRIBE_FAILED);
}
