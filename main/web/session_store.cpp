#include "web/session_store.hpp"

#include <esp_random.h>
#include <esp_timer.h>

#include <algorithm>

#include "shared/string_utils.hpp"

namespace {

// The bytes an id is minted from, spelled two characters each
constexpr std::size_t ID_BYTES{16};
constexpr char HEX[]{"0123456789abcdef"};

static_assert(
    SessionStore::ID_BUFFER_SIZE == (ID_BYTES * 2) + 1,
    "ID_BUFFER_SIZE does not fit two characters per byte and a terminator"
);

}  // namespace

void SessionStore::open(std::span<char, ID_BUFFER_SIZE> id) {
    std::array<std::uint8_t, ID_BYTES> bytes{};
    // esp_fill_random is truly random while an entropy source feeds it, the RF
    // subsystem on a chip with a radio or the SAR ADC on one without.
    esp_fill_random(bytes.data(), bytes.size());

    Slot& slot{next()};
    for (std::size_t i{0}; i < bytes.size(); ++i) {
        slot.id[i * 2] = HEX[bytes[i] >> 4];
        slot.id[(i * 2) + 1] = HEX[bytes[i] & 0x0F];
    }
    slot.id[ID_BUFFER_SIZE - 1] = '\0';
    slot.sequence = ++logins_;
    slot.opened_us = esp_timer_get_time();

    std::ranges::copy(slot.id, id.begin());
}

bool SessionStore::holds(std::string_view id) const {
    const std::int64_t now_us{esp_timer_get_time()};
    bool held{false};
    for (const Slot& slot : slots_) {
        // An expired session no longer counts, and its slot is free for the next
        // login.
        if (slot.sequence == 0 || (now_us - slot.opened_us) >= LIFETIME_US) {
            continue;
        }
        // Compares every held session, keeping timing from revealing which one
        // matched.
        held = string_utils::is_constant_time_equal(id, slot.id.data()) || held;
    }
    return held;
}

void SessionStore::close(std::string_view id) {
    for (Slot& slot : slots_) {
        if (slot.sequence != 0 && string_utils::is_constant_time_equal(id, slot.id.data())) {
            slot = {};
        }
    }
}

SessionStore::Slot& SessionStore::next() {
    for (Slot& slot : slots_) {
        if (slot.sequence == 0) {
            return slot;
        }
    }
    return *std::ranges::min_element(slots_, {}, &Slot::sequence);
}
