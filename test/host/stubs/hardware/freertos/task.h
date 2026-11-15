// SPDX-FileCopyrightText: 2021 Amazon.com, Inc. or its affiliates
// SPDX-License-Identifier: MIT
//
// Changed from the original: cut down to what the host tests use, with host
// behavior added.

#pragma once

// Host stub for the FreeRTOS task API. The pacing delay does nothing, and a burst
// that spaces its reads on the device runs straight through here. No task is
// ever spawned, which leaves a test to call what the task would.

#include <freertos/FreeRTOS.h>

#include <cstdint>

// As ESP-IDF 5.5.6 declares them in freertos/projdefs.h and freertos/task.h
typedef void (*TaskFunction_t)(void*);
typedef struct tskTaskControlBlock* TaskHandle_t;

inline void vTaskDelay(const TickType_t /*xTicksToDelay*/) {}

inline BaseType_t xTaskCreate(
    TaskFunction_t /*pxTaskCode*/,
    const char* /*pcName*/,
    std::uint32_t /*usStackDepth*/,
    void* /*pvParameters*/,
    UBaseType_t /*uxPriority*/,
    TaskHandle_t* /*pxCreatedTask*/
) {
    return pdPASS;
}
