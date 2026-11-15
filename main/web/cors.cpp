#include "web/cors.hpp"

void cors::allow_any_origin(HttpExchange& exchange) {
    // The header is kept until the send, which a literal outlives.
    exchange.set_header("Access-Control-Allow-Origin", "*");
}

void cors::preflight(HttpExchange& exchange, const char* methods) {
    allow_any_origin(exchange);
    exchange.set_header("Access-Control-Allow-Methods", methods);
    // Authorization has to be named, because it is not safelisted and a
    // wildcard here is defined not to cover it.
    exchange.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    // The policy is compiled in, and a cached answer cannot go stale.
    exchange.set_header("Access-Control-Max-Age", "86400");
    exchange.set_status(HttpStatus::NoContent);
    exchange.send_empty();
}
