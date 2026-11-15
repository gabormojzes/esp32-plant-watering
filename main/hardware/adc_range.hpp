#pragma once

#include <soc/soc_caps.h>

#include <cstdint>

namespace adc_range {

inline constexpr std::uint16_t MAX_RAW{(1U << SOC_ADC_RTC_MAX_BITWIDTH) - 1};

}  // namespace adc_range
