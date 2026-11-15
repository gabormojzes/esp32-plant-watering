#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "shared/constants.hpp"

// The sessions the server knows a logged-in browser by. An id stands until it
// is closed, until its lifetime is up, or until the device reboots.
class SessionStore {
public:
    // The id as it is spelled, and its terminator
    static constexpr std::size_t ID_BUFFER_SIZE{33};
    // One per browser expected to hold a session at once. A login past the last
    // free slot takes the oldest, which logs that browser out.
    static constexpr std::size_t MAX_SESSIONS{4};
    // How long a session stands after it is opened. Nothing refreshes it, and a
    // browser logged in this long ago logs in again.
    static constexpr std::int64_t LIFETIME_US{2419200 * constants::US_PER_S};

    // Opens a session and writes its null-terminated id into `id`.
    void open(std::span<char, ID_BUFFER_SIZE> id);
    bool holds(std::string_view id) const;
    void close(std::string_view id);

private:
    struct Slot {
        std::array<char, ID_BUFFER_SIZE> id{};
        // 0 for a slot never filled. The lowest non-zero value is the oldest
        // session.
        std::uint64_t sequence{0};
        std::int64_t opened_us{0};
    };

    // The free slot to fill next, or the oldest session when none is free
    Slot& next();

    std::array<Slot, MAX_SESSIONS> slots_{};
    // Counts logins, not sessions, which keeps an id opened into a reused slot
    // sorting after the ones already held.
    std::uint64_t logins_{0};
};
