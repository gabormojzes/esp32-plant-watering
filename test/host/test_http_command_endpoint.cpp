#include <doctest.h>

#include <cstddef>
#include <string_view>

#include "fake_web_host.hpp"
#include "messaging/http/http_command_endpoint.hpp"
#include "messaging/payload.hpp"

namespace {

constexpr char URI[]{"/api/command"};
constexpr char JSON[]{"application/json"};

// Longer than the buffer the endpoint reads a content type into
constexpr char PADDED_JSON[]{
    "application/json;                                                                 "
    "                                                                x"
};

std::string_view text_of(const Payload& payload) {
    return {payload.data.data(), static_cast<std::size_t>(payload.length)};
}

// An endpoint that has claimed its routes on a host serving nothing
struct Fixture {
    Fixture() {
        endpoint.start();
    }

    FakeWebHost web_host{};
    HttpCommandEndpoint endpoint{web_host, URI, JSON};
};

// Posts a body and hands back what the route answered.
FakeHttpExchange post(
    FakeWebHost& web_host, std::string_view body, const char* content_type = JSON
) {
    FakeHttpExchange exchange{};
    exchange.request_content_type = content_type;
    exchange.request_body = body;
    REQUIRE(web_host.dispatch(URI, HttpMethod::Post, exchange));
    return exchange;
}

}  // namespace

TEST_CASE("starting claims the post route and its preflight") {
    const Fixture fixture{};

    CHECK(fixture.web_host.route_count() == 2);
    CHECK(fixture.web_host.claimed(URI, HttpMethod::Post));
    CHECK(fixture.web_host.claimed(URI, HttpMethod::Options));
    // Posts need the token or a session. Preflights carry no credentials.
    CHECK(
        fixture.web_host.access_of(URI, HttpMethod::Post) ==
        AccessSet{Access::Token, Access::SessionNotFromAnotherOrigin}
    );
    CHECK(fixture.web_host.access_of(URI, HttpMethod::Options) == AccessSet{Access::Open});
}

TEST_CASE("an accepted command is queued and taken back whole") {
    Fixture fixture{};
    Payload taken{};

    const FakeHttpExchange answer{post(fixture.web_host, R"({"pump":true})")};

    CHECK(answer.status == HttpStatus::Accepted);
    REQUIRE(fixture.endpoint.take(taken));
    CHECK(text_of(taken) == R"({"pump":true})");
}

TEST_CASE("commands are taken in the order they were posted") {
    Fixture fixture{};
    Payload taken{};

    post(fixture.web_host, R"({"a":1})");
    post(fixture.web_host, R"({"b":2})");

    REQUIRE(fixture.endpoint.take(taken));
    CHECK(text_of(taken) == R"({"a":1})");
    REQUIRE(fixture.endpoint.take(taken));
    CHECK(text_of(taken) == R"({"b":2})");
}

TEST_CASE("nothing is taken from an endpoint nobody posted to") {
    Fixture fixture{};
    Payload taken{};

    CHECK_FALSE(fixture.endpoint.take(taken));
}

TEST_CASE("a post past the queue's depth is refused and the earlier ones kept") {
    Fixture fixture{};
    Payload taken{};

    post(fixture.web_host, R"({"a":1})");
    post(fixture.web_host, R"({"b":2})");
    const FakeHttpExchange answer{post(fixture.web_host, R"({"c":3})")};

    CHECK(answer.status == HttpStatus::ServiceUnavailable);
    REQUIRE(fixture.endpoint.take(taken));
    CHECK(text_of(taken) == R"({"a":1})");
    REQUIRE(fixture.endpoint.take(taken));
    CHECK(text_of(taken) == R"({"b":2})");
    CHECK_FALSE(fixture.endpoint.take(taken));
}

TEST_CASE("a post carrying no content type is refused") {
    Fixture fixture{};
    Payload taken{};

    const FakeHttpExchange answer{post(fixture.web_host, R"({"a":1})", nullptr)};

    CHECK(answer.status == HttpStatus::UnsupportedMediaType);
    CHECK_FALSE(fixture.endpoint.take(taken));
}

TEST_CASE("a content type that only starts like JSON is refused") {
    Fixture fixture{};

    const FakeHttpExchange answer{post(fixture.web_host, R"({"a":1})", "application/jsonl")};

    CHECK(answer.status == HttpStatus::UnsupportedMediaType);
}

TEST_CASE("a JSON content type carrying parameters is accepted") {
    Fixture fixture{};

    const FakeHttpExchange answer{
        post(fixture.web_host, R"({"a":1})", "application/json; charset=utf-8")
    };

    CHECK(answer.status == HttpStatus::Accepted);
}

TEST_CASE("a JSON content type is accepted in any letter case") {
    Fixture fixture{};

    const FakeHttpExchange answer{post(fixture.web_host, R"({"a":1})", "Application/JSON")};

    CHECK(answer.status == HttpStatus::Accepted);
}

// httpd strips the spaces ahead of a value but hands a tab over.
TEST_CASE("a JSON content type behind a leading tab is accepted") {
    Fixture fixture{};

    const FakeHttpExchange answer{post(fixture.web_host, R"({"a":1})", "\tapplication/json")};

    CHECK(answer.status == HttpStatus::Accepted);
}

TEST_CASE("a post is held to the media type the endpoint was given, not to JSON") {
    FakeWebHost web_host{};
    HttpCommandEndpoint endpoint{web_host, URI, "application/cbor"};
    endpoint.start();
    Payload taken{};

    CHECK(post(web_host, "a1", "application/cbor").status == HttpStatus::Accepted);
    CHECK(post(web_host, R"({"a":1})", JSON).status == HttpStatus::UnsupportedMediaType);
    REQUIRE(endpoint.take(taken));
    CHECK(text_of(taken) == "a1");
    CHECK_FALSE(endpoint.take(taken));
}

TEST_CASE("a content type too long to fit is refused rather than truncated in") {
    Fixture fixture{};

    const FakeHttpExchange answer{post(fixture.web_host, R"({"a":1})", PADDED_JSON)};

    CHECK(answer.status == HttpStatus::UnsupportedMediaType);
}

TEST_CASE("a body longer than a payload is refused before it is read") {
    Fixture fixture{};
    Payload taken{};

    FakeHttpExchange exchange{};
    exchange.request_content_type = JSON;
    exchange.declared_length = Payload::BUFFER_SIZE + 1;
    REQUIRE(fixture.web_host.dispatch(URI, HttpMethod::Post, exchange));

    CHECK(exchange.status == HttpStatus::ContentTooLarge);
    CHECK_FALSE(fixture.endpoint.take(taken));
}

TEST_CASE("a body filling a payload exactly is accepted") {
    Fixture fixture{};
    Payload taken{};

    FakeHttpExchange exchange{};
    exchange.request_content_type = JSON;
    exchange.declared_length = Payload::BUFFER_SIZE;
    REQUIRE(fixture.web_host.dispatch(URI, HttpMethod::Post, exchange));

    CHECK(exchange.status == HttpStatus::Accepted);
    REQUIRE(fixture.endpoint.take(taken));
    CHECK(taken.length == static_cast<int>(Payload::BUFFER_SIZE));
}

TEST_CASE("a client that goes quiet mid-body is told it timed out") {
    Fixture fixture{};
    Payload taken{};

    FakeHttpExchange exchange{};
    exchange.request_content_type = JSON;
    exchange.request_body = R"({"a":1})";
    exchange.body_outcome = BodyResult::TimedOut;
    REQUIRE(fixture.web_host.dispatch(URI, HttpMethod::Post, exchange));

    CHECK(exchange.status == HttpStatus::RequestTimeout);
    CHECK_FALSE(fixture.endpoint.take(taken));
}

TEST_CASE("a body that cannot be read is a bad request") {
    Fixture fixture{};
    Payload taken{};

    FakeHttpExchange exchange{};
    exchange.request_content_type = JSON;
    exchange.request_body = R"({"a":1})";
    exchange.body_outcome = BodyResult::Failed;
    REQUIRE(fixture.web_host.dispatch(URI, HttpMethod::Post, exchange));

    CHECK(exchange.status == HttpStatus::BadRequest);
    CHECK_FALSE(fixture.endpoint.take(taken));
}

TEST_CASE("the answer is offered to other origins") {
    Fixture fixture{};

    const FakeHttpExchange answer{post(fixture.web_host, R"({"a":1})")};

    CHECK(answer.header_value("Access-Control-Allow-Origin") == "*");
}

TEST_CASE("the preflight allows the post") {
    Fixture fixture{};
    FakeHttpExchange exchange{};

    REQUIRE(fixture.web_host.dispatch(URI, HttpMethod::Options, exchange));

    CHECK(exchange.status == HttpStatus::NoContent);
    CHECK(exchange.header_value("Access-Control-Allow-Methods") == "POST, OPTIONS");
    CHECK(exchange.header_value("Access-Control-Allow-Headers") == "Content-Type, Authorization");
}
