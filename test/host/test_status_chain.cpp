#include <doctest.h>

#include <array>
#include <optional>

#include "indicator/status.hpp"
#include "indicator/status_chain.hpp"
#include "indicator/status_source.hpp"

namespace {

struct SourceStub : StatusSource {
    std::optional<Status> status() const override {
        ++asked;
        return says;
    }

    std::optional<Status> says;
    mutable int asked{0};
};

}  // namespace

TEST_CASE("an empty chain is nominal") {
    const StatusChain chain{{.sources = {}}};
    CHECK(chain.status() == Status::Nominal);
}

TEST_CASE("a source with nothing to say leaves the chain nominal") {
    SourceStub quiet{};
    const std::array<const StatusSource*, 1> sources{&quiet};
    const StatusChain chain{{.sources = sources}};

    CHECK(chain.status() == Status::Nominal);

    quiet.says = Status::Error;
    CHECK(chain.status() == Status::Error);
}

TEST_CASE("the first source with something to say masks the ones after it") {
    SourceStub first{};
    SourceStub second{};
    second.says = Status::Warning;
    const std::array<const StatusSource*, 2> sources{&first, &second};
    const StatusChain chain{{.sources = sources}};

    CHECK(chain.status() == Status::Warning);

    first.says = Status::Error;
    CHECK(chain.status() == Status::Error);
}

TEST_CASE("nothing to say anywhere is nominal") {
    SourceStub first{};
    SourceStub second{};
    const std::array<const StatusSource*, 2> sources{&first, &second};
    const StatusChain chain{{.sources = sources}};

    CHECK(chain.status() == Status::Nominal);
}

TEST_CASE("a masked source is never asked") {
    SourceStub first{};
    first.says = Status::Error;
    SourceStub second{};
    const std::array<const StatusSource*, 2> sources{&first, &second};
    const StatusChain chain{{.sources = sources}};

    chain.status();
    CHECK(second.asked == 0);
}
