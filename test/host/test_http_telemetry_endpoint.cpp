#include <doctest.h>

#include <algorithm>
#include <string_view>

#include "fake_web_host.hpp"
#include "messaging/http/http_telemetry_endpoint.hpp"
#include "messaging/payload.hpp"

namespace {

constexpr char URI[]{"/api/telemetry"};
constexpr char JSON[]{"application/json"};

Payload make_payload(std::string_view text) {
    Payload payload{};
    payload.length = static_cast<int>(text.size());
    std::ranges::copy(text, payload.data.begin());
    return payload;
}

// An endpoint that has claimed its routes on a host serving nothing
struct Fixture {
    Fixture() {
        endpoint.start();
    }

    FakeWebHost web_host{};
    HttpTelemetryEndpoint endpoint{web_host, URI, JSON};
};

// Asks for the frame and hands back what the route answered.
FakeHttpExchange get(FakeWebHost& web_host) {
    FakeHttpExchange exchange{};
    REQUIRE(web_host.dispatch(URI, HttpMethod::Get, exchange));
    return exchange;
}

}  // namespace

TEST_CASE("starting claims the get route and its preflight") {
    const Fixture fixture{};

    CHECK(fixture.web_host.route_count() == 2);
    CHECK(fixture.web_host.claimed(URI, HttpMethod::Get));
    CHECK(fixture.web_host.claimed(URI, HttpMethod::Options));
    // Reads need the token or a session. Preflights carry no credentials.
    CHECK(
        fixture.web_host.access_of(URI, HttpMethod::Get) ==
        AccessSet{Access::Token, Access::SessionNotFromAnotherOrigin}
    );
    CHECK(fixture.web_host.access_of(URI, HttpMethod::Options) == AccessSet{Access::Open});
}

TEST_CASE("a device that has collected nothing yet says it has nothing") {
    Fixture fixture{};

    const FakeHttpExchange answer{get(fixture.web_host)};

    CHECK(answer.status == HttpStatus::ServiceUnavailable);
    CHECK(std::string_view{answer.response_media_type} == "text/plain");
    CHECK(answer.sent_text() == "No telemetry collected yet");
}

TEST_CASE("a collected frame is served as JSON") {
    Fixture fixture{};

    fixture.endpoint.send(make_payload(R"({"moisture":41})"));
    const FakeHttpExchange answer{get(fixture.web_host)};

    CHECK_FALSE(answer.status);
    CHECK(std::string_view{answer.response_media_type} == "application/json");
    CHECK(answer.sent_text() == R"({"moisture":41})");
}

TEST_CASE("a frame is served as the media type the endpoint was given, not as JSON") {
    FakeWebHost web_host{};
    HttpTelemetryEndpoint endpoint{web_host, URI, "application/cbor"};
    endpoint.start();

    endpoint.send(make_payload("a1"));
    const FakeHttpExchange answer{get(web_host)};

    CHECK(std::string_view{answer.response_media_type} == "application/cbor");
    CHECK(answer.sent_text() == "a1");
}

TEST_CASE("the newest frame replaces one nobody read") {
    Fixture fixture{};

    fixture.endpoint.send(make_payload(R"({"moisture":41})"));
    fixture.endpoint.send(make_payload(R"({"moisture":42})"));

    CHECK(get(fixture.web_host).sent_text() == R"({"moisture":42})");
}

TEST_CASE("a served frame stays for whoever asks next") {
    Fixture fixture{};

    fixture.endpoint.send(make_payload(R"({"moisture":41})"));

    CHECK(get(fixture.web_host).sent_text() == R"({"moisture":41})");
    CHECK(get(fixture.web_host).sent_text() == R"({"moisture":41})");
}

TEST_CASE("a frame filling a payload is served whole") {
    Fixture fixture{};
    Payload full{};
    full.length = static_cast<int>(Payload::BUFFER_SIZE);
    std::ranges::fill(full.data, 'x');

    fixture.endpoint.send(full);

    CHECK(get(fixture.web_host).sent_text().size() == Payload::BUFFER_SIZE);
}

TEST_CASE("the frame is offered to other origins") {
    Fixture fixture{};

    fixture.endpoint.send(make_payload(R"({"moisture":41})"));

    CHECK(get(fixture.web_host).header_value("Access-Control-Allow-Origin") == "*");
}

TEST_CASE("the preflight allows the read") {
    Fixture fixture{};
    FakeHttpExchange exchange{};

    REQUIRE(fixture.web_host.dispatch(URI, HttpMethod::Options, exchange));

    CHECK(exchange.status == HttpStatus::NoContent);
    CHECK(exchange.header_value("Access-Control-Allow-Methods") == "GET, OPTIONS");
}
