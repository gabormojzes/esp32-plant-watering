#pragma once

#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

#include "watchdog.hpp"

// Paces target.tick(now_us) at a fixed period on a watchdog-subscribed task,
// either one it spawns (start) or the calling one (run_on_current_task).
template <typename Target>
class PeriodicTask {
public:
    struct Config {
        std::uint32_t period_ms;
        // Used only when spawning; run_on_current_task() ignores these
        const char* name{nullptr};
        std::uint32_t stack{0};
        UBaseType_t priority{0};
    };

    PeriodicTask(const Config& config, Target& target) : config_{config}, target_{target} {}
    // The spawned task points at this object, never at a copy.
    PeriodicTask(const PeriodicTask&) = delete;
    PeriodicTask& operator=(const PeriodicTask&) = delete;

    // Spawns the task. Returns false when it cannot be created, and the target
    // never ticks.
    [[nodiscard]] bool start() {
        if (xTaskCreate(&run, config_.name, config_.stack, this, config_.priority, nullptr) !=
            pdPASS) {
            ESP_LOGE(TAG, "Task %s create failed", config_.name);
            return false;
        }
        return true;
    }

    [[noreturn]] void run_on_current_task() {
        watchdog::subscribe_current_task();
        while (true) {
            // Reset at the top, which means one tick plus one delay must fit in a
            // single watchdog window. The result needs no checking. A reset fails only
            // for a task the watchdog does not watch, which has nothing to feed.
            esp_task_wdt_reset();
            target_.tick(esp_timer_get_time());
            // The delay runs from the end of one tick to the start of the next,
            // making the period a floor on the spacing rather than a cadence.
            vTaskDelay(pdMS_TO_TICKS(config_.period_ms));
        }
    }

private:
    static constexpr char TAG[]{"PERIODIC_TASK"};

    static void run(void* arg) {
        static_cast<PeriodicTask*>(arg)->run_on_current_task();
    }

    Config config_{};
    Target& target_;
};
