#pragma once

#include <span>

#include "fault/fault.hpp"
#include "fault/fault_sink.hpp"

// Ambient singleton, letting any component report a fault without being handed
// a reference. Forwards each call to the installed sinks and keeps nothing.
class FaultManager {
public:
    static FaultManager& instance() {
        static FaultManager manager{};
        return manager;
    }

    // A copy would carry its own sink list, losing anything raised on it.
    FaultManager(const FaultManager&) = delete;
    FaultManager& operator=(const FaultManager&) = delete;

    // Installs the sinks, called in order. Call once at startup, before any task
    // that raises exists.
    void install(std::span<FaultSink* const> fault_sinks) {
        fault_sinks_ = fault_sinks;
    }

    // Raise and clear are idempotent, and safe to call from any task but not from
    // an interrupt.
    void raise(const Fault& fault) {
        for (FaultSink* sink : fault_sinks_) {
            sink->accept(fault);
        }
    }

    void clear(const Fault& fault) {
        for (FaultSink* sink : fault_sinks_) {
            sink->clear(fault.code);
        }
    }

private:
    FaultManager() = default;

    std::span<FaultSink* const> fault_sinks_;
};
