#include "web/password_auth.hpp"

#include "shared/string_utils.hpp"

PasswordAuth::PasswordAuth(const Config& config) : user_{config.user}, password_{config.password} {}

bool PasswordAuth::granted_for(std::string_view user, std::string_view password) const {
    // Neither half short-circuits the other, because an early answer would
    // reveal which one was wrong.
    const bool user_matches{string_utils::is_constant_time_equal(user, user_)};
    const bool password_matches{string_utils::is_constant_time_equal(password, password_)};
    return user_matches && password_matches;
}
