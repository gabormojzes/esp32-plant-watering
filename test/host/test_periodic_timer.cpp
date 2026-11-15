#include <doctest.h>

#include <cstdint>
#include <limits>

#include "shared/periodic_timer.hpp"

TEST_CASE("first poll fires immediately regardless of now") {
    PeriodicTimer t{100};
    CHECK(t.ready(0));
}

TEST_CASE("a timer is not ready before the interval elapses") {
    PeriodicTimer t{100};
    REQUIRE(t.ready(0));
    CHECK_FALSE(t.ready(1));
    CHECK_FALSE(t.ready(50));
    CHECK_FALSE(t.ready(99));
}

TEST_CASE("a timer is ready again once the interval elapses") {
    PeriodicTimer t{100};
    REQUIRE(t.ready(0));
    CHECK(t.ready(100));  // Exactly on the boundary
    CHECK_FALSE(t.ready(150));
    CHECK(t.ready(200));
}

TEST_CASE("a long gap fires once with no catch-up burst") {
    PeriodicTimer t{100};
    REQUIRE(t.ready(0));
    CHECK(t.ready(10000));
    CHECK_FALSE(t.ready(10000));  // No second fire at the same instant
    CHECK_FALSE(t.ready(10050));
    CHECK(t.ready(10100));
}

TEST_CASE("cadence holds over many ticks") {
    PeriodicTimer t{100};
    int fired{0};
    for (std::int64_t now_us{0}; now_us <= 1000; now_us++) {
        if (t.ready(now_us)) {
            fired++;
        }
    }
    CHECK(fired == 11);
}

TEST_CASE("a non-positive interval is never ready") {
    PeriodicTimer zero{0};
    PeriodicTimer negative{-100};
    CHECK_FALSE(zero.ready(0));
    CHECK_FALSE(zero.ready(1000000));
    CHECK_FALSE(negative.ready(1000000));
}

TEST_CASE("timers track their schedules independently") {
    PeriodicTimer a{100};
    PeriodicTimer b{300};
    CHECK(a.ready(0));  // Both fire first time
    CHECK(b.ready(0));
    CHECK(a.ready(100));
    CHECK_FALSE(b.ready(100));
    CHECK(b.ready(300));  // b due, independent of a
}

TEST_CASE("a gap to the top of the clock's range fires once") {
    constexpr std::int64_t max_us{std::numeric_limits<std::int64_t>::max()};
    PeriodicTimer t{100};
    REQUIRE(t.ready(0));
    CHECK(t.ready(max_us));
    CHECK_FALSE(t.ready(max_us));
}

TEST_CASE("a first poll at the top of the clock's range fires once") {
    constexpr std::int64_t max_us{std::numeric_limits<std::int64_t>::max()};
    PeriodicTimer t{100};
    CHECK(t.ready(max_us));
    CHECK_FALSE(t.ready(max_us));
}
