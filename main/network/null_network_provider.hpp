#pragma once

#include "network/network_info.hpp"
#include "network/network_provider.hpp"

class NullNetworkProvider : public NetworkProvider {
public:
    void init() override {}
    NetworkInfo info() const override {
        return {};
    }
};
