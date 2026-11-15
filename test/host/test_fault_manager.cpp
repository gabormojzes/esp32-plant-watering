#include <doctest.h>

#include <array>
#include <span>

#include "fault/fault.hpp"
#include "fault/fault_manager.hpp"
#include "fault/fault_sink.hpp"

namespace {

constexpr Fault WARNING{.code = "WTST01", .severity = Severity::Warning, .message = "w"};

// Ticks once per call, showing the order the sinks were called in
int next_sequence{0};

// Keeps what it was handed and when, showing the fan-out.
struct SinkStub : FaultSink {
    void accept(const Fault& fault) override {
        ++accepted;
        accepted_at = next_sequence++;
        last_code = fault.code;
    }

    void clear(const char* code) override {
        ++cleared;
        cleared_at = next_sequence++;
        last_cleared_code = code;
    }

    int accepted{0};
    int accepted_at{-1};
    const char* last_code{nullptr};
    int cleared{0};
    int cleared_at{-1};
    const char* last_cleared_code{nullptr};
};

// Installs the sinks for one case and uninstalls them with it, because a later
// case must not raise into sinks that are gone.
struct InstalledSinks {
    explicit InstalledSinks(std::span<FaultSink* const> sinks) {
        FaultManager::instance().install(sinks);
    }

    ~InstalledSinks() {
        FaultManager::instance().install({});
    }

    InstalledSinks(const InstalledSinks&) = delete;
    InstalledSinks& operator=(const InstalledSinks&) = delete;
};

}  // namespace

TEST_CASE("a raised fault reaches every installed sink") {
    next_sequence = 0;
    SinkStub first{};
    SinkStub second{};
    const std::array<FaultSink*, 2> sinks{&first, &second};
    const InstalledSinks installed{sinks};

    FaultManager::instance().raise(WARNING);

    CHECK(first.accepted == 1);
    CHECK(second.accepted == 1);
    CHECK(first.last_code == WARNING.code);
    CHECK(second.last_code == WARNING.code);
}

TEST_CASE("the sinks are handed the fault in list order") {
    next_sequence = 0;
    SinkStub first{};
    SinkStub second{};
    const std::array<FaultSink*, 2> sinks{&first, &second};
    const InstalledSinks installed{sinks};

    FaultManager::instance().raise(WARNING);

    CHECK(first.accepted_at == 0);
    CHECK(second.accepted_at == 1);
}

TEST_CASE("a cleared fault reaches every installed sink, in list order") {
    next_sequence = 0;
    SinkStub first{};
    SinkStub second{};
    const std::array<FaultSink*, 2> sinks{&first, &second};
    const InstalledSinks installed{sinks};

    FaultManager::instance().clear(WARNING);

    CHECK(first.cleared == 1);
    CHECK(second.cleared == 1);
    CHECK(first.last_cleared_code == WARNING.code);
    CHECK(second.last_cleared_code == WARNING.code);
    CHECK(first.cleared_at == 0);
    CHECK(second.cleared_at == 1);
}

TEST_CASE("the two directions reach the sink apart from each other") {
    next_sequence = 0;
    SinkStub sink{};
    const std::array<FaultSink*, 1> sinks{&sink};
    const InstalledSinks installed{sinks};

    FaultManager::instance().raise(WARNING);

    CHECK(sink.accepted == 1);
    CHECK(sink.cleared == 0);

    FaultManager::instance().clear(WARNING);

    CHECK(sink.accepted == 1);
    CHECK(sink.cleared == 1);
}

TEST_CASE("a raised fault with no sinks installed is dropped") {
    next_sequence = 0;
    FaultManager::instance().install({});
    FaultManager::instance().raise(WARNING);

    // Not crashing is the main check. A sink installed afterward confirms
    // nothing was queued for it.
    SinkStub late{};
    const std::array<FaultSink*, 1> sinks{&late};
    const InstalledSinks installed{sinks};
    CHECK(late.accepted == 0);
}

TEST_CASE("a cleared fault with no sinks installed is dropped") {
    next_sequence = 0;
    FaultManager::instance().install({});
    FaultManager::instance().clear(WARNING);

    SinkStub late{};
    const std::array<FaultSink*, 1> sinks{&late};
    const InstalledSinks installed{sinks};
    CHECK(late.cleared == 0);
}
