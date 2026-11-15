#include "web/pages/login.hpp"

#include <cJSON.h>
#include <esp_log.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

#include "shared/charsets.hpp"
#include "shared/constants.hpp"
#include "shared/http_utils.hpp"
#include "shared/json_utils.hpp"
#include "shared/media_types.hpp"
#include "web/http_exchange.hpp"
#include "web/session_cookie.hpp"

extern const char LOGIN_HTML_GZ_START[] asm("_binary_login_html_gz_start");
extern const std::uint32_t LOGIN_HTML_GZ_LENGTH asm("login_html_gz_length");

namespace {

constexpr char TAG[]{"LOGIN"};

// Not Basic, which would open the browser's own password dialog over the page.
constexpr char CHALLENGE[]{R"(Form realm="Plant watering")"};

constexpr std::size_t JSON_CONTENT_TYPE_LENGTH{
    std::string_view{media_types::APPLICATION_JSON}.size()
};

// HttpOnly keeps the id out of the page's own scripts, and SameSite=Strict
// keeps it off every request another site makes.
// Secure is left off, because the device serves plain HTTP and a Secure cookie
// would never come back.
constexpr char COOKIE_ATTRIBUTES[]{"; HttpOnly; SameSite=Strict; Path=/"};
// Keeps the cookie across a browser restart for as long as its session stands.
constexpr std::int64_t COOKIE_MAX_AGE_S{SessionStore::LIFETIME_US / constants::US_PER_S};
constexpr std::size_t MAX_AGE_ATTRIBUTE_LENGTH{
    std::string_view{"; Max-Age="}.size() + std::numeric_limits<std::int64_t>::digits10 + 1
};
// What expires the cookie a browser already holds
constexpr char EXPIRED[]{"; Max-Age=0"};

// A quiet socket gets one retry before the client is treated as gone.
constexpr int BODY_RETRIES{1};

bool has_json_content_type(const HttpExchange& exchange) {
    // Holds the type and any parameters, such as "; charset=utf-8". A longer
    // value reads as truncated and is refused.
    std::array<char, Login::CONTENT_TYPE_BUFFER_SIZE> content_type{};
    static_assert(
        JSON_CONTENT_TYPE_LENGTH < content_type.size(),
        "the type alone must leave a slot for the character that ends it"
    );
    if (!exchange.header("Content-Type", content_type)) {
        return false;
    }
    return http_utils::is_media_type(content_type.data(), media_types::APPLICATION_JSON);
}

// Empty unless the field is a string. The view points into the document and
// dies with it.
std::string_view string_field(const cJSON* object, const char* key) {
    const cJSON* item{cJSON_GetObjectItemCaseSensitive(object, key)};
    if (cJSON_IsString(item) == 0 || item->valuestring == nullptr) {
        return {};
    }
    return item->valuestring;
}

}  // namespace

Login::Login(
    WebHost& web_host,
    SessionStore& session_store,
    const PasswordAuth& password_auth,
    const Config& config
)
    : web_host_{web_host},
      session_store_{session_store},
      password_auth_{password_auth},
      config_{config} {}

void Login::start() {
    web_host_.route(config_.uri, HttpMethod::Get, {Access::Open}, &Login::on_page, this);
    // No preflight is claimed alongside it, since a login is only ever offered by
    // the page the device serves itself.
    web_host_.route(config_.uri, HttpMethod::Post, {Access::Open}, &Login::on_submit, this);
    // Behind the session, which keeps a page on another origin from posting here to
    // clear the cookie. A browser whose session is gone keeps an id that matches
    // nothing.
    web_host_.route(
        config_.logout_uri,
        HttpMethod::Post,
        {Access::SessionNotFromAnotherOrigin},
        &Login::on_logout,
        this
    );
}

void Login::on_page(HttpExchange& exchange, void* context) {
    static_cast<const Login*>(context)->page(exchange);
}

void Login::on_submit(HttpExchange& exchange, void* context) {
    static_cast<Login*>(context)->submit(exchange);
}

void Login::on_logout(HttpExchange& exchange, void* context) {
    static_cast<Login*>(context)->logout(exchange);
}

void Login::page(HttpExchange& exchange) const {
    if (session_store_.holds(session_cookie::read(exchange))) {
        exchange.set_status(HttpStatus::Found);
        exchange.set_header("Location", config_.dashboard_uri);
        exchange.send_empty();
        return;
    }

    exchange.set_header("Cache-Control", "no-store");
    exchange.set_header("Content-Security-Policy", CONTENT_SECURITY_POLICY);
    exchange.set_content_type(media_types::TEXT_HTML, charsets::UTF8);
    exchange.set_header("Content-Encoding", "gzip");
    exchange.send({LOGIN_HTML_GZ_START, LOGIN_HTML_GZ_LENGTH});
}

void Login::submit(HttpExchange& exchange) {
    if (!has_json_content_type(exchange)) {
        exchange.set_status(HttpStatus::UnsupportedMediaType);
        exchange.send_empty();
        return;
    }

    const std::size_t length{exchange.body_length()};
    if (length > body_.size()) {
        exchange.set_status(HttpStatus::ContentTooLarge);
        exchange.send_empty();
        return;
    }

    switch (exchange.read_body({body_.data(), length}, BODY_RETRIES)) {
        case BodyResult::TimedOut:
            exchange.set_status(HttpStatus::RequestTimeout);
            exchange.send_empty();
            return;
        case BodyResult::Failed:
            exchange.set_status(HttpStatus::BadRequest);
            exchange.send_empty();
            return;
        case BodyResult::Complete:
            break;
    }

    const std::string_view body{body_.data(), length};
    // Decided before the document is deleted, since the credentials are read from
    // it.
    cJSON* doc{cJSON_ParseWithLength(body.data(), body.size())};
    const bool granted{
        !json_utils::carries_nul(body) && doc != nullptr &&
        password_auth_.granted_for(string_field(doc, "user"), string_field(doc, "password"))
    };
    cJSON_Delete(doc);

    if (!granted) {
        ESP_LOGW(TAG, "Login refused");
        exchange.set_status(HttpStatus::Unauthorized);
        exchange.set_header("WWW-Authenticate", CHALLENGE);
        exchange.set_content_type(media_types::TEXT_PLAIN);
        exchange.send_text("Unauthorized");
        return;
    }

    std::array<char, SessionStore::ID_BUFFER_SIZE> id{};
    session_store_.open(id);

    static_assert(
        SET_COOKIE_BUFFER_SIZE >=
            std::string_view{session_cookie::NAME}.size() + 1 + (SessionStore::ID_BUFFER_SIZE - 1) +
                MAX_AGE_ATTRIBUTE_LENGTH + std::string_view{COOKIE_ATTRIBUTES}.size() + 1,
        "SET_COOKIE_BUFFER_SIZE cannot hold the cookie as it is set"
    );
    std::snprintf(
        set_cookie_.data(),
        set_cookie_.size(),
        "%s=%s; Max-Age=%lld%s",
        session_cookie::NAME,
        id.data(),
        static_cast<long long>(COOKIE_MAX_AGE_S),
        COOKIE_ATTRIBUTES
    );

    ESP_LOGI(TAG, "Session opened");
    exchange.set_status(HttpStatus::NoContent);
    exchange.set_header("Set-Cookie", set_cookie_.data());
    exchange.send_empty();
}

void Login::logout(HttpExchange& exchange) {
    session_store_.close(session_cookie::read(exchange));

    std::snprintf(
        set_cookie_.data(),
        set_cookie_.size(),
        "%s=%s%s",
        session_cookie::NAME,
        EXPIRED,
        COOKIE_ATTRIBUTES
    );

    exchange.set_status(HttpStatus::NoContent);
    exchange.set_header("Set-Cookie", set_cookie_.data());
    exchange.send_empty();
}
