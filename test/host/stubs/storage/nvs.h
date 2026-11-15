// SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
//
// Changed from the original: cut down to what the host tests use, with host
// behavior added.

#pragma once

// Host stub for ESP-IDF NVS, backed by a fixed table that lets a test seed
// stored values, see what a load makes of them, and read back what a save wrote. Only
// on the include path of tests that compile storage sources.

#include <esp_err.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// The error codes the firmware tells apart, at their real values
#define ESP_ERR_NVS_BASE 0x1100
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_INVALID_HANDLE (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_KEY_TOO_LONG (ESP_ERR_NVS_BASE + 0x09)
#define ESP_ERR_NVS_NO_FREE_PAGES (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)

// The real cap, terminator included. A longer key is refused rather than
// truncated, which would let two keys share an entry.
#define NVS_KEY_NAME_MAX_SIZE 16

typedef std::uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY,
    NVS_READWRITE
} nvs_open_mode_t;

// Wide enough for every key the store writes
inline constexpr std::size_t NVS_STUB_CAPACITY{8};

struct NvsEntryStub {
    std::int64_t wide_value{0};
    std::uint32_t value{0};
    bool used{false};
    bool wide{false};  // Set through nvs_set_i64 rather than nvs_set_u32
    std::array<char, NVS_KEY_NAME_MAX_SIZE> key{};
};

inline std::array<NvsEntryStub, NVS_STUB_CAPACITY> nvs_stub_entries{};

// What the calls answer. Tests set these to reach failure paths the host has no
// failing part for.
inline esp_err_t nvs_stub_flash_init_result{ESP_OK};
// When set, the mount after an erase succeeds whatever the first one answered.
inline bool nvs_stub_flash_init_recovers{false};
inline esp_err_t nvs_stub_flash_erase_result{ESP_OK};
inline esp_err_t nvs_stub_open_result{ESP_OK};
inline esp_err_t nvs_stub_write_result{ESP_OK};
// Lets a commit fail after its sets succeeded
inline esp_err_t nvs_stub_commit_result{ESP_OK};
// Fails reads of one key only, leaving the rest readable
inline esp_err_t nvs_stub_read_error{ESP_OK};
inline const char* nvs_stub_read_error_key{nullptr};

inline int nvs_stub_commits{0};
inline int nvs_stub_erase_alls{0};
inline int nvs_stub_flash_erases{0};
inline int nvs_stub_flash_inits{0};

inline void nvs_stub_reset() {
    for (NvsEntryStub& entry : nvs_stub_entries) {
        entry = {};
    }
    nvs_stub_flash_init_result = ESP_OK;
    nvs_stub_flash_init_recovers = false;
    nvs_stub_flash_erase_result = ESP_OK;
    nvs_stub_open_result = ESP_OK;
    nvs_stub_write_result = ESP_OK;
    nvs_stub_commit_result = ESP_OK;
    nvs_stub_read_error = ESP_OK;
    nvs_stub_read_error_key = nullptr;
    nvs_stub_commits = 0;
    nvs_stub_erase_alls = 0;
    nvs_stub_flash_erases = 0;
    nvs_stub_flash_inits = 0;
}

inline NvsEntryStub* nvs_stub_find(const char* key) {
    for (NvsEntryStub& entry : nvs_stub_entries) {
        if (entry.used && std::strcmp(entry.key.data(), key) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

inline NvsEntryStub* nvs_stub_claim(const char* key) {
    NvsEntryStub* existing{nvs_stub_find(key)};
    if (existing != nullptr) {
        return existing;
    }
    for (NvsEntryStub& entry : nvs_stub_entries) {
        if (!entry.used) {
            entry.used = true;
            std::strncpy(entry.key.data(), key, NVS_KEY_NAME_MAX_SIZE - 1);
            return &entry;
        }
    }
    return nullptr;
}

// A targeted read fails before the table is consulted, making even a present
// key unreadable.
inline esp_err_t nvs_stub_read_failure(const char* key) {
    if (nvs_stub_read_error != ESP_OK && nvs_stub_read_error_key != nullptr &&
        std::strcmp(nvs_stub_read_error_key, key) == 0) {
        return nvs_stub_read_error;
    }
    return ESP_OK;
}

// Every call refuses a handle no open() returned, before the namespace behind
// it is consulted.
inline bool nvs_stub_opened(nvs_handle_t handle) {
    return handle != 0;
}

inline esp_err_t nvs_open(
    const char* /*namespace_name*/, nvs_open_mode_t /*open_mode*/, nvs_handle_t* out_handle
) {
    if (nvs_stub_open_result != ESP_OK) {
        return nvs_stub_open_result;
    }
    // Nonzero, as the real one is, because the manager treats 0 as nothing open.
    *out_handle = 1;
    return ESP_OK;
}

inline void nvs_close(nvs_handle_t /*handle*/) {}

inline esp_err_t nvs_get_i64(nvs_handle_t handle, const char* key, std::int64_t* out_value) {
    if (!nvs_stub_opened(handle)) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (const esp_err_t forced{nvs_stub_read_failure(key)}; forced != ESP_OK) {
        return forced;
    }
    const NvsEntryStub* entry{nvs_stub_find(key)};
    if (entry == nullptr) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (!entry->wide) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }
    *out_value = entry->wide_value;
    return ESP_OK;
}

inline esp_err_t nvs_get_u32(nvs_handle_t handle, const char* key, std::uint32_t* out_value) {
    if (!nvs_stub_opened(handle)) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (const esp_err_t forced{nvs_stub_read_failure(key)}; forced != ESP_OK) {
        return forced;
    }
    const NvsEntryStub* entry{nvs_stub_find(key)};
    if (entry == nullptr) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (entry->wide) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }
    *out_value = entry->value;
    return ESP_OK;
}

inline esp_err_t nvs_set_i64(nvs_handle_t handle, const char* key, std::int64_t value) {
    if (!nvs_stub_opened(handle)) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (nvs_stub_write_result != ESP_OK) {
        return nvs_stub_write_result;
    }
    if (std::strlen(key) >= NVS_KEY_NAME_MAX_SIZE) {
        return ESP_ERR_NVS_KEY_TOO_LONG;
    }
    NvsEntryStub* entry{nvs_stub_claim(key)};
    if (entry == nullptr) {
        return ESP_ERR_NVS_NO_FREE_PAGES;
    }
    entry->wide = true;
    entry->wide_value = value;
    return ESP_OK;
}

inline esp_err_t nvs_set_u32(nvs_handle_t handle, const char* key, std::uint32_t value) {
    if (!nvs_stub_opened(handle)) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (nvs_stub_write_result != ESP_OK) {
        return nvs_stub_write_result;
    }
    if (std::strlen(key) >= NVS_KEY_NAME_MAX_SIZE) {
        return ESP_ERR_NVS_KEY_TOO_LONG;
    }
    NvsEntryStub* entry{nvs_stub_claim(key)};
    if (entry == nullptr) {
        return ESP_ERR_NVS_NO_FREE_PAGES;
    }
    entry->wide = false;
    entry->value = value;
    return ESP_OK;
}

inline esp_err_t nvs_erase_all(nvs_handle_t handle) {
    if (!nvs_stub_opened(handle)) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    ++nvs_stub_erase_alls;
    for (NvsEntryStub& entry : nvs_stub_entries) {
        entry = {};
    }
    return ESP_OK;
}

inline esp_err_t nvs_commit(nvs_handle_t handle) {
    if (!nvs_stub_opened(handle)) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    // Counted whatever it answers, since the call was made either way.
    ++nvs_stub_commits;
    return nvs_stub_commit_result;
}
