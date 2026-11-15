#pragma once

// One self-consistent view of the pump's own state
struct PumpSnapshot {
    bool on{false};
    bool faulted{false};  // The pump cannot switch
};
