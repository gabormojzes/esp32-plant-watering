#include <cJSON.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "fake_web_host.hpp"
#include "shared/json_utils.hpp"
#include "web/pages/login.hpp"
#include "web/password_auth.hpp"
#include "web/session_store.hpp"

// Stands in for the page embedded on the device, which no input reaches.
extern const char LOGIN_HTML_GZ_START[] asm("_binary_login_html_gz_start");
extern const std::uint32_t LOGIN_HTML_GZ_LENGTH asm("login_html_gz_length");
const char LOGIN_HTML_GZ_START[]{""};
const std::uint32_t LOGIN_HTML_GZ_LENGTH{0};

namespace {

constexpr char LOGIN_URI[]{"/login"};
constexpr char USER[]{"user"};
constexpr char PASSWORD[]{"password"};

constexpr Login::Config CONFIG{
    .uri = LOGIN_URI,
    .logout_uri = "/logout",
    .dashboard_uri = "/",
};

// Read with cJSON directly, which judges the body apart from the code under test.
// A NUL would cut a decoded credential short, and a body carrying one names none.
bool names_the_credentials(std::string_view body) {
    if (json_utils::carries_nul(body)) {
        return false;
    }
    cJSON* doc{cJSON_ParseWithLength(body.data(), body.size())};
    const cJSON* user{cJSON_GetObjectItemCaseSensitive(doc, "user")};
    const cJSON* password{cJSON_GetObjectItemCaseSensitive(doc, "password")};
    const bool named{
        cJSON_IsString(user) != 0 && cJSON_IsString(password) != 0 &&
        std::string_view{user->valuestring} == USER &&
        std::string_view{password->valuestring} == PASSWORD
    };
    cJSON_Delete(doc);
    return named;
}

}  // namespace

// Posts any body to the login route. A session opens exactly when the route reads
// the body and it names the configured credentials.
// libFuzzer calls the entry point by this name.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    FakeWebHost web_host{};
    SessionStore session_store{};
    const PasswordAuth password_auth{PasswordAuth::Config{.user = USER, .password = PASSWORD}};
    Login login{web_host, session_store, password_auth, CONFIG};
    login.start();

    const std::string_view body{reinterpret_cast<const char*>(data), size};
    FakeHttpExchange exchange{};
    exchange.request_content_type = "application/json";
    exchange.request_body = body;
    if (!web_host.dispatch(LOGIN_URI, HttpMethod::Post, exchange)) {
        std::abort();
    }

    // A body past the route's limit is refused unread
    const bool read_body{exchange.status != HttpStatus::ContentTooLarge};
    const bool opened{exchange.status == HttpStatus::NoContent};
    if (opened != (read_body && names_the_credentials(body))) {
        std::abort();
    }
    return 0;
}
