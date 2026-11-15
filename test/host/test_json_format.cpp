#include <doctest.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>

#include "messaging/json_codec.hpp"
#include "messaging/json_format.hpp"
#include "messaging/message_format.hpp"
#include "messaging/payload.hpp"
#include "messaging/telemetry.hpp"

namespace {

Payload make_payload(std::string_view text) {
    Payload payload{};
    payload.length = static_cast<int>(text.size());
    std::ranges::copy(text, payload.data.begin());
    return payload;
}

std::string_view text_of(const Payload& payload) {
    return {payload.data.data(), static_cast<std::size_t>(payload.length)};
}

}  // namespace

TEST_CASE("the format names JSON as its media type") {
    const JsonFormat json{};
    const MessageFormat& format{json};

    CHECK(std::string_view{format.media_type()} == "application/json");
}

TEST_CASE("a frame is encoded byte for byte as the codec lays it out") {
    const JsonFormat json{};
    const MessageFormat& format{json};
    const Telemetry telemetry{
        .device =
            {.name = "pot-3",
             .uptime_s = 5,
             .firmware = {},
             .chip = {.temperature = std::nullopt},
             .heap = {.internal = {}, .psram = std::nullopt},
             .network = {.wifi = std::nullopt},
             .messaging = {.mqtt = std::nullopt}},
        .sensor = {},
        .pump = {},
    };

    Payload encoded{};
    REQUIRE(format.encode(telemetry, encoded));
    Payload expected{};
    REQUIRE(json_codec::encode_telemetry(telemetry, expected));

    CHECK(text_of(encoded) == text_of(expected));
}

TEST_CASE("a frame the codec refuses is refused") {
    const JsonFormat json{};
    const MessageFormat& format{json};
    const Telemetry telemetry{
        .device =
            {.name = R"(pot "3")",
             .firmware = {},
             .chip = {.temperature = std::nullopt},
             .heap = {.internal = {}, .psram = std::nullopt},
             .network = {.wifi = std::nullopt},
             .messaging = {.mqtt = std::nullopt}},
        .sensor = {},
        .pump = {},
    };

    Payload payload{};
    CHECK_FALSE(format.encode(telemetry, payload));
    CHECK(payload.length == 0);
}

TEST_CASE("a command payload is decoded with its values as sent") {
    const JsonFormat json{};
    const MessageFormat& format{json};

    const std::optional<Command> command{
        format.decode(make_payload(R"({"sensor":{"target":42.25},"pump":{"duration":7}})"))
    };

    REQUIRE(command);
    REQUIRE(command->sensor.target);
    CHECK(*command->sensor.target == doctest::Approx(42.25));
    REQUIRE(command->pump.duration_s);
    CHECK(*command->pump.duration_s == doctest::Approx(7.0));
    CHECK_FALSE(command->sensor.deadband);
    CHECK_FALSE(command->pump.wait_time_s);
}

TEST_CASE("a payload that is not JSON yields no command") {
    const JsonFormat json{};
    const MessageFormat& format{json};

    CHECK_FALSE(format.decode(make_payload("pump.duration=7")));
}
