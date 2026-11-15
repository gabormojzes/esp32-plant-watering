#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "fault/fault_definitions.hpp"
#include "shared/net_utils.hpp"

// One frame as it is sent, with every value already in the units and spellings
// the wire carries
struct Telemetry {
    struct Station {
        std::optional<net_utils::MacText> mac;
        std::optional<net_utils::MacText> bssid;
        std::optional<std::int8_t> rssi;
        std::optional<std::uint32_t> reconnects;
        std::optional<net_utils::Ipv4Text> ipv4;
    };

    struct AccessPoint {
        std::uint8_t clients{0};
        std::optional<net_utils::Ipv4Text> ipv4;
    };

    struct Wifi {
        const char* mode{""};
        std::optional<std::uint8_t> channel;
        std::optional<Station> station;
        std::optional<AccessPoint> access_point;
    };

    // Each part is empty when its provider is not built in.
    struct Network {
        std::optional<Wifi> wifi;
    };

    struct Mqtt {
        bool connected{false};
        std::uint32_t reconnects{0};
    };

    // Each part is empty when its transport is not built in.
    struct Messaging {
        std::optional<Mqtt> mqtt;
    };

    struct HeapUsage {
        std::uint32_t total{0};
        std::uint32_t free{0};
        std::uint32_t min_free{0};
        std::uint32_t largest_free_block{0};
        std::uint32_t free_blocks{0};
    };

    struct Heap {
        HeapUsage internal{};
        std::optional<HeapUsage> psram;
    };

    struct Firmware {
        const char* version{""};
        const char* build{""};
    };

    struct Chip {
        const char* model{""};
        const char* revision{""};
        std::optional<float> temperature;
    };

    struct Device {
        const char* name{""};
        std::int64_t uptime_s{0};
        const char* reset_reason{""};
        bool core_dump{false};
        Firmware firmware{};
        Chip chip{};
        Heap heap{};
        Network network{};
        Messaging messaging{};
    };

    struct Calibration {
        std::uint16_t adc_min{0};
        std::uint16_t adc_max{0};
    };

    struct Sensor {
        float target{0.0f};
        float deadband{0.0f};
        Calibration calibration{};
        float latest{0.0f};
        float smoothed{0.0f};
        std::uint32_t raw{0};
        bool ready{false};
        bool faulted{false};
    };

    struct Pump {
        std::uint32_t run_count{0};
        bool on{false};
        std::int64_t current_run_time_s{0};
        std::int64_t idle_time_s{0};
        bool automatic{false};
        std::int64_t duration_s{0};
        std::int64_t wait_time_s{0};
        std::int64_t remaining_wait_time_s{0};
        std::int64_t total_run_time_s{0};
        bool faulted{false};
    };

    // Room for every defined fault, which keeps a frame from carrying a partial
    // list.
    static constexpr std::size_t MAX_FAULTS{fault_definitions::ALL.size()};

    struct Fault {
        const char* code{nullptr};
        bool active{false};
    };

    std::uint64_t sequence{0};
    Device device{};
    Sensor sensor{};
    Pump pump{};
    std::array<Fault, MAX_FAULTS> faults{};
    std::size_t fault_count{0};
};
