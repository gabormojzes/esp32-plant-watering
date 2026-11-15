#include <doctest.h>

#include "fault/fault.hpp"
#include "fault/fault_store.hpp"
#include "fault_status_source.hpp"
#include "indicator/status.hpp"

namespace {

constexpr Fault WARNING{.code = "WTST01", .severity = Severity::Warning, .message = "w"};
constexpr Fault ERROR{.code = "ETST01", .severity = Severity::Error, .message = "e"};

}  // namespace

TEST_CASE("an empty store has nothing to say") {
    const FaultStore fault_store{};
    const FaultStatusSource source{fault_store};
    CHECK_FALSE(source.status().has_value());
}

TEST_CASE("a warning calls for the warning status") {
    FaultStore fault_store{};
    fault_store.accept(WARNING);
    const FaultStatusSource source{fault_store};
    CHECK(source.status() == Status::Warning);
}

TEST_CASE("an error calls for the error status") {
    FaultStore fault_store{};
    fault_store.accept(ERROR);
    const FaultStatusSource source{fault_store};
    CHECK(source.status() == Status::Error);
}

TEST_CASE("the worst severity wins regardless of order") {
    FaultStore fault_store{};
    fault_store.accept(WARNING);
    fault_store.accept(ERROR);
    const FaultStatusSource source{fault_store};
    CHECK(source.status() == Status::Error);
}

TEST_CASE("a lesser fault added after does not soften the status") {
    FaultStore fault_store{};
    fault_store.accept(ERROR);
    fault_store.accept(WARNING);
    const FaultStatusSource source{fault_store};
    CHECK(source.status() == Status::Error);
}

TEST_CASE("every call re-reads the store, and the status follows what is in it") {
    FaultStore fault_store{};
    const FaultStatusSource source{fault_store};
    CHECK_FALSE(source.status().has_value());
    fault_store.accept(WARNING);
    CHECK(source.status() == Status::Warning);
    fault_store.accept(ERROR);
    CHECK(source.status() == Status::Error);
}

TEST_CASE("clearing the last active fault leaves nothing to say") {
    FaultStore fault_store{};
    const FaultStatusSource source{fault_store};
    fault_store.accept(ERROR);
    REQUIRE(source.status() == Status::Error);

    fault_store.clear(ERROR.code);

    CHECK_FALSE(source.status().has_value());
}

TEST_CASE("clearing the error softens the status to the warning still active") {
    FaultStore fault_store{};
    const FaultStatusSource source{fault_store};
    fault_store.accept(WARNING);
    fault_store.accept(ERROR);
    REQUIRE(source.status() == Status::Error);

    fault_store.clear(ERROR.code);

    CHECK(source.status() == Status::Warning);
}

TEST_CASE("a cleared fault raised again brings its status back") {
    FaultStore fault_store{};
    const FaultStatusSource source{fault_store};
    fault_store.accept(ERROR);
    fault_store.clear(ERROR.code);
    REQUIRE_FALSE(source.status().has_value());

    fault_store.accept(ERROR);

    CHECK(source.status() == Status::Error);
}
