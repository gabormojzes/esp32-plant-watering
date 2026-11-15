#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <system_error>

#include "messaging/command.hpp"
#include "messaging/message_format.hpp"
#include "messaging/payload.hpp"
#include "messaging/telemetry.hpp"

// A format small enough to read at a glance. A frame is its uptime in decimal,
// and a command is a whole number that becomes the sensor target. Anything else
// is not a document in it.
struct FakeMessageFormat : MessageFormat {
    static constexpr char MEDIA_TYPE[]{"application/x-fake"};

    const char* media_type() const override {
        return MEDIA_TYPE;
    }

    [[nodiscard]] bool encode(const Telemetry& telemetry, Payload& payload) const override {
        payload.length = 0;
        if (refuses_frames) {
            return false;
        }
        char* const begin{payload.data.data()};
        const auto [end, error]{
            std::to_chars(begin, begin + payload.data.size(), telemetry.device.uptime_s)
        };
        if (error != std::errc{}) {
            return false;
        }
        payload.length = static_cast<int>(end - begin);
        return true;
    }

    std::optional<Command> decode(const Payload& payload) const override {
        const char* const begin{payload.data.data()};
        const char* const end{begin + payload.length};
        std::int64_t target{0};
        const auto [stop, error]{std::from_chars(begin, end, target)};
        if (error != std::errc{} || stop != end) {
            return std::nullopt;
        }
        Command command{};
        command.sensor.target = static_cast<double>(target);
        return command;
    }

    // Every frame is refused, as one the format cannot write
    bool refuses_frames{false};
};
