#include <doctest.h>

#include <array>
#include <cstddef>
#include <cstdio>

#include "fault/fault.hpp"
#include "fault/fault_definitions.hpp"
#include "fault/fault_store.hpp"

namespace {

constexpr Fault WARNING{.code = "WTST01", .severity = Severity::Warning, .message = "w"};
constexpr Fault OTHER_WARNING{.code = "WTST02", .severity = Severity::Warning, .message = "w"};
constexpr Fault ERROR{.code = "ETST01", .severity = Severity::Error, .message = "e"};

// What the store holds at once, one slot per fault the firmware defines
constexpr std::size_t CAPACITY{fault_definitions::ALL.size()};

// Static storage, because the store keeps the code pointers it is given
std::array<std::array<char, 8>, CAPACITY + 1> generated_codes{};

Fault generated(std::size_t index) {
    std::snprintf(generated_codes[index].data(), generated_codes[index].size(), "WGEN%02zu", index);
    return {
        .code = generated_codes[index].data(),
        .severity = Severity::Warning,
        .message = "generated",
    };
}

}  // namespace

TEST_CASE("an empty store has no worst severity") {
    const FaultStore store{};
    CHECK_FALSE(store.worst_severity().has_value());
}

TEST_CASE("an added fault is recorded once and active") {
    FaultStore store{};
    store.accept(WARNING);

    std::array<FaultEntry, 4> entries{};
    REQUIRE(store.snapshot(entries) == 1);
    CHECK(entries[0].fault.code == WARNING.code);
    CHECK(entries[0].active);
    CHECK(store.worst_severity() == Severity::Warning);
}

TEST_CASE("a code raised again is nothing new to record") {
    FaultStore store{};
    store.accept(WARNING);
    store.accept(WARNING);
    store.accept(WARNING);

    std::array<FaultEntry, 4> entries{};
    REQUIRE(store.snapshot(entries) == 1);
    CHECK(entries[0].active);
}

TEST_CASE("distinct codes each take a slot") {
    FaultStore store{};
    store.accept(WARNING);
    store.accept(ERROR);

    std::array<FaultEntry, 4> entries{};
    CHECK(store.snapshot(entries) == 2);
}

TEST_CASE("the worst severity is the worst of everything in") {
    FaultStore store{};
    store.accept(WARNING);
    CHECK(store.worst_severity() == Severity::Warning);
    store.accept(ERROR);
    CHECK(store.worst_severity() == Severity::Error);
    store.accept(OTHER_WARNING);  // Lesser than the error already in
    CHECK(store.worst_severity() == Severity::Error);
}

TEST_CASE("a full store drops new codes and keeps what it has") {
    FaultStore store{};
    for (std::size_t i{0}; i < CAPACITY; ++i) {
        store.accept(generated(i));
    }
    store.accept(ERROR);  // One code too many

    std::array<FaultEntry, CAPACITY + 1> entries{};
    CHECK(store.snapshot(entries) == CAPACITY);
    // Dropped entirely, worst severity included.
    CHECK(store.worst_severity() == Severity::Warning);
}

TEST_CASE("a cleared code stops reaching the worst severity") {
    FaultStore store{};
    store.accept(WARNING);
    store.clear(WARNING.code);

    CHECK_FALSE(store.worst_severity().has_value());
}

TEST_CASE("a cleared code keeps its record") {
    FaultStore store{};
    store.accept(WARNING);
    store.clear(WARNING.code);

    std::array<FaultEntry, 4> entries{};
    REQUIRE(store.snapshot(entries) == 1);
    CHECK(entries[0].fault.code == WARNING.code);
    CHECK_FALSE(entries[0].active);
}

TEST_CASE("clearing the worst falls back to what is still active") {
    FaultStore store{};
    store.accept(WARNING);
    store.accept(ERROR);
    store.clear(ERROR.code);

    CHECK(store.worst_severity() == Severity::Warning);
}

TEST_CASE("clearing one code leaves the others alone") {
    FaultStore store{};
    store.accept(WARNING);
    store.accept(OTHER_WARNING);
    store.clear(WARNING.code);

    std::array<FaultEntry, 4> entries{};
    REQUIRE(store.snapshot(entries) == 2);
    CHECK_FALSE(entries[0].active);
    CHECK(entries[1].active);
    CHECK(store.worst_severity() == Severity::Warning);
}

TEST_CASE("clearing a code the store never held changes nothing") {
    FaultStore store{};
    store.accept(WARNING);
    store.clear(ERROR.code);

    std::array<FaultEntry, 4> entries{};
    CHECK(store.snapshot(entries) == 1);
    CHECK(store.worst_severity() == Severity::Warning);
}

TEST_CASE("clearing on an empty store is nothing to do") {
    FaultStore store{};
    store.clear(WARNING.code);

    CHECK_FALSE(store.worst_severity().has_value());
}

TEST_CASE("clearing twice reads the same as clearing once") {
    FaultStore store{};
    store.accept(WARNING);
    store.clear(WARNING.code);
    store.clear(WARNING.code);

    std::array<FaultEntry, 4> entries{};
    REQUIRE(store.snapshot(entries) == 1);
    CHECK_FALSE(entries[0].active);
    CHECK_FALSE(store.worst_severity().has_value());
}

TEST_CASE("a cleared code raised again is active once more") {
    FaultStore store{};
    store.accept(WARNING);
    store.clear(WARNING.code);
    store.accept(WARNING);

    std::array<FaultEntry, 4> entries{};
    REQUIRE(store.snapshot(entries) == 1);
    CHECK(entries[0].active);
    CHECK(store.worst_severity() == Severity::Warning);
}

TEST_CASE("a code cleared and raised again takes no second slot") {
    FaultStore store{};
    for (std::size_t i{0}; i < CAPACITY; ++i) {
        store.accept(generated(i));
    }
    store.clear(generated_codes[0].data());
    store.accept(generated(0));

    std::array<FaultEntry, CAPACITY + 1> entries{};
    CHECK(store.snapshot(entries) == CAPACITY);
    CHECK(entries[0].active);
}

TEST_CASE("snapshot writes no more than the caller has room for") {
    FaultStore store{};
    store.accept(WARNING);
    store.accept(ERROR);
    store.accept(OTHER_WARNING);

    std::array<FaultEntry, 2> entries{};
    CHECK(store.snapshot(entries) == 2);
    CHECK(entries[1].fault.code == ERROR.code);
}
