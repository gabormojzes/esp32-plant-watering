// SPDX-FileCopyrightText: 2010-2025 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
//
// Changed from the original: cut down to what the host tests use, with host
// behavior added.

#pragma once

// Host stub for the ESP-IDF GPIO driver, with only the symbols the firmware uses
// and a record of what reaches the pin. Only on the include path of tests that
// compile hardware sources.

#include <esp_err.h>
#include <soc/gpio_num.h>

#include <cstdint>

// As ESP-IDF 5.5.6 declares them in esp_bit_defs.h, hal/gpio_types.h and
// driver/gpio.h. gpio_config_t leaves out the hysteresis field, which the real
// header has only on chips with SOC_GPIO_SUPPORT_PIN_HYS_FILTER.
#define BIT0 0x00000001
#define BIT1 0x00000002
#define BIT2 0x00000004

#define GPIO_MODE_DEF_DISABLE (0)
#define GPIO_MODE_DEF_INPUT (BIT0)
#define GPIO_MODE_DEF_OUTPUT (BIT1)
#define GPIO_MODE_DEF_OD (BIT2)

typedef enum {
    GPIO_MODE_DISABLE = GPIO_MODE_DEF_DISABLE,
    GPIO_MODE_INPUT = GPIO_MODE_DEF_INPUT,
    GPIO_MODE_OUTPUT = GPIO_MODE_DEF_OUTPUT,
    GPIO_MODE_OUTPUT_OD = ((GPIO_MODE_DEF_OUTPUT) | (GPIO_MODE_DEF_OD)),
    GPIO_MODE_INPUT_OUTPUT_OD =
        ((GPIO_MODE_DEF_INPUT) | (GPIO_MODE_DEF_OUTPUT) | (GPIO_MODE_DEF_OD)),
    GPIO_MODE_INPUT_OUTPUT = ((GPIO_MODE_DEF_INPUT) | (GPIO_MODE_DEF_OUTPUT)),
} gpio_mode_t;

typedef enum {
    GPIO_PULLUP_DISABLE = 0x0,
    GPIO_PULLUP_ENABLE = 0x1,
} gpio_pullup_t;

typedef enum {
    GPIO_PULLDOWN_DISABLE = 0x0,
    GPIO_PULLDOWN_ENABLE = 0x1,
} gpio_pulldown_t;

typedef enum {
    GPIO_INTR_DISABLE = 0,
    GPIO_INTR_POSEDGE = 1,
    GPIO_INTR_NEGEDGE = 2,
    GPIO_INTR_ANYEDGE = 3,
    GPIO_INTR_LOW_LEVEL = 4,
    GPIO_INTR_HIGH_LEVEL = 5,
    GPIO_INTR_MAX,
} gpio_int_type_t;

typedef struct {
    std::uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

// Mirrors the real macro, short-circuit included, which rejects a negative pin
// before it can become a negative shift. The mask stands in for the per-chip
// one, which on the target is narrower wherever it has input-only pins.
#define SOC_GPIO_VALID_OUTPUT_GPIO_MASK 0x7FFFFFFFULL
#define GPIO_IS_VALID_OUTPUT_GPIO(gpio_num) \
    (((gpio_num) >= 0) && ((((1ULL << (gpio_num))) & SOC_GPIO_VALID_OUTPUT_GPIO_MASK) != 0))

// Tests set this to a nonzero value to make gpio_config() fail
inline esp_err_t gpio_stub_config_result{ESP_OK};
// What the pin was last driven to, and how many writes it took. Tests reset both
// and read them back.
inline std::uint32_t gpio_stub_level{0};
inline int gpio_stub_set_level_calls{0};

inline esp_err_t gpio_config(const gpio_config_t* /*pGPIOConfig*/) {
    return gpio_stub_config_result;
}

inline esp_err_t gpio_set_level(gpio_num_t /*gpio_num*/, std::uint32_t level) {
    gpio_stub_level = level;
    ++gpio_stub_set_level_calls;
    return ESP_OK;
}
