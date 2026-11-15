#pragma once

#include "fault/fault.hpp"
#include "fault/fault_manager.hpp"

// Free functions over the ambient FaultManager. A raise names the fault, never
// where it goes.
namespace faults {

inline void raise(const Fault& fault) {
    FaultManager::instance().raise(fault);
}

inline void clear(const Fault& fault) {
    FaultManager::instance().clear(fault);
}

}  // namespace faults
