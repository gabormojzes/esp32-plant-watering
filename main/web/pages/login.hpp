#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "web/password_auth.hpp"
#include "web/session_store.hpp"
#include "web/web_host.hpp"

class Login {
public:
    struct Config {
        const char* uri;
        const char* logout_uri;
        const char* dashboard_uri;
    };

    // Room for a posted Content-Type, parameters and terminator included
    static constexpr std::size_t CONTENT_TYPE_BUFFER_SIZE{64};
    static constexpr char CONTENT_SECURITY_POLICY[]{
        "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
        "connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'"
    };

    Login(
        WebHost& web_host,
        SessionStore& session_store,
        const PasswordAuth& password_auth,
        const Config& config
    );
    // The routes keep a pointer to this object, never to a copy.
    Login(const Login&) = delete;
    Login& operator=(const Login&) = delete;
    // Only claims the routes.
    void start();

private:
    // Longest login body read. A longer one is refused unread.
    static constexpr std::size_t BODY_BUFFER_SIZE{512};
    // The cookie as it is set, and its terminator
    static constexpr std::size_t SET_COOKIE_BUFFER_SIZE{112};

    // Every character of a credential may arrive as a \u00XX escape, which is
    // the longest JSON spells one.
    static_assert(
        BODY_BUFFER_SIZE >= std::string_view{R"({"user":"","password":""})"}.size() +
                                ((PasswordAuth::MAX_USER_SIZE - 1) * 6) +
                                ((PasswordAuth::MAX_PASSWORD_SIZE - 1) * 6),
        "BODY_BUFFER_SIZE cannot hold a login body with every character escaped"
    );

    static void on_page(HttpExchange& exchange, void* context);
    static void on_submit(HttpExchange& exchange, void* context);
    static void on_logout(HttpExchange& exchange, void* context);

    void page(HttpExchange& exchange) const;
    void submit(HttpExchange& exchange);
    void logout(HttpExchange& exchange);

    WebHost& web_host_;
    SessionStore& session_store_;
    const PasswordAuth& password_auth_;
    Config config_{};
    // A member, because the header is queued until the response is sent
    std::array<char, SET_COOKIE_BUFFER_SIZE> set_cookie_{};
    std::array<char, BODY_BUFFER_SIZE> body_{};
};
