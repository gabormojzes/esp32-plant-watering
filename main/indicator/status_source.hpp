#pragma once

#include <optional>

#include "indicator/status.hpp"

// One view of the device's status
class StatusSource {
public:
    virtual ~StatusSource() = default;
    // Nullopt when this source has nothing to say.
    virtual std::optional<Status> status() const = 0;
};
