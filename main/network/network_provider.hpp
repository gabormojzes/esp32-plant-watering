#pragma once

#include "network/network_info.hpp"

class NetworkProvider {
public:
    virtual ~NetworkProvider() = default;
    // Creates the provider's network interfaces before returning. Joining a
    // network may continue afterward.
    virtual void init() = 0;
    // For display only
    virtual NetworkInfo info() const = 0;
};
