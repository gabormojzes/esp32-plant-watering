#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "fault/fault.hpp"
#include "fault/fault_definitions.hpp"
#include "fault/fault_sink.hpp"

// Holds the device's faults, one entry per code however often it is raised.
// Sized for every fault in fault_definitions::ALL. Once full, a new code is dropped
// rather than displacing one already held. Clearing marks an entry
// inactive and keeps its record. Memory only; a reboot starts empty.
class FaultStore : public FaultSink {
public:
    void accept(const Fault& fault) override {
        const std::scoped_lock lock{mutex_};

        const std::span<FaultEntry> held{std::span{entries_}.first(count_)};
        const auto existing{std::ranges::find(held, std::string_view{fault.code}, spelling)};
        if (existing != held.end()) {
            existing->active = true;
            publish_worst();
            return;
        }

        if (count_ == fault_definitions::ALL.size()) {
            return;
        }
        entries_[count_++] = {.fault = fault, .active = true};
        publish_worst();
    }

    void clear(const char* code) override {
        const std::scoped_lock lock{mutex_};

        const std::span<FaultEntry> held{std::span{entries_}.first(count_)};
        const auto existing{std::ranges::find(held, std::string_view{code}, spelling)};
        if (existing == held.end()) {
            return;
        }
        existing->active = false;
        publish_worst();
    }

    // Lock-free, reading the value the writers publish.
    std::optional<Severity> worst_severity() const {
        const int worst{worst_.load()};
        if (worst == NONE) {
            return std::nullopt;
        }
        return static_cast<Severity>(worst);
    }

    [[nodiscard]] std::size_t snapshot(std::span<FaultEntry> faults) const {
        const std::scoped_lock lock{mutex_};

        const std::size_t written{std::min(faults.size(), count_)};
        std::copy_n(entries_.begin(), written, faults.begin());
        return written;
    }

private:
    static constexpr int NONE{-1};

    // Compared by spelling, because codes are pointers and two codes spelled the
    // same need not share one.
    static constexpr std::string_view spelling(const FaultEntry& entry) {
        return entry.fault.code;
    }

    // Recomputes the worst from the active entries, letting it fall as well as
    // rise. Call with the lock held.
    void publish_worst() {
        int worst{NONE};
        for (const FaultEntry& entry : std::span{entries_}.first(count_)) {
            if (entry.active) {
                worst = std::max<int>(worst, std::to_underlying(entry.fault.severity));
            }
        }
        worst_.store(worst);
    }

    // Raises come from any task, and snapshot() reads what they write
    mutable std::mutex mutex_;
    std::array<FaultEntry, fault_definitions::ALL.size()> entries_{};
    std::size_t count_{0};
    // Published by the writers, read without the lock
    std::atomic<int> worst_{NONE};
};
