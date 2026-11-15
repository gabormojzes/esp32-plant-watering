#pragma once

// Host stub for ESP-IDF logging. The log macros compile away but still evaluate
// their arguments, keeping values used only for logging from counting as
// unused.

namespace esp_log_stub {

template <typename... Args>
inline void sink(const Args&... /*args*/) {}

}  // namespace esp_log_stub

#define ESP_LOGE(...) esp_log_stub::sink(__VA_ARGS__)
#define ESP_LOGW(...) esp_log_stub::sink(__VA_ARGS__)
#define ESP_LOGI(...) esp_log_stub::sink(__VA_ARGS__)
#define ESP_LOGD(...) esp_log_stub::sink(__VA_ARGS__)
#define ESP_LOGV(...) esp_log_stub::sink(__VA_ARGS__)
