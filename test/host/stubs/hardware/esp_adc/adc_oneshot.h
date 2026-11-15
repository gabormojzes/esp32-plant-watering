// SPDX-FileCopyrightText: 2020-2025 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
//
// Changed from the original: cut down to what the host tests use, with host
// behavior added.

#pragma once

// Host stub for the ESP-IDF one-shot ADC driver, with scripted conversions. Only
// the symbols the firmware uses. Only on the include path of tests that compile
// hardware sources.

#include <esp_err.h>

// As ESP-IDF 5.5.6 declares them in hal/adc_types.h and esp_adc/adc_oneshot.h,
// without the deprecated ADC_ATTEN_DB_11 and the LP-core ULP mode of chips that
// have one. The clock source takes the real header's default type.
typedef enum {
    ADC_UNIT_1,
    ADC_UNIT_2,
} adc_unit_t;

typedef enum {
    ADC_CHANNEL_0,
    ADC_CHANNEL_1,
    ADC_CHANNEL_2,
    ADC_CHANNEL_3,
    ADC_CHANNEL_4,
    ADC_CHANNEL_5,
    ADC_CHANNEL_6,
    ADC_CHANNEL_7,
    ADC_CHANNEL_8,
    ADC_CHANNEL_9,
    ADC_CHANNEL_10,
} adc_channel_t;

typedef enum {
    ADC_ATTEN_DB_0 = 0,
    ADC_ATTEN_DB_2_5 = 1,
    ADC_ATTEN_DB_6 = 2,
    ADC_ATTEN_DB_12 = 3,
} adc_atten_t;

typedef enum {
    ADC_BITWIDTH_DEFAULT = 0,
    ADC_BITWIDTH_9 = 9,
    ADC_BITWIDTH_10 = 10,
    ADC_BITWIDTH_11 = 11,
    ADC_BITWIDTH_12 = 12,
    ADC_BITWIDTH_13 = 13,
} adc_bitwidth_t;

typedef enum {
    ADC_ULP_MODE_DISABLE = 0,
    ADC_ULP_MODE_FSM = 1,
    ADC_ULP_MODE_RISCV = 2,
} adc_ulp_mode_t;

typedef int adc_oneshot_clk_src_t;

typedef struct adc_oneshot_unit_ctx_t* adc_oneshot_unit_handle_t;

typedef struct {
    adc_unit_t unit_id;
    adc_oneshot_clk_src_t clk_src;
    adc_ulp_mode_t ulp_mode;
} adc_oneshot_unit_init_cfg_t;

typedef struct {
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
} adc_oneshot_chan_cfg_t;

// The driver's unit, which the real header leaves opaque
struct adc_oneshot_unit_ctx_t {};

// Tests set these to a nonzero value to make the matching call fail.
inline esp_err_t adc_stub_new_unit_result{ESP_OK};
inline esp_err_t adc_stub_config_channel_result{ESP_OK};
// Raw count every successful conversion reports
inline int adc_stub_read_value{0};
// Conversions that fail before the rest succeed, one consumed per read
inline int adc_stub_read_failures{0};
// Whether the unit is claimed. The real driver refuses a second claim.
inline bool adc_stub_unit_claimed{false};

inline void adc_stub_reset() {
    adc_stub_new_unit_result = ESP_OK;
    adc_stub_config_channel_result = ESP_OK;
    adc_stub_read_value = 0;
    adc_stub_read_failures = 0;
    adc_stub_unit_claimed = false;
}

inline esp_err_t adc_oneshot_new_unit(
    const adc_oneshot_unit_init_cfg_t* /*init_config*/, adc_oneshot_unit_handle_t* ret_unit
) {
    if (adc_stub_new_unit_result != ESP_OK) {
        return adc_stub_new_unit_result;
    }
    // The real driver refuses a second claim too, which catches a bring-up that
    // does not release first.
    if (adc_stub_unit_claimed) {
        return ESP_FAIL;
    }
    adc_stub_unit_claimed = true;
    // Written only on success, as the real driver does, which leaves a caller
    // that failed to claim with whatever handle it already held.
    static adc_oneshot_unit_ctx_t unit{};
    *ret_unit = &unit;
    return ESP_OK;
}

inline esp_err_t adc_oneshot_config_channel(
    adc_oneshot_unit_handle_t /*handle*/,
    adc_channel_t /*channel*/,
    const adc_oneshot_chan_cfg_t* /*config*/
) {
    return adc_stub_config_channel_result;
}

inline esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t /*handle*/) {
    adc_stub_unit_claimed = false;
    return ESP_OK;
}

inline esp_err_t adc_oneshot_read(
    adc_oneshot_unit_handle_t /*handle*/, adc_channel_t /*channel*/, int* out
) {
    if (adc_stub_read_failures > 0) {
        adc_stub_read_failures--;
        return ESP_FAIL;
    }
    *out = adc_stub_read_value;
    return ESP_OK;
}
