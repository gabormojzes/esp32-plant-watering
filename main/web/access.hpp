#pragma once

#include <cstdint>
#include <initializer_list>

// One credential a route may take
enum class Access : std::uint8_t {
    Open,
    Session,
    SessionNotFromAnotherOrigin,
    Token
};

// The credentials a route takes, any one of which reaches it. An empty set is
// reached by nothing.
class AccessSet {
public:
    constexpr AccessSet() = default;
    constexpr AccessSet(std::initializer_list<Access> accepted) {
        for (const Access access : accepted) {
            bits_ |= bit_of(access);
        }
    }

    constexpr bool has(Access access) const {
        return (bits_ & bit_of(access)) != 0;
    }
    constexpr bool operator==(const AccessSet&) const = default;

private:
    // A value outside the enum has no bit, and is in no set.
    static constexpr std::uint8_t bit_of(Access access) {
        const auto index{static_cast<unsigned>(access)};
        return index < 8 ? static_cast<std::uint8_t>(1U << index) : std::uint8_t{0};
    }

    std::uint8_t bits_{0};
};
