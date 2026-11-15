#include "web/pages/dashboard.hpp"

#include <cstdint>

#include "shared/charsets.hpp"
#include "shared/media_types.hpp"

extern const char DASHBOARD_HTML_GZ_START[] asm("_binary_dashboard_html_gz_start");
extern const std::uint32_t DASHBOARD_HTML_GZ_LENGTH asm("dashboard_html_gz_length");

Dashboard::Dashboard(WebHost& web_host, const Config& config)
    : web_host_{web_host}, config_{config} {}

void Dashboard::start() {
    web_host_.route(config_.uri, HttpMethod::Get, {Access::Session}, &Dashboard::on_request, this);
}

void Dashboard::on_request(HttpExchange& exchange, void* /*context*/) {
    page(exchange);
}

void Dashboard::page(HttpExchange& exchange) {
    exchange.set_header("Cache-Control", "no-store");
    exchange.set_header("Content-Security-Policy", CONTENT_SECURITY_POLICY);
    exchange.set_content_type(media_types::TEXT_HTML, charsets::UTF8);
    exchange.set_header("Content-Encoding", "gzip");
    exchange.send({DASHBOARD_HTML_GZ_START, DASHBOARD_HTML_GZ_LENGTH});
}
