// SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
//
// Changed from the original: cut down to what the host tests use, with host
// behavior added.

#ifndef HOST_STUB_ESP_ERR_H
#define HOST_STUB_ESP_ERR_H

// Host stub for the ESP-IDF error type, shared by the driver stubs.
// storage/esp_err.h holds the same definitions under the same guard.

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105

typedef int esp_err_t;

inline const char* esp_err_to_name(esp_err_t /*code*/) {
    return "ERROR";
}

#endif  // HOST_STUB_ESP_ERR_H
