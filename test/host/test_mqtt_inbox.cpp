#include <doctest.h>
#include <mqtt_client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

#include "messaging/mqtt/mqtt_client.hpp"
#include "messaging/mqtt/mqtt_inbox.hpp"
#include "messaging/payload.hpp"

namespace {

constexpr char TOPIC[]{"home/device1/command"};
constexpr MqttClient::Config CLIENT_CONFIG{
    .protocol = "mqtt",
    .broker = "broker.example",
    .port = 1883,
    .client_id = "device1",
    .user = "",
    .password = "",
    .status_topic = "home/device1/status",
    .tls = nullptr,
};
constexpr std::string_view FIRST{R"({"pump":{"run":true}})"};
constexpr std::string_view SECOND{R"({"pump":{"stop":true}})"};
constexpr std::string_view THIRD{R"({"pump":{"duration":20}})"};

std::string_view text_of(const Payload& payload) {
    return {payload.data.data(), static_cast<std::size_t>(payload.length)};
}

// An inbox on a started client, which a test delivers messages through
struct Fixture {
    Fixture() {
        inbox.start();
        client.start();
    }

    // One whole message, as esp-mqtt hands it over
    void deliver(std::string_view topic, std::string_view payload, bool retained) {
        REQUIRE(topic.size() <= topic_text.size());
        REQUIRE(payload.size() <= data.size());
        std::ranges::copy(topic, topic_text.begin());
        std::ranges::copy(payload, data.begin());
        esp_mqtt_event_t event{};
        event.event_id = MQTT_EVENT_DATA;
        event.topic = topic_text.data();
        event.topic_len = static_cast<int>(topic.size());
        event.data = data.data();
        event.data_len = static_cast<int>(payload.size());
        event.total_data_len = event.data_len;
        event.retain = retained;
        mqtt_stub_emit(event);
    }

    MqttClient client{CLIENT_CONFIG};
    MqttInbox inbox{client, TOPIC};
    Payload payload{};
    // Where a delivered message sits, writable as esp-mqtt hands it over
    std::array<char, MqttClient::MAX_TOPIC_SIZE> topic_text{};
    std::array<char, Payload::BUFFER_SIZE + 1> data{};
};

}  // namespace

TEST_CASE("an inbox that was never started has nothing to take") {
    MqttClient client{CLIENT_CONFIG};
    MqttInbox inbox{client, TOPIC};
    Payload payload{};
    CHECK_FALSE(inbox.take(payload));
}

TEST_CASE("a message published while subscribed is taken as it was sent") {
    Fixture fixture{};
    fixture.deliver(TOPIC, FIRST, false);
    REQUIRE(fixture.inbox.take(fixture.payload));
    CHECK(text_of(fixture.payload) == FIRST);
    CHECK_FALSE(fixture.inbox.take(fixture.payload));
}

TEST_CASE("messages are taken in the order they arrived") {
    Fixture fixture{};
    fixture.deliver(TOPIC, FIRST, false);
    fixture.deliver(TOPIC, SECOND, false);
    REQUIRE(fixture.inbox.take(fixture.payload));
    CHECK(text_of(fixture.payload) == FIRST);
    REQUIRE(fixture.inbox.take(fixture.payload));
    CHECK(text_of(fixture.payload) == SECOND);
}

TEST_CASE("a retained message is dropped") {
    Fixture fixture{};
    fixture.deliver(TOPIC, FIRST, true);
    CHECK_FALSE(fixture.inbox.take(fixture.payload));
}

TEST_CASE("a retained message takes no room from the ones published after it") {
    Fixture fixture{};
    fixture.deliver(TOPIC, FIRST, true);
    fixture.deliver(TOPIC, SECOND, false);
    fixture.deliver(TOPIC, THIRD, false);
    REQUIRE(fixture.inbox.take(fixture.payload));
    CHECK(text_of(fixture.payload) == SECOND);
    REQUIRE(fixture.inbox.take(fixture.payload));
    CHECK(text_of(fixture.payload) == THIRD);
}

TEST_CASE("a message on another topic is not taken") {
    Fixture fixture{};
    fixture.deliver("home/device1/other", FIRST, false);
    CHECK_FALSE(fixture.inbox.take(fixture.payload));
}

TEST_CASE("a payload that fills the buffer is taken whole") {
    Fixture fixture{};
    std::array<char, Payload::BUFFER_SIZE> bytes{};
    bytes.fill('x');
    const std::string_view full{bytes.data(), bytes.size()};
    fixture.deliver(TOPIC, full, false);
    REQUIRE(fixture.inbox.take(fixture.payload));
    CHECK(text_of(fixture.payload) == full);
}

TEST_CASE("a payload longer than the buffer is dropped") {
    Fixture fixture{};
    std::array<char, Payload::BUFFER_SIZE + 1> bytes{};
    bytes.fill('x');
    const std::string_view too_long{bytes.data(), bytes.size()};
    fixture.deliver(TOPIC, too_long, false);
    CHECK_FALSE(fixture.inbox.take(fixture.payload));
}

TEST_CASE("a full inbox drops the newest message and keeps the earlier ones") {
    Fixture fixture{};
    fixture.deliver(TOPIC, FIRST, false);
    fixture.deliver(TOPIC, SECOND, false);
    fixture.deliver(TOPIC, THIRD, false);
    REQUIRE(fixture.inbox.take(fixture.payload));
    CHECK(text_of(fixture.payload) == FIRST);
    REQUIRE(fixture.inbox.take(fixture.payload));
    CHECK(text_of(fixture.payload) == SECOND);
    CHECK_FALSE(fixture.inbox.take(fixture.payload));
}

TEST_CASE("room freed by a take is filled by the next message") {
    Fixture fixture{};
    fixture.deliver(TOPIC, FIRST, false);
    fixture.deliver(TOPIC, SECOND, false);
    REQUIRE(fixture.inbox.take(fixture.payload));
    fixture.deliver(TOPIC, THIRD, false);
    REQUIRE(fixture.inbox.take(fixture.payload));
    CHECK(text_of(fixture.payload) == SECOND);
    REQUIRE(fixture.inbox.take(fixture.payload));
    CHECK(text_of(fixture.payload) == THIRD);
}
