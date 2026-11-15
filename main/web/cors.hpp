#pragma once

#include "web/http_exchange.hpp"

// Cross-origin headers for a response. They are queued rather than sent, which
// lets one call cover whatever response the handler goes on to make.
namespace cors {

void allow_any_origin(HttpExchange& exchange);
// Answers a preflight, allowing `methods` (e.g. "POST, OPTIONS") from any
// origin. `methods` must outlive the response.
void preflight(HttpExchange& exchange, const char* methods);

}  // namespace cors
