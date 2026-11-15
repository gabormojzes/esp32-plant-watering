#include "web/access_gate.hpp"

#include <array>
#include <cstddef>
#include <string_view>

#include "shared/http_utils.hpp"
#include "shared/media_types.hpp"
#include "web/bearer_auth.hpp"
#include "web/cors.hpp"
#include "web/http_exchange.hpp"
#include "web/session_cookie.hpp"
#include "web/session_store.hpp"

namespace {

// The WWW-Authenticate value, naming the scheme to retry with and the realm
constexpr char CHALLENGE[]{R"(Bearer realm="Plant watering")"};

// The longest Sec-Fetch-Site value the Fetch standard defines, and its
// terminator. A longer value is not one the gate refuses.
constexpr std::size_t SEC_FETCH_SITE_BUFFER_SIZE{std::string_view{"same-origin"}.size() + 1};

}  // namespace

AccessGate::AccessGate(
    const Config& config, const BearerAuth& bearer_auth, const SessionStore& session_store
)
    : config_{config}, bearer_auth_{bearer_auth}, session_store_{session_store} {}

bool AccessGate::granted(AccessSet access, const HttpExchange& exchange) const {
    return access.has(Access::Open) || (access.has(Access::Token) && has_token(exchange)) ||
           (access.has(Access::SessionNotFromAnotherOrigin) &&
            has_session_not_from_another_origin(exchange)) ||
           (access.has(Access::Session) && has_session(exchange));
}

void AccessGate::refuse(AccessSet access, HttpExchange& exchange) const {
    // A route that takes no token is a page, and its browser is sent to log in.
    if (!access.has(Access::Token)) {
        exchange.set_status(HttpStatus::Found);
        exchange.set_header("Location", config_.login_uri);
        exchange.send_empty();
        return;
    }

    exchange.set_status(HttpStatus::Unauthorized);
    exchange.set_header("WWW-Authenticate", CHALLENGE);
    // Without it a cross-origin page reads a blocked response rather than the
    // 401, and cannot tell a refused token from an unreachable device.
    cors::allow_any_origin(exchange);
    exchange.set_content_type(media_types::TEXT_PLAIN);
    exchange.send_text("Unauthorized");
}

bool AccessGate::has_session(const HttpExchange& exchange) const {
    return session_store_.holds(session_cookie::read(exchange));
}

bool AccessGate::has_session_not_from_another_origin(const HttpExchange& exchange) const {
    // A browser carries the cookie on a request another origin of the same site
    // makes, which SameSite=Strict lets through.
    std::array<char, SEC_FETCH_SITE_BUFFER_SIZE> site{};
    if (exchange.header("Sec-Fetch-Site", site) &&
        http_utils::is_from_another_origin(site.data())) {
        return false;
    }
    return has_session(exchange);
}

bool AccessGate::has_token(const HttpExchange& exchange) const {
    std::array<char, BearerAuth::HEADER_BUFFER_SIZE> authorization{};
    if (!exchange.header("Authorization", authorization)) {
        return false;
    }
    return bearer_auth_.granted_for(authorization.data());
}
