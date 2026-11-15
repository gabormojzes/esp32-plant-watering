#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

#include "fault/fault.hpp"

namespace fault_definitions {

// Sensor - SEN
inline constexpr Fault SENSOR_ADC_INIT_FAILED{
    .code = "ESEN01",
    .severity = Severity::Error,
    .message = "Sensor ADC init failed",
};
inline constexpr Fault SENSOR_READS_FAILED{
    .code = "ESEN02",
    .severity = Severity::Error,
    .message = "All sensor ADC reads failed",
};
inline constexpr Fault SENSOR_OUT_OF_RANGE{
    .code = "ESEN03",
    .severity = Severity::Error,
    .message = "Sensor reading out of range",
};
inline constexpr Fault SENSOR_TASK_START_FAILED{
    .code = "ESEN04",
    .severity = Severity::Error,
    .message = "Sensor task start failed",
};
inline constexpr Fault SENSOR_CALIBRATION_INVALID{
    .code = "ESEN05",
    .severity = Severity::Error,
    .message = "Sensor calibration range maps nothing",
};
inline constexpr Fault SENSOR_SMOOTHED_UNAVAILABLE{
    .code = "ESEN06",
    .severity = Severity::Error,
    .message = "Sensor smoothed reading unavailable",
};

// Pump - PMP
inline constexpr Fault PUMP_BRING_UP_FAILED{
    .code = "EPMP01",
    .severity = Severity::Error,
    .message = "Pump bring-up failed",
};

// Status LED - LED
inline constexpr Fault STATUS_LED_INIT_FAILED{
    .code = "WLED01",
    .severity = Severity::Warning,
    .message = "Status LED init failed",
};

// Watchdog - WDG
inline constexpr Fault WATCHDOG_SUBSCRIBE_FAILED{
    .code = "EWDG01",
    .severity = Severity::Error,
    .message = "Watchdog task subscribe failed",
};

// Settings - SET
inline constexpr Fault SETTINGS_STORAGE_UNAVAILABLE{
    .code = "WSET01",
    .severity = Severity::Warning,
    .message = "Settings storage unavailable",
};
inline constexpr Fault SETTINGS_READ_FAILED{
    .code = "WSET02",
    .severity = Severity::Warning,
    .message = "Stored settings unreadable",
};
inline constexpr Fault SETTINGS_WRITE_FAILED{
    .code = "WSET03",
    .severity = Severity::Warning,
    .message = "Settings not written",
};

// WiFi - WIF
inline constexpr Fault WIFI_BRING_UP_FAILED{
    .code = "WWIF01",
    .severity = Severity::Warning,
    .message = "WiFi bring-up failed",
};
inline constexpr Fault WIFI_LINK_DOWN{
    .code = "WWIF02",
    .severity = Severity::Warning,
    .message = "WiFi link down",
};

// MQTT client - MQT
inline constexpr Fault MQTT_CLIENT_START_FAILED{
    .code = "WMQT01",
    .severity = Severity::Warning,
    .message = "MQTT client start failed",
};
inline constexpr Fault MQTT_CONNECTION_REFUSED{
    .code = "WMQT02",
    .severity = Severity::Warning,
    .message = "MQTT broker refused the connection",
};
inline constexpr Fault MQTT_SUBSCRIPTION_MISSING{
    .code = "WMQT03",
    .severity = Severity::Warning,
    .message = "MQTT topic not subscribed",
};

// Commands - CMD
inline constexpr Fault MQTT_COMMAND_QUEUE_UNAVAILABLE{
    .code = "WCMD01",
    .severity = Severity::Warning,
    .message = "MQTT command queue unavailable",
};
inline constexpr Fault HTTP_COMMAND_QUEUE_UNAVAILABLE{
    .code = "WCMD02",
    .severity = Severity::Warning,
    .message = "HTTP command queue unavailable",
};

// Telemetry - TLM
inline constexpr Fault MQTT_TELEMETRY_MAILBOX_UNAVAILABLE{
    .code = "WTLM01",
    .severity = Severity::Warning,
    .message = "MQTT telemetry mailbox unavailable",
};
inline constexpr Fault MQTT_PUBLISHER_TASK_START_FAILED{
    .code = "WTLM02",
    .severity = Severity::Warning,
    .message = "MQTT publisher task start failed",
};
inline constexpr Fault HTTP_TELEMETRY_MAILBOX_UNAVAILABLE{
    .code = "WTLM03",
    .severity = Severity::Warning,
    .message = "HTTP telemetry mailbox unavailable",
};

// Web server - WEB
inline constexpr Fault WEB_SERVER_START_FAILED{
    .code = "WWEB01",
    .severity = Severity::Warning,
    .message = "Web server start failed",
};
inline constexpr Fault WEB_ROUTE_REGISTER_FAILED{
    .code = "WWEB02",
    .severity = Severity::Warning,
    .message = "Web route registration failed",
};

// Every fault the firmware can raise. Tables of faults are sized from this
// list, and the checks below run over it. A fault missing here goes unchecked,
// and no table sets aside a slot for it.
inline constexpr std::array ALL{
    SENSOR_ADC_INIT_FAILED,
    SENSOR_READS_FAILED,
    SENSOR_OUT_OF_RANGE,
    SENSOR_TASK_START_FAILED,
    SENSOR_CALIBRATION_INVALID,
    SENSOR_SMOOTHED_UNAVAILABLE,
    PUMP_BRING_UP_FAILED,
    STATUS_LED_INIT_FAILED,
    WATCHDOG_SUBSCRIBE_FAILED,
    SETTINGS_STORAGE_UNAVAILABLE,
    SETTINGS_READ_FAILED,
    SETTINGS_WRITE_FAILED,
    WIFI_BRING_UP_FAILED,
    WIFI_LINK_DOWN,
    MQTT_CLIENT_START_FAILED,
    MQTT_CONNECTION_REFUSED,
    MQTT_SUBSCRIPTION_MISSING,
    MQTT_COMMAND_QUEUE_UNAVAILABLE,
    HTTP_COMMAND_QUEUE_UNAVAILABLE,
    MQTT_TELEMETRY_MAILBOX_UNAVAILABLE,
    MQTT_PUBLISHER_TASK_START_FAILED,
    HTTP_TELEMETRY_MAILBOX_UNAVAILABLE,
    WEB_SERVER_START_FAILED,
    WEB_ROUTE_REGISTER_FAILED,
};

consteval bool all_distinct() {
    for (std::size_t i{0}; i < ALL.size(); ++i) {
        for (std::size_t j{i + 1}; j < ALL.size(); ++j) {
            // Compared by spelling, because codes are pointers and two codes
            // spelled the same need not share one.
            if (std::string_view{ALL[i].code} == std::string_view{ALL[j].code}) {
                return false;
            }
        }
    }
    return true;
}

static_assert(all_distinct(), "two faults share a code");

consteval char severity_letter(Severity severity) {
    switch (severity) {
        case Severity::Warning:
            return 'W';
        case Severity::Error:
            return 'E';
    }
    // A severity with no letter here fails every code raised at it.
    return '\0';
}

// A code is its severity letter, a three-letter component and a two-digit id.
// The code alone must tell a reader the severity.
consteval bool is_well_formed(const Fault& fault) {
    const std::string_view code{fault.code};
    const auto upper{[](char c) { return c >= 'A' && c <= 'Z'; }};
    const auto digit{[](char c) { return c >= '0' && c <= '9'; }};
    return code.size() == 6 && code[0] == severity_letter(fault.severity) &&
           std::ranges::all_of(code.substr(1, 3), upper) &&
           std::ranges::all_of(code.substr(4), digit);
}

consteval bool all_well_formed() {
    return std::ranges::all_of(ALL, is_well_formed);
}

static_assert(all_well_formed(), "a fault code does not spell its severity, component and id");

}  // namespace fault_definitions
