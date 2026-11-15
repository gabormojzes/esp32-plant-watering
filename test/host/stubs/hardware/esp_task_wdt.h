#pragma once

// Host stub for the ESP-IDF task watchdog. Nothing watches a host test, and
// every call succeeds.

#include <esp_err.h>
#include <freertos/task.h>

inline esp_err_t esp_task_wdt_add(TaskHandle_t /*task_handle*/) {
    return ESP_OK;
}

inline esp_err_t esp_task_wdt_reset() {
    return ESP_OK;
}
