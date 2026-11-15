#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "fake_message_format.hpp"
#include "messaging/command.hpp"
#include "messaging/next_command.hpp"
#include "messaging/payload.hpp"

namespace {

// Hands out the queued texts in order, as an inbox or an endpoint would.
struct SourceStub {
    static constexpr std::size_t CAPACITY{4};

    void queue(std::string_view text) {
        REQUIRE(queued < CAPACITY);
        pending[queued++] = text;
    }
    bool take(Payload& payload) {
        if (taken == queued) {
            return false;
        }
        const std::string_view text{pending[taken++]};
        payload.length = static_cast<int>(text.size());
        std::ranges::copy(text, payload.data.begin());
        return true;
    }

    std::array<std::string_view, CAPACITY> pending{};
    std::size_t queued{0};
    std::size_t taken{0};
};

}  // namespace

TEST_CASE("a payload that decodes becomes the command") {
    const FakeMessageFormat format{};
    SourceStub source{};
    source.queue("42");
    Payload payload{};

    const std::optional<Command> command{next_command(source, format, payload)};

    REQUIRE(command);
    REQUIRE(command->sensor.target);
    CHECK(*command->sensor.target == doctest::Approx(42.0));
}

TEST_CASE("an undecodable payload is skipped for the next one") {
    const FakeMessageFormat format{};
    SourceStub source{};
    source.queue("not a number");
    source.queue("30");
    Payload payload{};

    const std::optional<Command> command{next_command(source, format, payload)};

    REQUIRE(command);
    REQUIRE(command->sensor.target);
    CHECK(*command->sensor.target == doctest::Approx(30.0));
}

TEST_CASE("payloads after the command stay with the source") {
    const FakeMessageFormat format{};
    SourceStub source{};
    source.queue("1");
    source.queue("2");
    Payload payload{};

    REQUIRE(next_command(source, format, payload));

    CHECK(source.taken == 1);
}

TEST_CASE("a source holding nothing decodable is drained to no command") {
    const FakeMessageFormat format{};
    SourceStub source{};
    source.queue("one");
    source.queue("two");
    Payload payload{};

    CHECK_FALSE(next_command(source, format, payload));
    CHECK(source.taken == 2);
}

TEST_CASE("a source with nothing pending gives no command") {
    const FakeMessageFormat format{};
    SourceStub source{};
    Payload payload{};

    CHECK_FALSE(next_command(source, format, payload));
}
