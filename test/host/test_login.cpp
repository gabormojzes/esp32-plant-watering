#include <doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "fake_web_host.hpp"
#include "shared/constants.hpp"
#include "web/cookie.hpp"
#include "web/pages/login.hpp"
#include "web/password_auth.hpp"
#include "web/session_cookie.hpp"
#include "web/session_store.hpp"

// Stands in for the gzipped page embedded on the device. The routes are under
// test, not what the page holds.
extern const char LOGIN_HTML_GZ_START[] asm("_binary_login_html_gz_start");
extern const std::uint32_t LOGIN_HTML_GZ_LENGTH asm("login_html_gz_length");
constexpr char LOGIN_HTML_GZ_START[]{"<h1>Plant watering</h1>"};
constexpr std::uint32_t LOGIN_HTML_GZ_LENGTH{
    static_cast<std::uint32_t>(std::string_view{LOGIN_HTML_GZ_START}.size())
};

namespace {

constexpr char LOGIN_URI[]{"/login"};
constexpr char LOGOUT_URI[]{"/logout"};
constexpr char DASHBOARD_URI[]{"/"};

using Id = std::array<char, SessionStore::ID_BUFFER_SIZE>;
// The cookie's name, "=" and an id, and the terminator
constexpr std::size_t SESSION_COOKIE_BUFFER_SIZE{
    std::string_view{session_cookie::NAME}.size() + SessionStore::ID_BUFFER_SIZE + 1
};
using SessionCookie = std::array<char, SESSION_COOKIE_BUFFER_SIZE>;

constexpr Login::Config CONFIG{
    .uri = LOGIN_URI,
    .logout_uri = LOGOUT_URI,
    .dashboard_uri = DASHBOARD_URI,
};

struct Fixture {
    Fixture() {
        login.start();
    }

    Id open_session() {
        Id id{};
        session_store.open(id);
        return id;
    }

    // Posts a JSON body to the login route, recording the answer in `exchange`.
    void submit(FakeHttpExchange& exchange, std::string_view body) {
        exchange.request_content_type = "application/json";
        exchange.request_body = body;
        REQUIRE(web_host.dispatch(LOGIN_URI, HttpMethod::Post, exchange));
    }

    FakeWebHost web_host{};
    SessionStore session_store{};
    PasswordAuth password_auth{PasswordAuth::Config{.user = "user", .password = "password"}};
    Login login{web_host, session_store, password_auth, CONFIG};
};

SessionCookie build_session_cookie(const Id& id) {
    SessionCookie cookie{};
    std::snprintf(cookie.data(), cookie.size(), "%s=%s", session_cookie::NAME, id.data());
    return cookie;
}

// The id the response tells the browser to hold
std::string_view id_in(const FakeHttpExchange& exchange) {
    return cookie::value(exchange.header_value("Set-Cookie"), session_cookie::NAME);
}

}  // namespace

TEST_CASE("the routes are claimed, the login open and the logout behind a session") {
    const Fixture fixture{};

    CHECK(fixture.web_host.claimed(LOGIN_URI, HttpMethod::Get));
    CHECK(fixture.web_host.claimed(LOGIN_URI, HttpMethod::Post));
    CHECK(fixture.web_host.claimed(LOGOUT_URI, HttpMethod::Post));

    CHECK(fixture.web_host.access_of(LOGIN_URI, HttpMethod::Get) == AccessSet{Access::Open});
    CHECK(fixture.web_host.access_of(LOGIN_URI, HttpMethod::Post) == AccessSet{Access::Open});
    CHECK(
        fixture.web_host.access_of(LOGOUT_URI, HttpMethod::Post) ==
        AccessSet{Access::SessionNotFromAnotherOrigin}
    );
}

// A login is only ever offered by the page the device serves itself.
TEST_CASE("no preflight is claimed alongside the login") {
    const Fixture fixture{};

    CHECK_FALSE(fixture.web_host.claimed(LOGIN_URI, HttpMethod::Options));
    CHECK_FALSE(fixture.web_host.claimed(LOGOUT_URI, HttpMethod::Options));
}

TEST_CASE("the page is served to a browser holding no session") {
    Fixture fixture{};
    FakeHttpExchange exchange{};

    REQUIRE(fixture.web_host.dispatch(LOGIN_URI, HttpMethod::Get, exchange));

    CHECK(exchange.response_media_type == std::string_view{"text/html"});
    CHECK(exchange.response_charset == std::string_view{"utf-8"});
    CHECK(exchange.header_value("Content-Encoding") == std::string_view{"gzip"});
    CHECK(exchange.sent_text() == std::string_view{LOGIN_HTML_GZ_START});
}

TEST_CASE("the page is served uncacheable") {
    Fixture fixture{};
    FakeHttpExchange exchange{};

    REQUIRE(fixture.web_host.dispatch(LOGIN_URI, HttpMethod::Get, exchange));

    CHECK(exchange.header_value("Cache-Control") == std::string_view{"no-store"});
}

TEST_CASE("the page is served under its content security policy") {
    Fixture fixture{};
    FakeHttpExchange exchange{};

    REQUIRE(fixture.web_host.dispatch(LOGIN_URI, HttpMethod::Get, exchange));

    CHECK(
        exchange.header_value("Content-Security-Policy") ==
        std::string_view{Login::CONTENT_SECURITY_POLICY}
    );
}

TEST_CASE("a browser already holding one is sent on to what it came for") {
    Fixture fixture{};
    const SessionCookie header{build_session_cookie(fixture.open_session())};
    FakeHttpExchange exchange{};
    exchange.set_request_header("Cookie", header.data());

    REQUIRE(fixture.web_host.dispatch(LOGIN_URI, HttpMethod::Get, exchange));

    REQUIRE(exchange.status == HttpStatus::Found);
    CHECK(exchange.header_value("Location") == DASHBOARD_URI);
}

TEST_CASE("the right credentials open a session") {
    Fixture fixture{};
    FakeHttpExchange exchange{};

    fixture.submit(exchange, R"({"user":"user","password":"password"})");

    REQUIRE(exchange.status == HttpStatus::NoContent);
    const std::string_view id{id_in(exchange)};
    CHECK(id.size() == SessionStore::ID_BUFFER_SIZE - 1);
    // The browser is told to hold an id the store will answer for.
    CHECK(fixture.session_store.holds(id));
}

TEST_CASE("a login carrying a JSON content type in any letter case opens a session") {
    Fixture fixture{};
    FakeHttpExchange exchange{};
    exchange.request_content_type = "Application/JSON";
    exchange.request_body = R"({"user":"user","password":"password"})";

    REQUIRE(fixture.web_host.dispatch(LOGIN_URI, HttpMethod::Post, exchange));

    REQUIRE(exchange.status == HttpStatus::NoContent);
    CHECK(fixture.session_store.holds(id_in(exchange)));
}

TEST_CASE("the cookie keeps the id out of scripts and off other origins") {
    Fixture fixture{};
    FakeHttpExchange exchange{};

    fixture.submit(exchange, R"({"user":"user","password":"password"})");

    const std::string_view set_cookie{exchange.header_value("Set-Cookie")};
    CHECK(set_cookie.contains("HttpOnly"));
    CHECK(set_cookie.contains("SameSite=Strict"));
    CHECK(set_cookie.contains("Path=/"));
    // The device serves plain HTTP, and a Secure cookie would never come back.
    CHECK_FALSE(set_cookie.contains("Secure"));
}

TEST_CASE("the cookie lasts as long as the session it names") {
    Fixture fixture{};
    FakeHttpExchange exchange{};

    fixture.submit(exchange, R"({"user":"user","password":"password"})");

    std::array<char, 32> max_age{};
    std::snprintf(
        max_age.data(),
        max_age.size(),
        "; Max-Age=%lld;",
        static_cast<long long>(SessionStore::LIFETIME_US / constants::US_PER_S)
    );
    CHECK(exchange.header_value("Set-Cookie").contains(max_age.data()));
}

TEST_CASE("wrong credentials open nothing") {
    Fixture fixture{};

    SUBCASE("a wrong password") {
        FakeHttpExchange exchange{};
        fixture.submit(exchange, R"({"user":"user","password":"wrong"})");
        CHECK(exchange.status == HttpStatus::Unauthorized);
        CHECK(exchange.header_value("WWW-Authenticate") == R"(Form realm="Plant watering")");
        CHECK_FALSE(exchange.has_header("Set-Cookie"));
    }
    SUBCASE("a wrong user") {
        FakeHttpExchange exchange{};
        fixture.submit(exchange, R"({"user":"other","password":"password"})");
        CHECK(exchange.status == HttpStatus::Unauthorized);
        CHECK_FALSE(exchange.has_header("Set-Cookie"));
    }
    SUBCASE("a body missing a half") {
        FakeHttpExchange exchange{};
        fixture.submit(exchange, R"({"user":"user"})");
        CHECK(exchange.status == HttpStatus::Unauthorized);
    }
    SUBCASE("a body that is not an object of strings") {
        FakeHttpExchange exchange{};
        fixture.submit(exchange, R"({"user":1,"password":true})");
        CHECK(exchange.status == HttpStatus::Unauthorized);
    }
    SUBCASE("a body that is not JSON at all") {
        FakeHttpExchange exchange{};
        fixture.submit(exchange, "user=user&password=password");
        CHECK(exchange.status == HttpStatus::Unauthorized);
    }
    SUBCASE("an empty body") {
        FakeHttpExchange exchange{};
        fixture.submit(exchange, "");
        CHECK(exchange.status == HttpStatus::Unauthorized);
    }
    SUBCASE("a password followed by an escaped NUL") {
        FakeHttpExchange exchange{};
        fixture.submit(exchange, R"({"user":"user","password":"password\u0000junk"})");
        CHECK(exchange.status == HttpStatus::Unauthorized);
    }
    SUBCASE("a user followed by an escaped NUL") {
        FakeHttpExchange exchange{};
        fixture.submit(exchange, R"({"user":"user\u0000junk","password":"password"})");
        CHECK(exchange.status == HttpStatus::Unauthorized);
    }
    SUBCASE("a password followed by a raw NUL") {
        FakeHttpExchange exchange{};
        // Spelled as an array, because a literal read up to its terminator would
        // stop at the NUL.
        constexpr char body[]{"{\"user\":\"user\",\"password\":\"password\0junk\"}"};
        fixture.submit(exchange, {body, std::size(body) - 1});
        CHECK(exchange.status == HttpStatus::Unauthorized);
    }
}

TEST_CASE("an escaped backslash before the text u0000 is no NUL") {
    FakeWebHost web_host{};
    SessionStore session_store{};
    const PasswordAuth password_auth{
        PasswordAuth::Config{.user = "user", .password = R"(pass\u0000)"}
    };
    Login login{web_host, session_store, password_auth, CONFIG};
    login.start();

    FakeHttpExchange exchange{};
    exchange.request_content_type = "application/json";
    exchange.request_body = R"({"user":"user","password":"pass\\u0000"})";
    REQUIRE(web_host.dispatch(LOGIN_URI, HttpMethod::Post, exchange));

    CHECK(exchange.status == HttpStatus::NoContent);
}

TEST_CASE("a body that cannot be read opens nothing") {
    Fixture fixture{};

    SUBCASE("carrying no JSON content type") {
        FakeHttpExchange exchange{};
        exchange.request_body = R"({"user":"user","password":"password"})";
        REQUIRE(fixture.web_host.dispatch(LOGIN_URI, HttpMethod::Post, exchange));
        CHECK(exchange.status == HttpStatus::UnsupportedMediaType);
    }
    SUBCASE("carrying a content type that only starts like JSON") {
        FakeHttpExchange exchange{};
        exchange.request_content_type = "application/jsonl";
        exchange.request_body = R"({"user":"user","password":"password"})";
        REQUIRE(fixture.web_host.dispatch(LOGIN_URI, HttpMethod::Post, exchange));
        CHECK(exchange.status == HttpStatus::UnsupportedMediaType);
    }
    SUBCASE("past the buffer it is read into") {
        FakeHttpExchange exchange{};
        exchange.request_content_type = "application/json";
        exchange.declared_length = 100000;
        REQUIRE(fixture.web_host.dispatch(LOGIN_URI, HttpMethod::Post, exchange));
        CHECK(exchange.status == HttpStatus::ContentTooLarge);
    }
    SUBCASE("from a socket that went quiet") {
        FakeHttpExchange exchange{};
        exchange.request_content_type = "application/json";
        exchange.request_body = R"({"user":"user","password":"password"})";
        exchange.body_outcome = BodyResult::TimedOut;
        REQUIRE(fixture.web_host.dispatch(LOGIN_URI, HttpMethod::Post, exchange));
        CHECK(exchange.status == HttpStatus::RequestTimeout);
    }
    SUBCASE("from a socket that broke") {
        FakeHttpExchange exchange{};
        exchange.request_content_type = "application/json";
        exchange.request_body = R"({"user":"user","password":"password"})";
        exchange.body_outcome = BodyResult::Failed;
        REQUIRE(fixture.web_host.dispatch(LOGIN_URI, HttpMethod::Post, exchange));
        CHECK(exchange.status == HttpStatus::BadRequest);
    }
}

TEST_CASE("a credential carrying what JSON escapes is read as it was typed") {
    FakeWebHost web_host{};
    SessionStore session_store{};
    const PasswordAuth password_auth{
        PasswordAuth::Config{.user = "user", .password = R"(pa"ss\word)"}
    };
    Login login{web_host, session_store, password_auth, CONFIG};
    login.start();

    FakeHttpExchange exchange{};
    exchange.request_content_type = "application/json";
    exchange.request_body = R"({"user":"user","password":"pa\"ss\\word"})";
    REQUIRE(web_host.dispatch(LOGIN_URI, HttpMethod::Post, exchange));

    CHECK(exchange.status == HttpStatus::NoContent);
}

TEST_CASE("logging out closes the session and clears the cookie") {
    Fixture fixture{};
    const Id id{fixture.open_session()};
    const SessionCookie header{build_session_cookie(id)};
    FakeHttpExchange exchange{};
    exchange.set_request_header("Cookie", header.data());

    REQUIRE(fixture.web_host.dispatch(LOGOUT_URI, HttpMethod::Post, exchange));

    CHECK(exchange.status == HttpStatus::NoContent);
    CHECK_FALSE(fixture.session_store.holds(id.data()));
    CHECK(exchange.header_value("Set-Cookie").contains("Max-Age=0"));
}

TEST_CASE("one browser logging out leaves another's session alone") {
    Fixture fixture{};
    const Id mine{fixture.open_session()};
    const Id theirs{fixture.open_session()};
    const SessionCookie header{build_session_cookie(mine)};
    FakeHttpExchange exchange{};
    exchange.set_request_header("Cookie", header.data());

    REQUIRE(fixture.web_host.dispatch(LOGOUT_URI, HttpMethod::Post, exchange));

    CHECK_FALSE(fixture.session_store.holds(mine.data()));
    CHECK(fixture.session_store.holds(theirs.data()));
}
