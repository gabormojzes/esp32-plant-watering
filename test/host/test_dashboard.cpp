#include <doctest.h>

#include <cstdint>
#include <string_view>

#include "fake_web_host.hpp"
#include "web/pages/dashboard.hpp"

// Stands in for the gzipped dashboard embedded on the device. The route is under
// test, not what the dashboard holds.
extern const char DASHBOARD_HTML_GZ_START[] asm("_binary_dashboard_html_gz_start");
extern const std::uint32_t DASHBOARD_HTML_GZ_LENGTH asm("dashboard_html_gz_length");
constexpr char DASHBOARD_HTML_GZ_START[]{"<p>page"};
constexpr std::uint32_t DASHBOARD_HTML_GZ_LENGTH{
    static_cast<std::uint32_t>(std::string_view{DASHBOARD_HTML_GZ_START}.size())
};

namespace {

constexpr char DASHBOARD_URI[]{"/"};

constexpr Dashboard::Config CONFIG{
    .uri = DASHBOARD_URI,
};

}  // namespace

TEST_CASE("the route is claimed behind the session") {
    FakeWebHost web_host{};
    Dashboard dashboard{web_host, CONFIG};

    dashboard.start();

    REQUIRE(web_host.claimed(DASHBOARD_URI, HttpMethod::Get));
    CHECK(web_host.access_of(DASHBOARD_URI, HttpMethod::Get) == AccessSet{Access::Session});
    CHECK(web_host.route_count() == 1);
}

TEST_CASE("the dashboard is served as it is embedded") {
    FakeWebHost web_host{};
    Dashboard dashboard{web_host, CONFIG};
    dashboard.start();
    FakeHttpExchange exchange{};

    REQUIRE(web_host.dispatch(DASHBOARD_URI, HttpMethod::Get, exchange));

    CHECK(exchange.response_media_type == std::string_view{"text/html"});
    CHECK(exchange.response_charset == std::string_view{"utf-8"});
    CHECK(exchange.header_value("Content-Encoding") == std::string_view{"gzip"});
    CHECK(exchange.sent_text() == std::string_view{DASHBOARD_HTML_GZ_START});
}

TEST_CASE("the dashboard is served uncacheable") {
    FakeWebHost web_host{};
    Dashboard dashboard{web_host, CONFIG};
    dashboard.start();
    FakeHttpExchange exchange{};

    REQUIRE(web_host.dispatch(DASHBOARD_URI, HttpMethod::Get, exchange));

    CHECK(exchange.header_value("Cache-Control") == std::string_view{"no-store"});
}

TEST_CASE("the dashboard is served under its content security policy") {
    FakeWebHost web_host{};
    Dashboard dashboard{web_host, CONFIG};
    dashboard.start();
    FakeHttpExchange exchange{};

    REQUIRE(web_host.dispatch(DASHBOARD_URI, HttpMethod::Get, exchange));

    CHECK(
        exchange.header_value("Content-Security-Policy") ==
        std::string_view{Dashboard::CONTENT_SECURITY_POLICY}
    );
}
