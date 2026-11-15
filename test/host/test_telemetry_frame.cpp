#include <doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#include "fault/fault.hpp"
#include "heap_info.hpp"
#include "messaging/json_codec.hpp"
#include "messaging/mqtt/mqtt_info.hpp"
#include "messaging/telemetry_assembler.hpp"
#include "network/wifi_info.hpp"
#include "shared/constants.hpp"
#include "shared/net_utils.hpp"

// The frame as it goes on the wire, byte for byte, from the snapshots the
// application hands over. docs/telemetry.md describes its fields.

using constants::US_PER_MS;
using constants::US_PER_S;

namespace {

std::string_view text_of(const Payload& payload) {
    return {payload.data.data(), static_cast<std::size_t>(payload.length)};
}

}  // namespace

TEST_CASE("a running device's frame carries every field in wire units") {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 55.5f,
        .sensor_deadband = 0.5f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = 5 * US_PER_S,
        .pump_wait_time_us = 60 * US_PER_S,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{
        "test-device", "1.0.0", "b6c3172ee", "esp32c6", "v0.1", "panic", true
    };
    const Telemetry telemetry{assembler.assemble(
        runtime_settings,
        {.latest = 40.25f, .smoothed = 42.0f, .raw = 1234, .ready = true},
        {.on = true},
        {.run_count = 3,
         .current_run_time_us = 4 * US_PER_S,
         .idle_time_us = 9 * US_PER_S,
         .remaining_wait_time_us = 30 * US_PER_S,
         .total_run_time_us = 12 * US_PER_S},
        {.wifi =
             WifiInfo{
                 .mode = "sta",
                 .channel = 6,
                 .station =
                     WifiInfo::Station{
                         .mac = net_utils::MacAddress{0x00, 0x00, 0x5E, 0x00, 0x53, 0xAF},
                         .bssid = net_utils::MacAddress{0x00, 0x00, 0x5E, 0x00, 0x53, 0x01},
                         .rssi = -67,
                         .reconnects = 2,
                         .ipv4 = net_utils::Ipv4Address{192, 168, 1, 5},
                     },
                 .access_point = std::nullopt,
             }},
        {.mqtt = MqttInfo{.connected = true, .reconnects = 4}},
        41.7f,
        {.internal =
             {.total = 362396,
              .free = 237968,
              .min_free = 225232,
              .largest_free_block = 217088,
              .free_blocks = 9},
         .psram = std::nullopt},
        {},
        90 * US_PER_S
    )};

    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));
    CHECK(
        text_of(payload) ==
        R"({"sequence":1,"device":{"name":"test-device","uptime":90,"resetReason":"panic","coreDump":true,"firmware":{"version":"1.0.0","build":"b6c3172ee"},)"
        R"("chip":{"model":"esp32c6","revision":"v0.1","temperature":41.7},)"
        R"("heap":{"internal":{"total":362396,"free":237968,"minFree":225232,"largestFreeBlock":217088,"freeBlocks":9},"psram":null},)"
        R"("network":{"wifi":{"mode":"sta","channel":6,"station":{"mac":"00:00:5e:00:53:af",)"
        R"("bssid":"00:00:5e:00:53:01","rssi":-67,"reconnects":2,"ipv4":"192.168.1.5"}}},)"
        R"("messaging":{"mqtt":{"connected":true,"reconnects":4}}},)"
        R"("sensor":{"target":55.50,"deadband":0.50,"calibration":{"adcMin":1200,"adcMax":2900},)"
        R"("latest":40.25,"smoothed":42.00,"raw":1234,)"
        R"("ready":true,"faulted":false},)"
        R"("pump":{"runCount":3,"on":true,"currentRunTime":4,"idleTime":9,"automatic":true,"duration":5,)"
        R"("waitTime":60,"remainingWaitTime":30,"totalRunTime":12,"faulted":false},)"
        R"("faults":[]})"
    );
}

TEST_CASE("a broken device's frame carries its faults and rounds part seconds") {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = 1500 * US_PER_MS,
        .pump_wait_time_us = 0,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"d", "2", "0123abcde", "esp32", "v3.1", "brownout", false};
    const std::array<FaultEntry, 2> faults{{
        {.fault = {.code = "ETST01", .severity = Severity::Error, .message = "e"}, .active = true},
        {.fault = {.code = "WTST01", .severity = Severity::Warning, .message = "w"},
         .active = false},
    }};
    const Telemetry telemetry{assembler.assemble(
        runtime_settings,
        {.ready = false, .faulted = true},
        {.on = false, .faulted = true},
        {.idle_time_us = 2500 * US_PER_MS,
         .remaining_wait_time_us = 200 * US_PER_MS,
         .total_run_time_us = 3999 * US_PER_MS},
        {},
        {},
        std::nullopt,
        {},
        faults,
        1900 * US_PER_MS
    )};

    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));
    CHECK(
        text_of(payload) ==
        R"({"sequence":1,"device":{"name":"d","uptime":1,"resetReason":"brownout","coreDump":false,"firmware":{"version":"2","build":"0123abcde"},)"
        R"("chip":{"model":"esp32","revision":"v3.1","temperature":null},)"
        R"("heap":{"internal":{"total":0,"free":0,"minFree":0,"largestFreeBlock":0,"freeBlocks":0},"psram":null},)"
        R"("network":{},"messaging":{}},)"
        R"("sensor":{"target":50.00,"deadband":0.00,"calibration":{"adcMin":1200,"adcMax":2900},)"
        R"("latest":0.00,"smoothed":0.00,"raw":0,)"
        R"("ready":false,"faulted":true},)"
        R"("pump":{"runCount":0,"on":false,"currentRunTime":0,"idleTime":2,"automatic":true,"duration":1,)"
        R"("waitTime":0,"remainingWaitTime":1,"totalRunTime":3,"faulted":true},)"
        R"("faults":[{"code":"ETST01","active":true},{"code":"WTST01","active":false}]})"
    );
}

TEST_CASE(
    "an access point's frame carries its clients and address beside a station still looking"
) {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = US_PER_S,
        .pump_wait_time_us = 0,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"d", "2", "0123abcde", "esp32s3", "v0.2", "powerOn", false};
    const Telemetry telemetry{assembler.assemble(
        runtime_settings,
        {},
        {},
        {},
        {.wifi =
             WifiInfo{
                 .mode = "apsta",
                 .channel = 1,
                 .station =
                     WifiInfo::Station{
                         .mac = net_utils::MacAddress{0x00, 0x00, 0x5E, 0x00, 0x53, 0xAF},
                         .bssid = std::nullopt,
                         .rssi = std::nullopt,
                         .reconnects = 0,
                         .ipv4 = std::nullopt,
                     },
                 .access_point =
                     WifiInfo::AccessPoint{
                         .clients = 0, .ipv4 = net_utils::Ipv4Address{192, 168, 4, 1}
                     },
             }},
        {.mqtt = MqttInfo{.connected = false, .reconnects = 0}},
        {},
        {},
        {},
        0
    )};

    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));
    CHECK(
        text_of(payload) ==
        R"({"sequence":1,"device":{"name":"d","uptime":0,"resetReason":"powerOn","coreDump":false,"firmware":{"version":"2","build":"0123abcde"},)"
        R"("chip":{"model":"esp32s3","revision":"v0.2","temperature":null},)"
        R"("heap":{"internal":{"total":0,"free":0,"minFree":0,"largestFreeBlock":0,"freeBlocks":0},"psram":null},)"
        R"("network":{"wifi":{"mode":"apsta","channel":1,"station":{"mac":"00:00:5e:00:53:af",)"
        R"("bssid":null,"rssi":null,"reconnects":0,"ipv4":null},)"
        R"("accessPoint":{"clients":0,"ipv4":"192.168.4.1"}}},)"
        R"("messaging":{"mqtt":{"connected":false,"reconnects":0}}},)"
        R"("sensor":{"target":50.00,"deadband":0.00,"calibration":{"adcMin":1200,"adcMax":2900},)"
        R"("latest":0.00,"smoothed":0.00,"raw":0,)"
        R"("ready":false,"faulted":false},)"
        R"("pump":{"runCount":0,"on":false,"currentRunTime":0,"idleTime":0,"automatic":true,"duration":1,)"
        R"("waitTime":0,"remainingWaitTime":0,"totalRunTime":0,"faulted":false},)"
        R"("faults":[]})"
    );
}

// The payload is sized for this frame, and a field or a code added without
// resizing it fails here rather than as a truncated frame on the wire.
TEST_CASE("the widest possible frame still fits the payload") {
    // The longest name, version and build the assembler takes
    std::array<char, TelemetryAssembler::MAX_DEVICE_NAME_SIZE> widest_name{};
    widest_name.fill('X');
    widest_name.back() = '\0';
    std::array<char, TelemetryAssembler::MAX_FIRMWARE_VERSION_SIZE> widest_firmware{};
    widest_firmware.fill('9');
    widest_firmware.back() = '\0';
    std::array<char, TelemetryAssembler::MAX_FIRMWARE_BUILD_SIZE> widest_build{};
    widest_build.fill('f');
    widest_build.back() = '\0';

    // Every flag is false, the longer spelling. The chip model, its revision, the reset
    // reason and the mode are the longest names, and every number prints at its widest.
    // The chip temperature is the sensor's floor, which ties its ceiling of 125.0.
    // PSRAM is in the heap, and MQTT is built in.
    constexpr std::int64_t widest_us{std::numeric_limits<std::int64_t>::max()};
    constexpr std::uint32_t widest_count{std::numeric_limits<std::uint32_t>::max()};
    constexpr HeapUsage widest_heap{
        .total = widest_count,
        .free = widest_count,
        .min_free = widest_count,
        .largest_free_block = widest_count,
        .free_blocks = widest_count,
    };
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 100.0f,
        .sensor_deadband = 100.0f,
        .sensor_calibration =
            {.adc_min = std::numeric_limits<std::uint16_t>::max(),
             .adc_max = std::numeric_limits<std::uint16_t>::max()},
        .pump_duration_us = widest_us,
        .pump_wait_time_us = widest_us,
        .pump_automatic = false,
    }};
    TelemetryAssembler assembler{
        widest_name.data(),
        widest_firmware.data(),
        widest_build.data(),
        "esp32c61",
        "v655.35",
        "interruptWdt",
        false
    };
    std::array<FaultEntry, Telemetry::MAX_FAULTS> faults{};
    for (FaultEntry& entry : faults) {
        entry = {
            .fault = {.code = "EXXX99", .severity = Severity::Error, .message = ""},
            .active = false,
        };
    }
    constexpr net_utils::MacAddress widest_mac{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    constexpr net_utils::Ipv4Address widest_ipv4{255, 255, 255, 255};
    Telemetry telemetry{assembler.assemble(
        runtime_settings,
        {.latest = 100.0f, .smoothed = 100.0f, .raw = std::numeric_limits<std::uint32_t>::max()},
        {},
        {.run_count = std::numeric_limits<std::uint32_t>::max(),
         .current_run_time_us = widest_us,
         .idle_time_us = widest_us,
         .remaining_wait_time_us = widest_us,
         .total_run_time_us = widest_us},
        {.wifi =
             WifiInfo{
                 .mode = "apsta",
                 .channel = std::numeric_limits<std::uint8_t>::max(),
                 .station =
                     WifiInfo::Station{
                         .mac = widest_mac,
                         .bssid = widest_mac,
                         .rssi = std::numeric_limits<std::int8_t>::min(),
                         .reconnects = std::numeric_limits<std::uint32_t>::max(),
                         .ipv4 = widest_ipv4,
                     },
                 .access_point =
                     WifiInfo::AccessPoint{
                         .clients = std::numeric_limits<std::uint8_t>::max(),
                         .ipv4 = widest_ipv4,
                     },
             }},
        {.mqtt =
             MqttInfo{
                 .connected = false,
                 .reconnects = std::numeric_limits<std::uint32_t>::max(),
             }},
        -40.0f,
        {.internal = widest_heap, .psram = widest_heap},
        faults,
        widest_us
    )};
    telemetry.sequence = std::numeric_limits<std::uint64_t>::max();

    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));
    MESSAGE("widest frame: " << payload.length << " of " << Payload::BUFFER_SIZE << " bytes");
}
