#include <doctest.h>
#include <esp_timer.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "web/session_store.hpp"

namespace {

using Id = std::array<char, SessionStore::ID_BUFFER_SIZE>;

// The id as it is spelled, copied out to survive the next open()
Id open_session(SessionStore& store) {
    Id id{};
    store.open(id);
    return id;
}

std::string_view text_of(const Id& id) {
    return id.data();
}

}  // namespace

TEST_CASE("a store with nothing opened holds nothing") {
    const SessionStore store{};

    CHECK_FALSE(store.holds("00000000000000000000000000000000"));
    CHECK_FALSE(store.holds(""));
}

TEST_CASE("an opened session is held by the id it was opened under") {
    SessionStore store{};
    const Id id{open_session(store)};

    CHECK(store.holds(text_of(id)));
}

TEST_CASE("an id is 32 hex characters") {
    SessionStore store{};
    const Id id{open_session(store)};

    REQUIRE(text_of(id).size() == SessionStore::ID_BUFFER_SIZE - 1);
    CHECK(std::ranges::all_of(text_of(id), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }));
}

TEST_CASE("each session is opened under an id of its own") {
    SessionStore store{};
    const Id first{open_session(store)};
    const Id second{open_session(store)};

    CHECK(text_of(first) != text_of(second));
    CHECK(store.holds(text_of(first)));
    CHECK(store.holds(text_of(second)));
}

// The length check is what keeps the compare in bounds.
TEST_CASE("an id of the wrong length is never held") {
    SessionStore store{};
    const Id id{open_session(store)};

    // One character longer, the id and a 0
    std::array<char, SessionStore::ID_BUFFER_SIZE + 1> longer{};
    std::ranges::copy(text_of(id), longer.begin());
    longer[SessionStore::ID_BUFFER_SIZE - 1] = '0';

    CHECK_FALSE(store.holds(text_of(id).substr(0, text_of(id).size() - 1)));
    CHECK_FALSE(store.holds(longer.data()));
    CHECK_FALSE(store.holds(""));
}

TEST_CASE("a wrong id of the right length is not held") {
    SessionStore store{};
    Id id{open_session(store)};
    // The last character is the only one that differs, leaving only the compare
    // to refuse it.
    char& last{id[SessionStore::ID_BUFFER_SIZE - 2]};
    last = (last == '0') ? '1' : '0';

    CHECK_FALSE(store.holds(text_of(id)));
}

TEST_CASE("closing drops the session the id names and no other") {
    SessionStore store{};
    const Id first{open_session(store)};
    const Id second{open_session(store)};

    store.close(text_of(first));

    CHECK_FALSE(store.holds(text_of(first)));
    CHECK(store.holds(text_of(second)));
}

TEST_CASE("closing an id the store does not hold drops nothing") {
    SessionStore store{};
    const Id id{open_session(store)};

    store.close("ffffffffffffffffffffffffffffffff");
    store.close("");

    CHECK(store.holds(text_of(id)));
}

TEST_CASE("a closed session can be opened again") {
    SessionStore store{};
    const Id first{open_session(store)};
    store.close(text_of(first));

    const Id second{open_session(store)};

    CHECK(store.holds(text_of(second)));
    CHECK_FALSE(store.holds(text_of(first)));
}

TEST_CASE("a login past the last slot drops the oldest session") {
    SessionStore store{};
    std::array<Id, SessionStore::MAX_SESSIONS> held{};
    for (Id& id : held) {
        id = open_session(store);
    }
    for (const Id& id : held) {
        REQUIRE(store.holds(text_of(id)));
    }

    const Id newest{open_session(store)};

    CHECK_FALSE(store.holds(text_of(held.front())));
    CHECK(store.holds(text_of(newest)));
    // Everything opened after the oldest is still held.
    for (std::size_t i{1}; i < held.size(); ++i) {
        CHECK(store.holds(text_of(held[i])));
    }
}

TEST_CASE("the oldest is the oldest still held, not the oldest slot") {
    SessionStore store{};
    std::array<Id, SessionStore::MAX_SESSIONS> held{};
    for (Id& id : held) {
        id = open_session(store);
    }

    // Takes the first slot, and is now the newest session in the store.
    const Id reused{open_session(store)};
    // Takes the second, which is the oldest still held
    const Id newest{open_session(store)};

    CHECK(store.holds(text_of(reused)));
    CHECK(store.holds(text_of(newest)));
    CHECK_FALSE(store.holds(text_of(held[1])));
}

TEST_CASE("a session is held until its lifetime is up") {
    esp_timer_stub_now_us = 0;
    SessionStore store{};
    const Id id{open_session(store)};

    esp_timer_stub_now_us = SessionStore::LIFETIME_US - 1;
    CHECK(store.holds(text_of(id)));

    esp_timer_stub_now_us = SessionStore::LIFETIME_US;
    CHECK_FALSE(store.holds(text_of(id)));
}

// The lifetime runs from the login, however often the session is used.
TEST_CASE("using a session does not extend it") {
    esp_timer_stub_now_us = 0;
    SessionStore store{};
    const Id id{open_session(store)};

    for (std::int64_t at{0}; at < SessionStore::LIFETIME_US; at += SessionStore::LIFETIME_US / 4) {
        esp_timer_stub_now_us = at;
        REQUIRE(store.holds(text_of(id)));
    }

    esp_timer_stub_now_us = SessionStore::LIFETIME_US;
    CHECK_FALSE(store.holds(text_of(id)));
}
