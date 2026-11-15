#pragma once

#include <cstddef>
#include <string_view>

class BearerAuth {
public:
    struct Config {
        // Never empty, which would match a request carrying no token
        const char* api_token;
    };

    // Longest token that can match, terminator included
    static constexpr std::size_t MAX_API_TOKEN_SIZE{64};
    // "Bearer ", the token and a terminator. A longer header, padding included,
    // is refused rather than truncated.
    static constexpr std::size_t HEADER_BUFFER_SIZE{
        std::string_view{"Bearer "}.size() + MAX_API_TOKEN_SIZE
    };

    explicit BearerAuth(const Config& config);

    // Takes the whole Authorization value, scheme included.
    bool granted_for(std::string_view authorization) const;

private:
    std::string_view api_token_;
};
