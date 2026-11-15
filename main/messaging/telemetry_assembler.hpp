#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "fault/fault.hpp"
#include "hardware/pump_snapshot.hpp"
#include "heap_info.hpp"
#include "irrigation_state.hpp"
#include "messaging/messaging_info.hpp"
#include "messaging/telemetry.hpp"
#include "network/network_info.hpp"
#include "network/wifi_info.hpp"
#include "runtime_settings.hpp"
#include "sensor_snapshot.hpp"
#include "shared/net_utils.hpp"
#include "shared/time_utils.hpp"

class TelemetryAssembler {
public:
    // The frame is sized for strings no longer than these.

    // Longest device name, terminator included
    static constexpr std::size_t MAX_DEVICE_NAME_SIZE{32};
    // Longest firmware version, terminator included
    static constexpr std::size_t MAX_FIRMWARE_VERSION_SIZE{17};
    // Longest firmware build id, terminator included: every hex digit of a SHA-256
    static constexpr std::size_t MAX_FIRMWARE_BUILD_SIZE{65};

    TelemetryAssembler(
        const char* device_name,
        const char* firmware_version,
        const char* firmware_build,
        const char* chip_model,
        const char* chip_revision,
        const char* reset_reason,
        bool core_dump
    )
        : device_name_{device_name},
          firmware_version_{firmware_version},
          firmware_build_{firmware_build},
          chip_model_{chip_model},
          chip_revision_{chip_revision},
          reset_reason_{reset_reason},
          core_dump_{core_dump} {}

    [[nodiscard]] Telemetry assemble(
        const RuntimeSettings& runtime_settings,
        const SensorSnapshot& sensor_snapshot,
        const PumpSnapshot& pump_snapshot,
        const IrrigationState& irrigation_state,
        const NetworkInfo& network_info,
        const MessagingInfo& messaging_info,
        std::optional<float> chip_temperature,
        const HeapInfo& heap_info,
        std::span<const FaultEntry> faults,
        std::int64_t uptime_us
    ) {
        Telemetry telemetry{
            .sequence = ++sequence_,
            .device =
                {
                    .name = device_name_,
                    .uptime_s = time_utils::to_seconds_floor(uptime_us),
                    .reset_reason = reset_reason_,
                    .core_dump = core_dump_,
                    .firmware = {.version = firmware_version_, .build = firmware_build_},
                    .chip =
                        {
                            .model = chip_model_,
                            .revision = chip_revision_,
                            .temperature = chip_temperature,
                        },
                    .heap = heap(heap_info),
                    .network = network(network_info),
                    .messaging = messaging(messaging_info),
                },
            .sensor =
                {
                    .target = runtime_settings.sensor_target(),
                    .deadband = runtime_settings.sensor_deadband(),
                    .calibration =
                        {
                            .adc_min = runtime_settings.sensor_calibration().adc_min,
                            .adc_max = runtime_settings.sensor_calibration().adc_max,
                        },
                    .latest = sensor_snapshot.latest,
                    .smoothed = sensor_snapshot.smoothed,
                    .raw = sensor_snapshot.raw,
                    .ready = sensor_snapshot.ready,
                    .faulted = sensor_snapshot.faulted,
                },
            .pump = {
                .run_count = irrigation_state.run_count,
                .on = pump_snapshot.on,
                .current_run_time_s =
                    time_utils::to_seconds_floor(irrigation_state.current_run_time_us),
                .idle_time_s = time_utils::to_seconds_floor(irrigation_state.idle_time_us),
                .automatic = runtime_settings.pump_automatic(),
                .duration_s = time_utils::to_seconds_floor(runtime_settings.pump_duration()),
                .wait_time_s = time_utils::to_seconds_floor(runtime_settings.pump_wait_time()),
                // Rounded up, which makes it read 0 exactly when the pump
                // becomes eligible, never a second early.
                .remaining_wait_time_s =
                    time_utils::to_seconds_ceil(irrigation_state.remaining_wait_time_us),
                .total_run_time_s =
                    time_utils::to_seconds_floor(irrigation_state.total_run_time_us),
                .faulted = pump_snapshot.faulted,
            },
        };
        telemetry.fault_count = std::min(faults.size(), Telemetry::MAX_FAULTS);
        for (std::size_t i{0}; i < telemetry.fault_count; ++i) {
            telemetry.faults[i] = {
                .code = faults[i].fault.code,
                .active = faults[i].active,
            };
        }
        return telemetry;
    }

private:
    static constexpr Telemetry::HeapUsage heap_usage(const HeapUsage& usage) {
        return {
            .total = usage.total,
            .free = usage.free,
            .min_free = usage.min_free,
            .largest_free_block = usage.largest_free_block,
            .free_blocks = usage.free_blocks,
        };
    }

    static constexpr Telemetry::Heap heap(const HeapInfo& heap_info) {
        return {
            .internal = heap_usage(heap_info.internal),
            .psram = heap_info.psram.transform(heap_usage),
        };
    }

    static constexpr Telemetry::Station station(const WifiInfo::Station& station) {
        return {
            .mac = station.mac.transform(net_utils::format_mac),
            .bssid = station.bssid.transform(net_utils::format_mac),
            .rssi = station.rssi,
            .reconnects = station.reconnects,
            .ipv4 = station.ipv4.transform(net_utils::format_ipv4),
        };
    }

    static constexpr Telemetry::AccessPoint access_point(
        const WifiInfo::AccessPoint& access_point
    ) {
        return {
            .clients = access_point.clients,
            .ipv4 = access_point.ipv4.transform(net_utils::format_ipv4),
        };
    }

    static constexpr Telemetry::Network network(const NetworkInfo& network_info) {
        if (!network_info.wifi) {
            return {};
        }
        const WifiInfo& wifi{*network_info.wifi};
        return {
            .wifi = Telemetry::Wifi{
                .mode = wifi.mode,
                .channel = wifi.channel,
                .station = wifi.station.transform(station),
                .access_point = wifi.access_point.transform(access_point),
            }
        };
    }

    static constexpr Telemetry::Messaging messaging(const MessagingInfo& messaging_info) {
        if (!messaging_info.mqtt) {
            return {};
        }
        return {
            .mqtt = Telemetry::Mqtt{
                .connected = messaging_info.mqtt->connected,
                .reconnects = messaging_info.mqtt->reconnects,
            }
        };
    }

    const char* device_name_{nullptr};
    const char* firmware_version_{nullptr};
    const char* firmware_build_{nullptr};
    const char* chip_model_{nullptr};
    const char* chip_revision_{nullptr};
    const char* reset_reason_{nullptr};
    bool core_dump_{false};
    std::uint64_t sequence_{0};
};
