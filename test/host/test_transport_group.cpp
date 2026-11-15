#include <doctest.h>

#include <array>
#include <cstdint>
#include <optional>

#include "messaging/command.hpp"
#include "messaging/messaging_info.hpp"
#include "messaging/mqtt/mqtt_info.hpp"
#include "messaging/telemetry.hpp"
#include "messaging/transport_group.hpp"

namespace {

Command command_for(double target) {
    Command command{};
    command.sensor.target = target;
    return command;
}

Telemetry frame_at(std::int64_t uptime_s) {
    Telemetry telemetry{};
    telemetry.device.uptime_s = uptime_s;
    return telemetry;
}

struct TransportStub : Messaging {
    void start() override {
        started = true;
    }
    [[nodiscard]] std::optional<Command> poll() override {
        std::optional<Command> command{pending};
        pending.reset();
        return command;
    }
    void send(const Telemetry& telemetry) override {
        sent = telemetry;
    }
    void describe(MessagingInfo& messaging_info) const override {
        if (mqtt) {
            messaging_info.mqtt = mqtt;
        }
    }

    bool started{false};
    std::optional<Command> pending;
    std::optional<Telemetry> sent;
    std::optional<MqttInfo> mqtt;
};

}  // namespace

TEST_CASE("an empty group carries nothing") {
    TransportGroup group{{.transports = {}}};

    group.start();
    group.send(frame_at(1));

    CHECK_FALSE(group.poll());
    MessagingInfo messaging_info{};
    group.describe(messaging_info);
    CHECK_FALSE(messaging_info.mqtt);
}

TEST_CASE("describe gathers each transport's own part") {
    TransportStub silent{};
    TransportStub reporting{};
    reporting.mqtt = MqttInfo{.connected = true, .reconnects = 3};
    const std::array<Messaging*, 2> transports{&silent, &reporting};
    const TransportGroup group{{.transports = transports}};

    MessagingInfo messaging_info{};
    group.describe(messaging_info);

    REQUIRE(messaging_info.mqtt);
    CHECK(messaging_info.mqtt->connected);
    CHECK(messaging_info.mqtt->reconnects == 3);
}

TEST_CASE("start reaches every transport") {
    TransportStub first{};
    TransportStub second{};
    const std::array<Messaging*, 2> transports{&first, &second};
    TransportGroup group{{.transports = transports}};

    group.start();

    CHECK(first.started);
    CHECK(second.started);
}

TEST_CASE("a frame goes out over every transport unchanged") {
    TransportStub first{};
    TransportStub second{};
    const std::array<Messaging*, 2> transports{&first, &second};
    TransportGroup group{{.transports = transports}};

    group.send(frame_at(7));

    REQUIRE(first.sent);
    REQUIRE(second.sent);
    CHECK(first.sent->device.uptime_s == 7);
    CHECK(second.sent->device.uptime_s == 7);
}

TEST_CASE("polls take from the transports in list order") {
    TransportStub first{};
    TransportStub second{};
    first.pending = command_for(10.0);
    second.pending = command_for(20.0);
    const std::array<Messaging*, 2> transports{&first, &second};
    TransportGroup group{{.transports = transports}};

    const std::optional<Command> earlier{group.poll()};
    REQUIRE(earlier);
    CHECK(earlier->sensor.target == 10.0);

    const std::optional<Command> later{group.poll()};
    REQUIRE(later);
    CHECK(later->sensor.target == 20.0);

    CHECK_FALSE(group.poll());
}

TEST_CASE("a transport with nothing pending is skipped") {
    TransportStub empty{};
    TransportStub holding{};
    holding.pending = command_for(30.0);
    const std::array<Messaging*, 2> transports{&empty, &holding};
    TransportGroup group{{.transports = transports}};

    const std::optional<Command> command{group.poll()};

    REQUIRE(command);
    CHECK(command->sensor.target == 30.0);
}
