// SPDX-FileCopyrightText: 2021 Amazon.com, Inc. or its affiliates
// SPDX-License-Identifier: MIT
//
// Changed from the original: cut down to what the host tests use.

#pragma once

// Host stub for the FreeRTOS kernel header. The firmware includes it for the
// tick conversion beside vTaskDelay and for the base types the queue API is
// spelled in. On the host a tick is a millisecond.

#include <cstdint>

#define pdMS_TO_TICKS(ms) (ms)

// As ESP-IDF 5.5.6 declares them in portmacro.h and projdefs.h for a RISC-V chip
// with 32-bit ticks
#define portBASE_TYPE int

typedef portBASE_TYPE BaseType_t;
typedef unsigned portBASE_TYPE UBaseType_t;
typedef std::uint32_t TickType_t;

#define pdFALSE ((BaseType_t)0)
#define pdTRUE ((BaseType_t)1)
#define pdPASS (pdTRUE)
