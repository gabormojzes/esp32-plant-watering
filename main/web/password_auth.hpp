#pragma once

#include <cstddef>
#include <string_view>

class PasswordAuth {
public:
    // Neither is empty, which would match a login leaving that half out.
    struct Config {
        const char* user;
        const char* password;
    };

    // Longest credentials that can match, terminator included
    static constexpr std::size_t MAX_USER_SIZE{32};
    static constexpr std::size_t MAX_PASSWORD_SIZE{32};

    explicit PasswordAuth(const Config& config);

    bool granted_for(std::string_view user, std::string_view password) const;

private:
    std::string_view user_;
    std::string_view password_;
};
