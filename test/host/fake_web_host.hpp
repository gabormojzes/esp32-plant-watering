#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "messaging/payload.hpp"
#include "web/web_host.hpp"

// One request a test hands to a claimed route, and the answer it records. It
// stands where the server's own exchange stands, showing a route exactly what
// it would see on the device.
class FakeHttpExchange : public HttpExchange {
public:
    struct RecordedHeader {
        const char* name{nullptr};
        const char* value{nullptr};
    };

    std::size_t body_length() const override {
        return declared_length.value_or(request_body.size());
    }

    [[nodiscard]] BodyResult read_body(std::span<char> body, int /*retries*/) override {
        if (body_outcome != BodyResult::Complete) {
            return body_outcome;
        }
        const std::size_t taken{std::min(request_body.size(), body.size())};
        std::ranges::copy(request_body.substr(0, taken), body.begin());
        return BodyResult::Complete;
    }

    // Supplies a request header beyond the content type, e.g. Cookie or
    // Authorization
    void set_request_header(const char* name, const char* value) {
        if (request_header_count_ < request_headers_.size()) {
            request_headers_[request_header_count_++] = {.name = name, .value = value};
        }
    }

    [[nodiscard]] bool header(const char* name, std::span<char> value) const override {
        const char* stored{request_header_value(name)};
        if (stored == nullptr) {
            return false;
        }
        const std::string_view text{stored};
        // A value that does not fit is reported as missing, as the server does.
        if (text.size() >= value.size()) {
            return false;
        }
        std::ranges::copy(text, value.begin());
        value[text.size()] = '\0';
        return true;
    }

    void set_status(HttpStatus answered) override {
        status = answered;
    }

    void set_content_type(const char* media_type) override {
        response_media_type = media_type;
        response_charset = nullptr;
    }

    void set_content_type(const char* media_type, const char* charset) override {
        response_media_type = media_type;
        response_charset = charset;
    }

    void set_header(const char* name, const char* value) override {
        if (header_count_ < headers_.size()) {
            headers_[header_count_++] = {.name = name, .value = value};
        }
    }

    void send(std::span<const char> body) override {
        sent = true;
        sent_length_ = std::min(body.size(), sent_body_.size());
        std::ranges::copy(body.first(sent_length_), sent_body_.begin());
    }

    std::string_view sent_text() const {
        return {sent_body_.data(), sent_length_};
    }

    // Returns the value the route queued under `name`, or empty when it queued
    // none.
    std::string_view header_value(std::string_view name) const {
        for (const RecordedHeader& header : std::span{headers_}.first(header_count_)) {
            if (name == header.name) {
                return header.value;
            }
        }
        return {};
    }

    bool has_header(std::string_view name) const {
        return !header_value(name).empty();
    }

    // What the client sent. No content type is a request carrying none.
    const char* request_content_type{nullptr};
    std::string_view request_body;
    // What the client claims it is sending, when that is not what it holds
    std::optional<std::size_t> declared_length;
    // How the socket behaves once the body is read
    BodyResult body_outcome{BodyResult::Complete};

    // What the route answered. No status is the 200 a host serves by default.
    std::optional<HttpStatus> status;
    const char* response_media_type{nullptr};
    // No charset is a content type sent bare
    const char* response_charset{nullptr};
    bool sent{false};

private:
    // More than any route here queues
    static constexpr std::size_t MAX_HEADERS{8};

    const char* request_header_value(const char* name) const {
        const std::string_view wanted{name};
        if (wanted == "Content-Type") {
            return request_content_type;
        }
        for (const RecordedHeader& header :
             std::span{request_headers_}.first(request_header_count_)) {
            if (wanted == header.name) {
                return header.value;
            }
        }
        return nullptr;
    }

    std::array<RecordedHeader, MAX_HEADERS> headers_{};
    std::size_t header_count_{0};
    std::array<RecordedHeader, MAX_HEADERS> request_headers_{};
    std::size_t request_header_count_{0};
    std::array<char, Payload::BUFFER_SIZE> sent_body_{};
    std::size_t sent_length_{0};
};

// A host that claims routes without serving anything, and lets a test call one
// the way the server's task would.
class FakeWebHost : public WebHost {
public:
    struct Registration {
        const char* uri{nullptr};
        HttpMethod method{};
        AccessSet access;
        Handler handler{nullptr};
        void* context{nullptr};
    };

    void start() override {
        started = true;
    }

    void tick(std::int64_t /*now_us*/) override {}

    void route(
        const char* uri, HttpMethod method, AccessSet access, Handler handler, void* context
    ) override {
        if (route_count_ < routes_.size()) {
            routes_[route_count_++] = {
                .uri = uri,
                .method = method,
                .access = access,
                .handler = handler,
                .context = context
            };
        }
    }

    std::size_t route_count() const {
        return route_count_;
    }

    bool claimed(std::string_view uri, HttpMethod method) const {
        return find(uri, method) != nullptr;
    }

    // The access a route was claimed with, or the empty set for an unclaimed one.
    // Check claimed() first.
    AccessSet access_of(std::string_view uri, HttpMethod method) const {
        const Registration* route{find(uri, method)};
        return route == nullptr ? AccessSet{} : route->access;
    }

    // Calls a claimed route, or returns false when none answers. A call runs the
    // route's real handler.
    bool dispatch(std::string_view uri, HttpMethod method, FakeHttpExchange& exchange) {
        const Registration* route{find(uri, method)};
        if (route == nullptr) {
            return false;
        }
        route->handler(exchange, route->context);
        return true;
    }

    bool started{false};

private:
    // More than any endpoint here claims
    static constexpr std::size_t MAX_ROUTES{8};

    const Registration* find(std::string_view uri, HttpMethod method) const {
        for (const Registration& route : std::span{routes_}.first(route_count_)) {
            if (uri == route.uri && method == route.method) {
                return &route;
            }
        }
        return nullptr;
    }

    std::array<Registration, MAX_ROUTES> routes_{};
    std::size_t route_count_{0};
};
