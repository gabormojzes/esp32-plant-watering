#include "web/bearer_auth.hpp"

#include "shared/http_utils.hpp"
#include "shared/string_utils.hpp"

namespace {

constexpr char SCHEME[]{"Bearer"};
constexpr std::size_t SCHEME_LENGTH{std::string_view{SCHEME}.size()};

// What may part the scheme from the token. Never part of the token itself, since
// token68 admits neither.
constexpr char WHITESPACE[]{" \t"};

// Returns the token from an Authorization value, or empty when it carries no
// Bearer token. Per RFC 9110 the value is a case-insensitive scheme, one or
// more spaces and the credential, with optional whitespace around it.
std::string_view bearer_token(std::string_view authorization) {
    const std::string_view field{string_utils::trimmed(authorization)};

    if (field.size() <= SCHEME_LENGTH) {
        return {};
    }
    if (!http_utils::is_ascii_case_insensitive_equal(field.substr(0, SCHEME_LENGTH), SCHEME)) {
        return {};
    }
    // A separator must follow the scheme, since a token starting right after it
    // means a longer scheme that only starts like Bearer. npos means no token at all.
    const std::size_t token_at{field.find_first_not_of(WHITESPACE, SCHEME_LENGTH)};
    if (token_at == SCHEME_LENGTH || token_at == std::string_view::npos) {
        return {};
    }
    return field.substr(token_at);
}

}  // namespace

BearerAuth::BearerAuth(const Config& config) : api_token_{config.api_token} {}

bool BearerAuth::granted_for(std::string_view authorization) const {
    // The empty token of a header without one never matches, since the
    // configured token is never empty.
    return string_utils::is_constant_time_equal(bearer_token(authorization), api_token_);
}
