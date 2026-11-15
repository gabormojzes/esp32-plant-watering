#pragma once

#include "web/web_host.hpp"

class NullWebHost : public WebHost {
public:
    void start() override {}

    void tick(std::int64_t /*now_us*/) override {}

    void route(
        const char* /*uri*/,
        HttpMethod /*method*/,
        AccessSet /*access*/,
        Handler /*handler*/,
        void* /*context*/
    ) override {}
};
