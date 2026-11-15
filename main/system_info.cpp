#include "system_info.hpp"

#include <esp_app_desc.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <sdkconfig.h>
#include <soc/soc_caps.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>

#include "shared/math_utils.hpp"

#if SOC_TEMP_SENSOR_SUPPORTED
#include <driver/temperature_sensor.h>
#endif

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <span>
#endif

namespace {

constexpr char TAG[]{"SYSTEM_INFO"};

// Byte-addressable internal RAM. MALLOC_CAP_INTERNAL alone would add the
// original ESP32's IRAM heap, which takes 32-bit accesses only, and
// MALLOC_CAP_8BIT alone would add PSRAM.
constexpr std::uint32_t INTERNAL_CAPS{MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT};

// ESP-IDF keeps the revision as major * 100 + minor
constexpr unsigned MINORS_PER_MAJOR{100};

// The widest revision the field holds, 65535, prints as v655.35
constexpr std::size_t REVISION_BUFFER_SIZE{std::string_view{"v655.35"}.size() + 1};
static_assert(
    std::numeric_limits<decltype(esp_chip_info_t::revision)>::max() == 65535,
    "REVISION_BUFFER_SIZE no longer matches the widest revision"
);

#if SOC_TEMP_SENSOR_SUPPORTED

// The span the sensor reads to within 1 °C, its finest. Outside it the driver
// moves to a coarser range by itself.
constexpr int MIN_FINEST_RANGE_C{-10};
constexpr int MAX_FINEST_RANGE_C{80};

// Null when the sensor cannot start.
[[nodiscard]] temperature_sensor_handle_t start_temperature_sensor() {
    const temperature_sensor_config_t config{
        .range_min = MIN_FINEST_RANGE_C,
        .range_max = MAX_FINEST_RANGE_C,
        .clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT,
        .flags = {},
    };
    temperature_sensor_handle_t temperature_sensor{nullptr};
    const esp_err_t install_err{temperature_sensor_install(&config, &temperature_sensor)};
    if (install_err != ESP_OK) {
        ESP_LOGE(TAG, "temperature_sensor_install failed: %s", esp_err_to_name(install_err));
        return nullptr;
    }
    const esp_err_t enable_err{temperature_sensor_enable(temperature_sensor)};
    if (enable_err != ESP_OK) {
        ESP_LOGE(TAG, "temperature_sensor_enable failed: %s", esp_err_to_name(enable_err));
        return nullptr;
    }
    return temperature_sensor;
}

#endif  // SOC_TEMP_SENSOR_SUPPORTED

HeapUsage heap_usage(std::uint32_t caps) {
    // One walk gives every free figure. The largest block needs a walk anyway, and
    // the block count has no getter.
    multi_heap_info_t heap{};
    heap_caps_get_info(&heap, caps);
    return {
        .total = heap_caps_get_total_size(caps),
        .free = heap.total_free_bytes,
        .min_free = heap.minimum_free_bytes,
        .largest_free_block = heap.largest_free_block,
        .free_blocks = heap.free_blocks,
    };
}

#if CONFIG_FREERTOS_USE_TRACE_FACILITY

// Most tasks one snapshot holds. uxTaskGetSystemState is all-or-nothing, and
// reports none when there are more.
constexpr UBaseType_t MAX_TASKS{24};

void log_task_stacks() {
    // Static, because the snapshot must not consume the stack it reports
    static std::array<TaskStatus_t, MAX_TASKS> tasks{};

    const UBaseType_t count{uxTaskGetSystemState(tasks.data(), MAX_TASKS, nullptr)};
    if (count == 0) {
        ESP_LOGD(TAG, "Task table needs more than %u slots", static_cast<unsigned>(MAX_TASKS));
        return;
    }
    for (const TaskStatus_t& task : std::span{tasks}.first(count)) {
        // Free stack is the lifetime low water mark, not the depth right now.
        // Past 64 bytes the line splits across USB packets and another log cuts into it.
        ESP_LOGD(
            TAG,
            "Task %s stack free %u",
            task.pcTaskName,
            static_cast<unsigned>(task.usStackHighWaterMark)
        );
    }
}

#else

// The task table needs CONFIG_FREERTOS_USE_TRACE_FACILITY, which only the dev
// profile carries.
void log_task_stacks() {}

#endif  // CONFIG_FREERTOS_USE_TRACE_FACILITY

}  // namespace

const char* system_info::chip_model() {
    // The bootloader refuses to boot an image built for another chip.
    return CONFIG_IDF_TARGET;
}

const char* system_info::chip_revision() {
    static const std::array<char, REVISION_BUFFER_SIZE> revision{[] {
        // Filled in whole by esp_chip_info
        // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
        esp_chip_info_t info{};
        esp_chip_info(&info);
        std::array<char, REVISION_BUFFER_SIZE> revision{};
        std::snprintf(
            revision.data(),
            revision.size(),
            "v%u.%u",
            static_cast<unsigned>(info.revision / MINORS_PER_MAJOR),
            static_cast<unsigned>(info.revision % MINORS_PER_MAJOR)
        );
        return revision;
    }()};
    return revision.data();
}

const char* system_info::firmware_build() {
    // Filled from the image's descriptor during startup, before app_main.
    return esp_app_get_elf_sha256_str();
}

std::optional<float> system_info::chip_temperature() {
#if SOC_TEMP_SENSOR_SUPPORTED
    // Started on the first reading and kept for the whole boot. A sensor that
    // fails to start is not retried.
    static auto* const temperature_sensor{start_temperature_sensor()};
    if (temperature_sensor == nullptr) {
        return std::nullopt;
    }
    float celsius{0.0f};
    if (temperature_sensor_get_celsius(temperature_sensor, &celsius) != ESP_OK) {
        return std::nullopt;
    }
    return celsius;
#else
    return std::nullopt;
#endif
}

HeapInfo system_info::heap() {
    HeapInfo heap_info{.internal = heap_usage(INTERNAL_CAPS), .psram = std::nullopt};
    // No PSRAM heap is registered when PSRAM is off, absent, or mapped outside the heap.
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
        heap_info.psram = heap_usage(MALLOC_CAP_SPIRAM);
    }
    return heap_info;
}

void system_info::log_memory_stats() {
    // Skipped when debug logging is off, because both halves below walk the heap
    // and every task control block.
    if (esp_log_level_get(TAG) < ESP_LOG_DEBUG) {
        return;
    }

    multi_heap_info_t heap{};
    heap_caps_get_info(&heap, INTERNAL_CAPS);

    // Read only by the heap log, which the prod profile strips
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    const std::optional<float> largest_of_free{math_utils::percent_of_range(
        static_cast<std::uint32_t>(heap.largest_free_block),
        0,
        static_cast<std::uint32_t>(heap.total_free_bytes)
    )};

    // A widening gap between the largest block and total free means fragmentation.
    // Total free spans separate regions, and some gap is there from the start.
    ESP_LOGD(
        TAG,
        "Heap free %u of %u, min ever %u, largest block %u (%.0f%% of free), "
        "used %u in %u blocks, %u free blocks",
        static_cast<unsigned>(heap.total_free_bytes),
        static_cast<unsigned>(heap_caps_get_total_size(INTERNAL_CAPS)),
        static_cast<unsigned>(heap.minimum_free_bytes),
        static_cast<unsigned>(heap.largest_free_block),
        // 0 when nothing is free. It appears next to a 0 total and cannot pass
        // for a healthy figure.
        static_cast<double>(largest_of_free.value_or(0.0f)),
        static_cast<unsigned>(heap.total_allocated_bytes),
        static_cast<unsigned>(heap.allocated_blocks),
        static_cast<unsigned>(heap.free_blocks)
    );

    log_task_stacks();
}
