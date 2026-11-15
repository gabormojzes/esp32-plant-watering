#pragma once

// Host stub for the ESP-IDF NVS partition calls, beside the namespace ones in
// nvs.h and sharing their table.

#include <nvs.h>

// The manager erases and remounts a full or newer-format partition. The mount
// after an erase may answer differently, letting a test drive that recovery.
inline esp_err_t nvs_flash_init() {
    ++nvs_stub_flash_inits;
    if (nvs_stub_flash_inits > 1 && nvs_stub_flash_init_recovers) {
        return ESP_OK;
    }
    return nvs_stub_flash_init_result;
}

inline esp_err_t nvs_flash_erase() {
    ++nvs_stub_flash_erases;
    if (nvs_stub_flash_erase_result != ESP_OK) {
        return nvs_stub_flash_erase_result;
    }
    for (NvsEntryStub& entry : nvs_stub_entries) {
        entry = {};
    }
    return ESP_OK;
}
