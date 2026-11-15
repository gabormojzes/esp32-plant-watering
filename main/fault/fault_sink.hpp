#pragma once

#include "fault/fault.hpp"

// Receives faults as they are raised and cleared, and decides what to keep.
// Both calls come from any task, concurrently, and run on the reporting task. A
// sink with slow work queues it instead of doing it here.
class FaultSink {
public:
    virtual ~FaultSink() = default;
    virtual void accept(const Fault& fault) = 0;
    virtual void clear(const char* code) = 0;
};
