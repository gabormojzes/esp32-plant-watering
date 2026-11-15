#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "fake_web_host.hpp"
#include "web/access_gate.hpp"
#include "web/bearer_auth.hpp"
#include "web/http_exchange.hpp"
#include "web/session_cookie.hpp"
#include "web/session_store.hpp"

namespace {

constexpr char TOKEN[]{"s3cret-token"};
constexpr char LOGIN_URI[]{"/login"};
constexpr AccessSet API{Access::Token, Access::SessionNotFromAnotherOrigin};

using Id = std::array<char, SessionStore::ID_BUFFER_SIZE>;
// The cookie's name, "=" and an id, and the terminator
constexpr std::size_t SESSION_COOKIE_BUFFER_SIZE{
    std::string_view{session_cookie::NAME}.size() + SessionStore::ID_BUFFER_SIZE + 1
};
using SessionCookie = std::array<char, SESSION_COOKIE_BUFFER_SIZE>;

struct Fixture {
    Id open_session() {
        Id id{};
        session_store.open(id);
        return id;
    }

    BearerAuth bearer_auth{BearerAuth::Config{.api_token = TOKEN}};
    SessionStore session_store{};
    AccessGate gate{{.login_uri = LOGIN_URI}, bearer_auth, session_store};
};

SessionCookie build_session_cookie(std::string_view id) {
    SessionCookie cookie{};
    std::snprintf(
        cookie.data(),
        cookie.size(),
        "%s=%.*s",
        session_cookie::NAME,
        static_cast<int>(id.size()),
        id.data()
    );
    return cookie;
}

}  // namespace

TEST_CASE("an open route is reached by anything") {
    const Fixture fixture{};
    const FakeHttpExchange exchange{};

    CHECK(fixture.gate.granted({Access::Open}, exchange));
}

TEST_CASE("a route taking nothing is reached by nothing") {
    Fixture fixture{};
    const SessionCookie cookie{build_session_cookie(fixture.open_session().data())};
    FakeHttpExchange exchange{};
    exchange.set_request_header("Authorization", "Bearer s3cret-token");
    exchange.set_request_header("Cookie", cookie.data());

    CHECK_FALSE(fixture.gate.granted({}, exchange));
}

TEST_CASE("a route behind a session") {
    Fixture fixture{};
    const Id id{fixture.open_session()};
    FakeHttpExchange exchange{};

    SUBCASE("is refused without a cookie") {
        CHECK_FALSE(fixture.gate.granted({Access::Session}, exchange));
    }
    SUBCASE("is reached with the session's own cookie") {
        const SessionCookie header{build_session_cookie(id.data())};
        exchange.set_request_header("Cookie", header.data());
        CHECK(fixture.gate.granted({Access::Session}, exchange));
    }
    SUBCASE("is refused with a cookie naming no session") {
        const SessionCookie header{build_session_cookie("ffffffffffffffffffffffffffffffff")};
        exchange.set_request_header("Cookie", header.data());
        CHECK_FALSE(fixture.gate.granted({Access::Session}, exchange));
    }
    SUBCASE("is refused once the session is closed") {
        const SessionCookie header{build_session_cookie(id.data())};
        exchange.set_request_header("Cookie", header.data());
        fixture.session_store.close(id.data());
        CHECK_FALSE(fixture.gate.granted({Access::Session}, exchange));
    }
    // The token opens the API, not the page.
    SUBCASE("is refused with the API token") {
        exchange.set_request_header("Authorization", "Bearer s3cret-token");
        CHECK_FALSE(fixture.gate.granted({Access::Session}, exchange));
    }
    SUBCASE("is reached with other cookies filling the header up to the buffer") {
        const SessionCookie session{build_session_cookie(id.data())};
        const std::string_view tail{session.data()};
        // "other=", padding, "; " and the session, one short of the buffer
        std::array<char, session_cookie::BUFFER_SIZE> header{};
        header.fill('x');
        std::ranges::copy(std::string_view{"other="}, header.begin());
        const std::size_t tail_at{header.size() - 1 - tail.size()};
        std::ranges::copy(std::string_view{"; "}, header.begin() + tail_at - 2);
        std::ranges::copy(tail, header.begin() + tail_at);
        header.back() = '\0';
        exchange.set_request_header("Cookie", header.data());
        CHECK(fixture.gate.granted({Access::Session}, exchange));
    }
    SUBCASE("is refused with a cookie header past the buffer") {
        const SessionCookie session{build_session_cookie(id.data())};
        const std::string_view tail{session.data()};
        constexpr std::string_view padding_name{"; padding="};
        // The session, then padding running past the buffer
        std::array<char, session_cookie::BUFFER_SIZE + 64> header{};
        header.fill('x');
        std::ranges::copy(tail, header.begin());
        std::ranges::copy(padding_name, header.begin() + tail.size());
        header.back() = '\0';
        exchange.set_request_header("Cookie", header.data());
        CHECK_FALSE(fixture.gate.granted({Access::Session}, exchange));
    }
}

TEST_CASE("a route behind a session no other origin may use") {
    Fixture fixture{};
    const Id id{fixture.open_session()};
    const SessionCookie cookie{build_session_cookie(id.data())};
    FakeHttpExchange exchange{};

    SUBCASE("is refused without a cookie") {
        CHECK_FALSE(fixture.gate.granted({Access::SessionNotFromAnotherOrigin}, exchange));
    }
    SUBCASE("is reached on a request the device's own page made") {
        exchange.set_request_header("Cookie", cookie.data());
        exchange.set_request_header("Sec-Fetch-Site", "same-origin");
        CHECK(fixture.gate.granted({Access::SessionNotFromAnotherOrigin}, exchange));
    }
    SUBCASE("is reached on a request naming no initiator") {
        exchange.set_request_header("Cookie", cookie.data());
        CHECK(fixture.gate.granted({Access::SessionNotFromAnotherOrigin}, exchange));
    }
    SUBCASE("is refused on a request another site made") {
        exchange.set_request_header("Cookie", cookie.data());
        exchange.set_request_header("Sec-Fetch-Site", "cross-site");
        CHECK_FALSE(fixture.gate.granted({Access::SessionNotFromAnotherOrigin}, exchange));
    }
    SUBCASE("is refused on a request another origin of the site made") {
        exchange.set_request_header("Cookie", cookie.data());
        exchange.set_request_header("Sec-Fetch-Site", "same-site");
        CHECK_FALSE(fixture.gate.granted({Access::SessionNotFromAnotherOrigin}, exchange));
    }
    SUBCASE("is refused with the API token") {
        exchange.set_request_header("Authorization", "Bearer s3cret-token");
        CHECK_FALSE(fixture.gate.granted({Access::SessionNotFromAnotherOrigin}, exchange));
    }
}

TEST_CASE("a route behind the token") {
    Fixture fixture{};
    const Id id{fixture.open_session()};
    const SessionCookie cookie{build_session_cookie(id.data())};
    FakeHttpExchange exchange{};

    SUBCASE("is reached with the token") {
        exchange.set_request_header("Authorization", "Bearer s3cret-token");
        CHECK(fixture.gate.granted({Access::Token}, exchange));
    }
    SUBCASE("is refused with a session") {
        exchange.set_request_header("Cookie", cookie.data());
        CHECK_FALSE(fixture.gate.granted({Access::Token}, exchange));
    }
}

TEST_CASE("a route behind the token or a session") {
    Fixture fixture{};
    const Id id{fixture.open_session()};
    const SessionCookie cookie{build_session_cookie(id.data())};
    FakeHttpExchange exchange{};

    SUBCASE("is refused carrying nothing") {
        CHECK_FALSE(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is reached with the token") {
        exchange.set_request_header("Authorization", "Bearer s3cret-token");
        CHECK(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is reached with the token on a request another site made") {
        exchange.set_request_header("Authorization", "Bearer s3cret-token");
        exchange.set_request_header("Sec-Fetch-Site", "cross-site");
        CHECK(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is refused with a wrong token") {
        exchange.set_request_header("Authorization", "Bearer wrong-token");
        CHECK_FALSE(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is reached with a session on a request the device's own page made") {
        exchange.set_request_header("Cookie", cookie.data());
        exchange.set_request_header("Sec-Fetch-Site", "same-origin");
        CHECK(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is reached with a session on a request the user started") {
        exchange.set_request_header("Cookie", cookie.data());
        exchange.set_request_header("Sec-Fetch-Site", "none");
        CHECK(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is reached with a session on a request naming no initiator") {
        exchange.set_request_header("Cookie", cookie.data());
        CHECK(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is reached with a session on a request whose initiator the standard does not name") {
        exchange.set_request_header("Cookie", cookie.data());
        exchange.set_request_header("Sec-Fetch-Site", "elsewhere");
        CHECK(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is reached with a session on a request whose initiator does not fit the buffer") {
        exchange.set_request_header("Cookie", cookie.data());
        exchange.set_request_header("Sec-Fetch-Site", "cross-site-and-more");
        CHECK(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is reached with a session and a wrong token") {
        exchange.set_request_header("Cookie", cookie.data());
        exchange.set_request_header("Authorization", "Bearer wrong-token");
        CHECK(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is refused with a session on a request another site made") {
        exchange.set_request_header("Cookie", cookie.data());
        exchange.set_request_header("Sec-Fetch-Site", "cross-site");
        CHECK_FALSE(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is refused with a session on a request another origin of the site made") {
        exchange.set_request_header("Cookie", cookie.data());
        exchange.set_request_header("Sec-Fetch-Site", "same-site");
        CHECK_FALSE(fixture.gate.granted(API, exchange));
    }
    SUBCASE("is refused once the session is closed") {
        exchange.set_request_header("Cookie", cookie.data());
        fixture.session_store.close(id.data());
        CHECK_FALSE(fixture.gate.granted(API, exchange));
    }
}

TEST_CASE("a browser refused a session route is sent to the login page") {
    const Fixture fixture{};
    FakeHttpExchange exchange{};

    SUBCASE("on any request") {
        fixture.gate.refuse({Access::Session}, exchange);
    }
    SUBCASE("on one no other origin may make") {
        fixture.gate.refuse({Access::SessionNotFromAnotherOrigin}, exchange);
    }

    REQUIRE(exchange.status == HttpStatus::Found);
    CHECK(exchange.header_value("Location") == LOGIN_URI);
    CHECK(exchange.sent);
}

TEST_CASE("a client refused the API is told what to carry") {
    const Fixture fixture{};
    FakeHttpExchange exchange{};

    fixture.gate.refuse(API, exchange);

    REQUIRE(exchange.status == HttpStatus::Unauthorized);
    CHECK(exchange.header_value("WWW-Authenticate") == R"(Bearer realm="Plant watering")");
    // Without it a cross-origin page cannot tell a refused token from an
    // unreachable device.
    CHECK(exchange.header_value("Access-Control-Allow-Origin") == "*");
    CHECK(exchange.sent_text() == "Unauthorized");
}
