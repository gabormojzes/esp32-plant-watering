#include "web/session_cookie.hpp"

#include <array>

#include "web/cookie.hpp"
#include "web/http_exchange.hpp"

std::string_view session_cookie::read(const HttpExchange& exchange) {
    // Static, because a header this long does not fit the calling task's stack.
    static std::array<char, BUFFER_SIZE> cookies{};
    if (!exchange.header("Cookie", cookies)) {
        return {};
    }
    return cookie::value(cookies.data(), NAME);
}
