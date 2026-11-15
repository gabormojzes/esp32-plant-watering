#pragma once

#include <cstddef>
#include <string_view>

class HttpExchange;

// The cookie a browser carries its session in
namespace session_cookie {

inline constexpr char NAME[]{"session"};
// Holds any Cookie header that arrives, since CONFIG_HTTPD_MAX_REQ_HDR_LEN in
// sdkconfig.defaults caps a request's whole header block at this size. A
// longer header is refused rather than truncated.
inline constexpr std::size_t BUFFER_SIZE{2048};

// Returns the session id the request carries, or empty. The view points into a
// buffer the next read overwrites, and every read comes from one task.
[[nodiscard]] std::string_view read(const HttpExchange& exchange);

}  // namespace session_cookie
