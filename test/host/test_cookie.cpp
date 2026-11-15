#include <doctest.h>

#include "web/cookie.hpp"

TEST_CASE("the one pair a header carries is found under its name alone") {
    CHECK(cookie::value("session=abc123", "session") == "abc123");
    CHECK(cookie::value("session=abc123", "other").empty());
}

TEST_CASE("each of several pairs is found under its own name") {
    constexpr char cookies[]{"theme=dark; session=abc123; consent=1"};

    CHECK(cookie::value(cookies, "theme") == "dark");
    CHECK(cookie::value(cookies, "session") == "abc123");
    CHECK(cookie::value(cookies, "consent") == "1");
    CHECK(cookie::value(cookies, "absent").empty());
}

// RFC 6265 asks senders for "; " but readers must not require the space.
TEST_CASE("the padding around a pair is not part of it") {
    CHECK(cookie::value("a=1;session=abc", "session") == "abc");
    CHECK(cookie::value("a=1;   session=abc", "session") == "abc");
    CHECK(cookie::value("a=1;\tsession=abc", "session") == "abc");
    CHECK(cookie::value("  session = abc  ", "session") == "abc");
}

TEST_CASE("a name is matched whole") {
    CHECK(cookie::value("xsession=abc", "session").empty());
    CHECK(cookie::value("sessionid=abc", "session").empty());
    CHECK(cookie::value("session=abc", "sessio").empty());
    CHECK(cookie::value("session=abc", "SESSION").empty());
}

TEST_CASE("a header carrying nothing usable yields no value") {
    CHECK(cookie::value("", "session").empty());
    CHECK(cookie::value("   ", "session").empty());
    CHECK(cookie::value(";;;", "session").empty());
    // A pair without "=" stores nothing.
    CHECK(cookie::value("session", "session").empty());
    CHECK(cookie::value("a=1; session", "session").empty());
}

TEST_CASE("a name with no value reads as the empty value it is") {
    CHECK(cookie::value("session=", "session").empty());
    CHECK(cookie::value("session=; a=1", "session").empty());
    // The pair behind it is still found.
    CHECK(cookie::value("session=; a=1", "a") == "1");
}

TEST_CASE("a value is taken as it is spelled") {
    CHECK(cookie::value("session=a%20b", "session") == "a%20b");
    CHECK(cookie::value("session=\"quoted\"", "session") == "\"quoted\"");
    // The value ends where the pair does.
    CHECK(cookie::value("session=a=b", "session") == "a=b");
}

TEST_CASE("the first pair under a name is the one taken") {
    CHECK(cookie::value("session=first; session=second", "session") == "first");
}

TEST_CASE("nothing is stored under no name at all") {
    CHECK(cookie::value("session=abc", "").empty());
}
