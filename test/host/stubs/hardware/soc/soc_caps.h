// SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
//
// Changed from the original: cut down to what the host tests use.

#pragma once

// Host stub for the ESP-IDF chip capabilities, holding only what the host-tested
// code reads.

// As ESP-IDF 5.5.6 declares it for the ESP32-C6
#define SOC_ADC_RTC_MAX_BITWIDTH (12)
