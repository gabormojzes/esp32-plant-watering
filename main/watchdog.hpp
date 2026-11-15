#pragma once

namespace watchdog {

// Subscribes the calling task to the task watchdog. The caller must then feed
// it with esp_task_wdt_reset().
void subscribe_current_task();

}  // namespace watchdog
