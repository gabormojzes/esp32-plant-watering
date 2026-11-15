// SPDX-FileCopyrightText: 2018-2024 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
//
// Changed from the original: cut down to what the host tests use.

#pragma once

// Host stub for the ESP-IDF event loop types, which esp-mqtt spells its event
// handler in.

#include <cstdint>

// As ESP-IDF 5.5.6 declares them in esp_event_base.h
typedef const char* esp_event_base_t;
typedef void (*esp_event_handler_t)(
    void* event_handler_arg, esp_event_base_t event_base, std::int32_t event_id, void* event_data
);
