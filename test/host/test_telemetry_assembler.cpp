#include <doctest.h>

#include <array>
#include <string_view>

#include "fault/fault.hpp"
#include "heap_info.hpp"
#include "messaging/messaging_info.hpp"
#include "messaging/mqtt/mqtt_info.hpp"
#include "messaging/telemetry_assembler.hpp"
#include "network/network_info.hpp"
#include "network/wifi_info.hpp"
#include "shared/constants.hpp"
#include "shared/net_utils.hpp"

using constants::US_PER_S;

TEST_CASE(
    "assemble merges the name, the firmware version and build, the reset reason, the runtime "
    "settings, the snapshots, the network info, the messaging info, the chip temperature, the "
    "heap and the uptime into one document"
) {
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

    const SensorSnapshot sensor_snapshot{
        .latest = 40.25f, .smoothed = 42.0f, .raw = 1234, .ready = true
    };
    const PumpSnapshot pump_snapshot{.on = true, .faulted = false};
    const IrrigationState irrigation_state{
        .run_count = 3,
        .current_run_time_us = 4 * US_PER_S,
        .idle_time_us = 9 * US_PER_S,
        .remaining_wait_time_us = 30 * US_PER_S,
        .total_run_time_us = 12 * US_PER_S,
    };
    const NetworkInfo network_info{
        .wifi = WifiInfo{
            .mode = "apsta",
            .channel = 6,
            .station =
                WifiInfo::Station{
                    .mac = net_utils::MacAddress{0x00, 0x00, 0x5E, 0x00, 0x53, 0xAF},
                    .bssid = net_utils::MacAddress{0x00, 0x00, 0x5E, 0x00, 0x53, 0x01},
                    .rssi = -67,
                    .reconnects = 2,
                    .ipv4 = net_utils::Ipv4Address{192, 168, 1, 5},
                },
            .access_point =
                WifiInfo::AccessPoint{.clients = 1, .ipv4 = net_utils::Ipv4Address{192, 168, 4, 1}},
        }
    };
    const MessagingInfo messaging_info{.mqtt = MqttInfo{.connected = true, .reconnects = 4}};
    const HeapInfo heap_info{
        .internal =
            {.total = 362396,
             .free = 237968,
             .min_free = 225232,
             .largest_free_block = 217088,
             .free_blocks = 9},
        .psram = std::nullopt,
    };
    const Telemetry telemetry{assembler.assemble(
        runtime_settings,
        sensor_snapshot,
        pump_snapshot,
        irrigation_state,
        network_info,
        messaging_info,
        41.7f,
        heap_info,
        {},
        90 * US_PER_S
    )};

    // From the constructor
    CHECK(std::string_view{telemetry.device.name} == "test-device");
    CHECK(std::string_view{telemetry.device.firmware.version} == "1.0.0");
    CHECK(std::string_view{telemetry.device.firmware.build} == "b6c3172ee");
    CHECK(std::string_view{telemetry.device.chip.model} == "esp32c6");
    CHECK(std::string_view{telemetry.device.chip.revision} == "v0.1");
    CHECK(std::string_view{telemetry.device.reset_reason} == "panic");
    CHECK(telemetry.device.core_dump);
    // From the caller
    CHECK(telemetry.device.uptime_s == 90);
    CHECK(telemetry.device.chip.temperature == doctest::Approx(41.7f));
    CHECK(telemetry.device.heap.internal.total == 362396);
    CHECK(telemetry.device.heap.internal.free == 237968);
    CHECK(telemetry.device.heap.internal.min_free == 225232);
    CHECK(telemetry.device.heap.internal.largest_free_block == 217088);
    CHECK(telemetry.device.heap.internal.free_blocks == 9);
    CHECK_FALSE(telemetry.device.heap.psram);
    // From the network info
    REQUIRE(telemetry.device.network.wifi);
    const Telemetry::Wifi& wifi{*telemetry.device.network.wifi};
    CHECK(std::string_view{wifi.mode} == "apsta");
    CHECK(wifi.channel == 6);
    REQUIRE(wifi.station);
    REQUIRE(wifi.station->mac);
    CHECK(std::string_view{wifi.station->mac->data()} == "00:00:5e:00:53:af");
    REQUIRE(wifi.station->bssid);
    CHECK(std::string_view{wifi.station->bssid->data()} == "00:00:5e:00:53:01");
    CHECK(wifi.station->rssi == -67);
    CHECK(wifi.station->reconnects == 2);
    REQUIRE(wifi.station->ipv4);
    CHECK(std::string_view{wifi.station->ipv4->data()} == "192.168.1.5");
    REQUIRE(wifi.access_point);
    CHECK(wifi.access_point->clients == 1);
    REQUIRE(wifi.access_point->ipv4);
    CHECK(std::string_view{wifi.access_point->ipv4->data()} == "192.168.4.1");
    // From the messaging info
    REQUIRE(telemetry.device.messaging.mqtt);
    CHECK(telemetry.device.messaging.mqtt->connected);
    CHECK(telemetry.device.messaging.mqtt->reconnects == 4);
    // From the runtime settings
    CHECK(telemetry.sensor.target == doctest::Approx(55.5f));
    CHECK(telemetry.sensor.deadband == doctest::Approx(0.5f));
    CHECK(telemetry.sensor.calibration.adc_min == 1200);
    CHECK(telemetry.sensor.calibration.adc_max == 2900);
    CHECK(telemetry.pump.duration_s == 5);
    CHECK(telemetry.pump.wait_time_s == 60);
    // From the sensor snapshot
    CHECK(telemetry.sensor.latest == doctest::Approx(40.25f));
    CHECK(telemetry.sensor.smoothed == doctest::Approx(42.0f));
    CHECK(telemetry.sensor.raw == 1234);
    CHECK(telemetry.sensor.ready);
    CHECK_FALSE(telemetry.sensor.faulted);
    // From the pump snapshot
    CHECK(telemetry.pump.on);
    CHECK_FALSE(telemetry.pump.faulted);
    // From the irrigation state
    CHECK(telemetry.pump.run_count == 3);
    CHECK(telemetry.pump.current_run_time_s == 4);
    CHECK(telemetry.pump.idle_time_s == 9);
    CHECK(telemetry.pump.remaining_wait_time_s == 30);
    CHECK(telemetry.pump.total_run_time_s == 12);
}

TEST_CASE("without WiFi the network info carries nothing") {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = US_PER_S,
        .pump_wait_time_us = 0,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"", "", "", "", "", "", false};

    CHECK_FALSE(
        assembler.assemble(runtime_settings, {}, {}, {}, {}, {}, {}, {}, {}, 0).device.network.wifi
    );
}

TEST_CASE("a station not yet joined carries no address, access point or reading") {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = US_PER_S,
        .pump_wait_time_us = 0,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"", "", "", "", "", "", false};
    const NetworkInfo network_info{
        .wifi = WifiInfo{
            .mode = "sta",
            .channel = std::nullopt,
            .station =
                WifiInfo::Station{
                    .mac = net_utils::MacAddress{0x00, 0x00, 0x5E, 0x00, 0x53, 0xAF},
                    .bssid = std::nullopt,
                    .rssi = std::nullopt,
                    .reconnects = 0,
                    .ipv4 = std::nullopt,
                },
            .access_point = std::nullopt,
        }
    };

    const Telemetry telemetry{
        assembler.assemble(runtime_settings, {}, {}, {}, network_info, {}, {}, {}, {}, 0)
    };

    REQUIRE(telemetry.device.network.wifi);
    const Telemetry::Wifi& wifi{*telemetry.device.network.wifi};
    CHECK_FALSE(wifi.channel);
    CHECK_FALSE(wifi.access_point);
    REQUIRE(wifi.station);
    CHECK(wifi.station->mac);
    CHECK_FALSE(wifi.station->bssid);
    CHECK_FALSE(wifi.station->rssi);
    CHECK_FALSE(wifi.station->ipv4);
}

TEST_CASE("without MQTT the messaging info carries nothing") {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = US_PER_S,
        .pump_wait_time_us = 0,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"", "", "", "", "", "", false};

    CHECK_FALSE(assembler.assemble(runtime_settings, {}, {}, {}, {}, {}, {}, {}, {}, 0)
                    .device.messaging.mqtt);
}

TEST_CASE("PSRAM in the heap is carried beside the internal RAM") {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = US_PER_S,
        .pump_wait_time_us = 0,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"", "", "", "", "", "", false};
    const HeapInfo heap_info{
        .internal = {},
        .psram = HeapUsage{
            .total = 5000,
            .free = 4000,
            .min_free = 3000,
            .largest_free_block = 2000,
            .free_blocks = 1
        },
    };

    const Telemetry telemetry{
        assembler.assemble(runtime_settings, {}, {}, {}, {}, {}, {}, heap_info, {}, 0)
    };

    REQUIRE(telemetry.device.heap.psram);
    CHECK(telemetry.device.heap.psram->total == 5000);
    CHECK(telemetry.device.heap.psram->free == 4000);
    CHECK(telemetry.device.heap.psram->min_free == 3000);
    CHECK(telemetry.device.heap.psram->largest_free_block == 2000);
    CHECK(telemetry.device.heap.psram->free_blocks == 1);
}

TEST_CASE("assemble carries the health each subsystem reports") {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = US_PER_S,
        .pump_wait_time_us = 0,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"", "", "", "", "", "", false};

    const SensorSnapshot sensor_snapshot{.ready = true, .faulted = true};
    const PumpSnapshot pump_snapshot{.on = false, .faulted = true};

    const Telemetry telemetry{assembler.assemble(
        runtime_settings, sensor_snapshot, pump_snapshot, {}, {}, {}, {}, {}, {}, 0
    )};

    CHECK(telemetry.sensor.ready);
    CHECK(telemetry.sensor.faulted);
    CHECK(telemetry.pump.faulted);
}

TEST_CASE("assemble copies the raised faults, code and flag only") {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = US_PER_S,
        .pump_wait_time_us = 0,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"", "", "", "", "", "", false};

    const std::array<FaultEntry, 2> faults{{
        {.fault = {.code = "ETST01", .severity = Severity::Error, .message = "e"}, .active = true},
        {.fault = {.code = "WTST01", .severity = Severity::Warning, .message = "w"},
         .active = false},
    }};

    const Telemetry telemetry{
        assembler.assemble(runtime_settings, {}, {}, {}, {}, {}, {}, {}, faults, 0)
    };

    REQUIRE(telemetry.fault_count == 2);
    CHECK(telemetry.faults[0].code == faults[0].fault.code);
    CHECK(telemetry.faults[0].active);
    CHECK(telemetry.faults[1].code == faults[1].fault.code);
    // A cleared fault is still copied, and the frame keeps the record.
    CHECK_FALSE(telemetry.faults[1].active);
}

TEST_CASE("each document carries the next sequence number, starting at 1") {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = US_PER_S,
        .pump_wait_time_us = 0,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"", "", "", "", "", "", false};

    CHECK(assembler.assemble(runtime_settings, {}, {}, {}, {}, {}, {}, {}, {}, 0).sequence == 1);
    CHECK(assembler.assemble(runtime_settings, {}, {}, {}, {}, {}, {}, {}, {}, 0).sequence == 2);
    CHECK(assembler.assemble(runtime_settings, {}, {}, {}, {}, {}, {}, {}, {}, 0).sequence == 3);
}

TEST_CASE("a document with no faults carries an empty list") {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = US_PER_S,
        .pump_wait_time_us = 0,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"", "", "", "", "", "", false};

    CHECK(assembler.assemble(runtime_settings, {}, {}, {}, {}, {}, {}, {}, {}, 0).fault_count == 0);
}

TEST_CASE("a span longer than a frame holds is cut to what fits") {
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = US_PER_S,
        .pump_wait_time_us = 0,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"", "", "", "", "", "", false};

    const std::array<FaultEntry, Telemetry::MAX_FAULTS + 1> faults{};

    CHECK(
        assembler.assemble(runtime_settings, {}, {}, {}, {}, {}, {}, {}, faults, 0).fault_count ==
        Telemetry::MAX_FAULTS
    );
}

TEST_CASE("assemble floors part seconds, but ceils the remaining wait time") {
    // Two and a half seconds, for every time the frame carries
    constexpr std::int64_t span_us{(5 * US_PER_S) / 2};
    const RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = 50.0f,
        .sensor_deadband = 0.0f,
        .sensor_calibration = {.adc_min = 1200, .adc_max = 2900},
        .pump_duration_us = span_us,
        .pump_wait_time_us = span_us,
        .pump_automatic = true
    }};
    TelemetryAssembler assembler{"", "", "", "", "", "", false};
    const IrrigationState irrigation_state{
        .current_run_time_us = span_us,
        .idle_time_us = span_us,
        .remaining_wait_time_us = span_us,
        .total_run_time_us = span_us
    };

    const Telemetry telemetry{
        assembler.assemble(runtime_settings, {}, {}, irrigation_state, {}, {}, {}, {}, {}, span_us)
    };

    CHECK(telemetry.device.uptime_s == 2);
    CHECK(telemetry.pump.current_run_time_s == 2);
    CHECK(telemetry.pump.idle_time_s == 2);
    CHECK(telemetry.pump.duration_s == 2);
    CHECK(telemetry.pump.wait_time_s == 2);
    CHECK(telemetry.pump.total_run_time_s == 2);
    // Reads 0 only once the pump is eligible, never a second early.
    CHECK(telemetry.pump.remaining_wait_time_s == 3);
}
