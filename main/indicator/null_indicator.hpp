#pragma once

#include <cstdint>

#include "indicator/indicator.hpp"

class NullIndicator : public Indicator {
public:
    void init() override {}
    void update(std::int64_t /*now_us*/) override {}
};
