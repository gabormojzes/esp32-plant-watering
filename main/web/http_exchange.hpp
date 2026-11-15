#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

enum class HttpMethod : std::uint8_t {
    Get,
    Post,
    Options
};

// Every status a handler answers with, valued as RFC 9110 numbers it
enum class HttpStatus : std::uint16_t {
    Accepted = 202,
    NoContent = 204,
    Found = 302,
    BadRequest = 400,
    Unauthorized = 401,
    RequestTimeout = 408,
    ContentTooLarge = 413,
    UnsupportedMediaType = 415,
    ServiceUnavailable = 503
};

// The status's name as RFC 9110 gives it
constexpr const char* reason_phrase(HttpStatus status) {
    switch (status) {
        case HttpStatus::Accepted:
            return "Accepted";
        case HttpStatus::NoContent:
            return "No Content";
        case HttpStatus::Found:
            return "Found";
        case HttpStatus::BadRequest:
            return "Bad Request";
        case HttpStatus::Unauthorized:
            return "Unauthorized";
        case HttpStatus::RequestTimeout:
            return "Request Timeout";
        case HttpStatus::ContentTooLarge:
            return "Content Too Large";
        case HttpStatus::UnsupportedMediaType:
            return "Unsupported Media Type";
        case HttpStatus::ServiceUnavailable:
            return "Service Unavailable";
    }
    // RFC 9112 lets a status line carry no reason phrase. The code alone says
    // what happened.
    return "";
}

// Why a body did not arrive whole. A client that went quiet is answered
// differently from one whose socket broke.
enum class BodyResult : std::uint8_t {
    Complete,
    TimedOut,
    Failed
};

// One request being served, handed to a handler on the host's own task.
// Valid only during that call, and never to be referenced past it.
class HttpExchange {
public:
    virtual ~HttpExchange() = default;

    // What the client says it is sending, before any of it is read
    virtual std::size_t body_length() const = 0;
    // Fills `body` completely, retrying a quiet socket `retries` times before
    // reporting a timeout.
    [[nodiscard]] virtual BodyResult read_body(std::span<char> body, int retries) = 0;
    // Copies the header's value, null-terminated, into `value`. Returns false when
    // the header is absent or does not fit, because a truncated value is not
    // what the client sent.
    [[nodiscard]] virtual bool header(const char* name, std::span<char> value) const = 0;

    // A response that sets none is served as 200.
    virtual void set_status(HttpStatus status) = 0;
    virtual void set_content_type(const char* media_type) = 0;
    virtual void set_content_type(const char* media_type, const char* charset) = 0;
    // `value` is queued, not copied, and must outlive the response.
    virtual void set_header(const char* name, const char* value) = 0;
    // Sends the response. The exchange answers nothing further.
    virtual void send(std::span<const char> body) = 0;

    void send_empty() {
        send({});
    }

    // For a body that carries its own terminator
    void send_text(const char* text) {
        const std::string_view body{text};
        send({body.data(), body.size()});
    }
};
