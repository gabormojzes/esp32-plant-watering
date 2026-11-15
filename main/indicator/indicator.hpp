#pragma once

#include <cstdint>

class Indicator {
public:
    virtual ~Indicator() = default;
    // Shows the Booting status until the first update().
    virtual void init() = 0;
    virtual void update(std::int64_t now_us) = 0;
};
