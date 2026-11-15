#include <doctest.h>

#include <cstdint>
#include <optional>
#include <string_view>

#include "fake_message_format.hpp"
#include "fake_web_host.hpp"
#include "messaging/command.hpp"
#include "messaging/http/http_transport.hpp"
#include "messaging/telemetry.hpp"

namespace {

// The paths the transport claims, which are fixed rather than configured
constexpr char TELEMETRY_URI[]{"/api/telemetry"};
constexpr char COMMAND_URI[]{"/api/command"};

Telemetry frame_at(std::int64_t uptime_s) {
    Telemetry telemetry{};
    telemetry.device.uptime_s = uptime_s;
    return telemetry;
}

// A transport that has claimed its routes on a host serving nothing
struct Fixture {
    Fixture() {
        transport.start();
    }

    void post(const char* content_type, std::string_view body) {
        FakeHttpExchange exchange{};
        exchange.request_content_type = content_type;
        exchange.request_body = body;
        REQUIRE(web_host.dispatch(COMMAND_URI, HttpMethod::Post, exchange));
    }

    FakeWebHost web_host{};
    FakeMessageFormat format{};
    HttpTransport transport{web_host, format};
};

}  // namespace

TEST_CASE("starting claims a route and a preflight for each endpoint") {
    const Fixture fixture{};

    CHECK(fixture.web_host.route_count() == 4);
    CHECK(fixture.web_host.claimed(COMMAND_URI, HttpMethod::Post));
    CHECK(fixture.web_host.claimed(COMMAND_URI, HttpMethod::Options));
    CHECK(fixture.web_host.claimed(TELEMETRY_URI, HttpMethod::Get));
    CHECK(fixture.web_host.claimed(TELEMETRY_URI, HttpMethod::Options));
}

TEST_CASE("a posted command is polled back decoded") {
    Fixture fixture{};

    fixture.post(FakeMessageFormat::MEDIA_TYPE, "42");

    const std::optional<Command> command{fixture.transport.poll()};
    REQUIRE(command);
    REQUIRE(command->sensor.target);
    CHECK(*command->sensor.target == doctest::Approx(42.0));
}

TEST_CASE("a posted payload the format cannot read is skipped") {
    Fixture fixture{};

    fixture.post(FakeMessageFormat::MEDIA_TYPE, "forty-two");
    fixture.post(FakeMessageFormat::MEDIA_TYPE, "17");

    const std::optional<Command> command{fixture.transport.poll()};
    REQUIRE(command);
    REQUIRE(command->sensor.target);
    CHECK(*command->sensor.target == doctest::Approx(17.0));
}

TEST_CASE("a post in another media type than the format's is refused") {
    Fixture fixture{};

    FakeHttpExchange exchange{};
    exchange.request_content_type = "application/json";
    exchange.request_body = "42";
    REQUIRE(fixture.web_host.dispatch(COMMAND_URI, HttpMethod::Post, exchange));

    CHECK(exchange.status == HttpStatus::UnsupportedMediaType);
    CHECK_FALSE(fixture.transport.poll());
}

TEST_CASE("a sent frame is served off the telemetry route as the format spells it") {
    Fixture fixture{};

    fixture.transport.send(frame_at(41));

    FakeHttpExchange exchange{};
    REQUIRE(fixture.web_host.dispatch(TELEMETRY_URI, HttpMethod::Get, exchange));
    CHECK(exchange.sent_text() == "41");
    CHECK(std::string_view{exchange.response_media_type} == FakeMessageFormat::MEDIA_TYPE);
}

TEST_CASE("a frame the format refuses is not served") {
    Fixture fixture{};
    fixture.format.refuses_frames = true;

    fixture.transport.send(frame_at(41));

    FakeHttpExchange exchange{};
    REQUIRE(fixture.web_host.dispatch(TELEMETRY_URI, HttpMethod::Get, exchange));
    CHECK(exchange.status == HttpStatus::ServiceUnavailable);
}

TEST_CASE("a transport nobody posted to polls nothing") {
    Fixture fixture{};

    CHECK_FALSE(fixture.transport.poll());
}
