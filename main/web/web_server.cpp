#include "web/web_server.hpp"

#include <esp_log.h>
#include <esp_timer.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"
#include "network/ip_stack.hpp"
#include "shared/charsets.hpp"
#include "shared/constants.hpp"
#include "shared/media_types.hpp"

namespace {

constexpr char TAG[]{"WEB_SERVER"};

// The lowest level above idle, since serving is never urgent and sustained
// requests must not come at anything else's expense.
constexpr unsigned TASK_PRIORITY{1};

// Serving is single-task, which makes this also bound how long a client that
// goes quiet parks it.
constexpr int RECV_TIMEOUT_S{2};

// What a client may hold the server for, counted from the socket being
// accepted and again from each request served on it. Serving is
// single-task, and one that neither finishes nor disconnects keeps it all.
constexpr std::int64_t REQUEST_BUDGET_US{30 * constants::US_PER_S};

httpd_method_t method_of(HttpMethod method) {
    switch (method) {
        case HttpMethod::Get:
            return HTTP_GET;
        case HttpMethod::Post:
            return HTTP_POST;
        case HttpMethod::Options:
            return HTTP_OPTIONS;
    }
    // No route claims a method outside the enum, and httpd has no "no method"
    // value. GET, the read-only method, is the safe stand-in.
    return HTTP_GET;
}

// HttpExchange over one httpd request. Lives on the server task's stack for one
// request and owns nothing.
class HttpdExchange final : public HttpExchange {
public:
    explicit HttpdExchange(httpd_req_t* request) : request_{request} {}

    std::size_t body_length() const override {
        return request_->content_len;
    }

    [[nodiscard]] BodyResult read_body(std::span<char> body, int retries) override {
        std::size_t read{0};
        int retried{0};
        while (read < body.size()) {
            const int received{httpd_req_recv(request_, body.data() + read, body.size() - read)};
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                if (retried++ < retries) {
                    continue;
                }
                return BodyResult::TimedOut;
            }
            if (received <= 0) {
                return BodyResult::Failed;
            }
            read += static_cast<std::size_t>(received);
        }
        return BodyResult::Complete;
    }

    [[nodiscard]] bool header(const char* name, std::span<char> value) const override {
        // Anything but ESP_OK means the header is absent or truncated, leaving
        // `value` unusable.
        return httpd_req_get_hdr_value_str(request_, name, value.data(), value.size()) == ESP_OK;
    }

    void set_status(HttpStatus status) override {
        // httpd takes the code and reason as one line.
        std::snprintf(
            status_line_.data(),
            status_line_.size(),
            "%u %s",
            static_cast<unsigned>(status),
            reason_phrase(status)
        );
        // The result needs no checking. httpd refuses only a null request or status,
        // or a status holding a CR or LF, and none applies.
        httpd_resp_set_status(request_, status_line_.data());
    }

    void set_content_type(const char* media_type) override {
        // The result needs no checking. httpd refuses only a null request or type, or
        // a type holding a CR or LF, and no caller's type holds one.
        httpd_resp_set_type(request_, media_type);
    }

    void set_content_type(const char* media_type, const char* charset) override {
        const int length{std::snprintf(
            content_type_.data(), content_type_.size(), "%s; charset=%s", media_type, charset
        )};
        // A cut type would name something else, so one that does not fit goes out
        // bare.
        if (length < 0 || static_cast<std::size_t>(length) >= content_type_.size()) {
            set_content_type(media_type);
            return;
        }
        set_content_type(content_type_.data());
    }

    void set_header(const char* name, const char* value) override {
        // The result needs no checking. httpd refuses only a null name or value, one
        // holding a CR or LF, or a header past max_resp_headers, and no response sets
        // one of those.
        httpd_resp_set_hdr(request_, name, value);
    }

    void send(std::span<const char> body) override {
        result_ = httpd_resp_send(request_, body.data(), static_cast<ssize_t>(body.size()));
    }

    // The result returned to httpd. Only a failed send is one httpd acts on.
    esp_err_t result() const {
        return result_;
    }

private:
    // The code, a space, the reason and a terminator. A longer reason is cut
    // short, which still reads as a status line.
    static constexpr std::size_t STATUS_LINE_BUFFER_SIZE{32};
    // The media type, the charset parameter and a terminator
    static constexpr std::size_t CONTENT_TYPE_BUFFER_SIZE{32};
    static_assert(
        CONTENT_TYPE_BUFFER_SIZE >= std::string_view{media_types::TEXT_HTML}.size() +
                                        std::string_view{"; charset="}.size() +
                                        std::string_view{charsets::UTF8}.size() + 1,
        "CONTENT_TYPE_BUFFER_SIZE cannot hold the pages' content type"
    );

    httpd_req_t* request_{nullptr};
    esp_err_t result_{ESP_OK};
    // httpd keeps the pointer until the send, which comes before the exchange
    // goes.
    std::array<char, STATUS_LINE_BUFFER_SIZE> status_line_{};
    std::array<char, CONTENT_TYPE_BUFFER_SIZE> content_type_{};
};

}  // namespace

WebServer::WebServer(const Config& config, const AccessGate& access_gate)
    : config_{config}, access_gate_{access_gate} {}

WebServer::~WebServer() {
    if (is_running()) {
        // The result needs no checking. A stop fails only when the shutdown message
        // cannot reach the server task, and a destructor has no way to recover.
        httpd_stop(httpd_);
    }
}

void WebServer::route(
    const char* uri, HttpMethod method, AccessSet access, Handler handler, void* context
) {
    if (route_count_ >= routes_.size()) {
        ESP_LOGE(TAG, "Route table full, %s dropped", uri);
        return;
    }
    Route& entry{routes_[route_count_++]};
    entry.access = access;
    entry.handler = handler;
    entry.context = context;
    // httpd copies the descriptor but keeps the pointer, which must lead back
    // to storage that outlives the registration.
    entry.uri = {
        .uri = uri,
        .method = method_of(method),
        .handler = &WebServer::gate,
        .user_ctx = &entry,
    };
}

esp_err_t WebServer::gate(httpd_req_t* request) {
    const auto* route{static_cast<const Route*>(request->user_ctx)};
    auto* self{static_cast<WebServer*>(httpd_get_global_user_ctx(request->handle))};
    HttpdExchange exchange{request};

    // The budget is what one request may take, not what a connection may live
    // for.
    self->refresh(httpd_req_to_sockfd(request), esp_timer_get_time());

    // Before any answer, refusals included, which makes a browser take each as the
    // type it declares and never guess another.
    exchange.set_header("X-Content-Type-Options", "nosniff");

    const AccessGate& access_gate{self->access_gate_};
    if (!access_gate.granted(route->access, exchange)) {
        access_gate.refuse(route->access, exchange);
        return exchange.result();
    }

    route->handler(exchange, route->context);
    return exchange.result();
}

void WebServer::start() {
    if (is_running()) {
        return;
    }

    bring_up();
    if (!is_running()) {
        faults::raise(fault_definitions::WEB_SERVER_START_FAILED);
        return;
    }

    for (const Route& route : std::span{routes_}.first(route_count_)) {
        const esp_err_t err{httpd_register_uri_handler(httpd_, &route.uri)};
        if (err != ESP_OK) {
            faults::raise(fault_definitions::WEB_ROUTE_REGISTER_FAILED);
            ESP_LOGE(TAG, "Route %s failed: %s", route.uri.uri, esp_err_to_name(err));
        }
    }
    ESP_LOGI(TAG, "Listening on port %u", config_.port);
}

void WebServer::bring_up() {
    if (!ip_stack::up()) {
        ESP_LOGE(TAG, "No network interface, serving nothing");
        return;
    }

    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    httpd_config.server_port = config_.port;
    httpd_config.task_priority = TASK_PRIORITY;
    httpd_config.max_uri_handlers = MAX_ROUTES;
    httpd_config.max_open_sockets = MAX_CLIENTS;
    httpd_config.recv_wait_timeout = RECV_TIMEOUT_S;
    // A browser tab left open holds its socket. Without this, a few of them
    // would take every slot and lock out the next client.
    httpd_config.lru_purge_enable = true;
    httpd_config.open_fn = &WebServer::on_open;
    httpd_config.close_fn = &WebServer::on_close;
    httpd_config.global_user_ctx = this;
    httpd_config.global_user_ctx_free_fn = &WebServer::keep_global_context;

    const esp_err_t err{httpd_start(&httpd_, &httpd_config)};
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Start failed: %s, serving nothing", esp_err_to_name(err));
        // is_running() reads the outcome from `httpd_`.
        httpd_ = nullptr;
    }
}

esp_err_t WebServer::on_open(httpd_handle_t httpd, int sockfd) {
    auto* self{static_cast<WebServer*>(httpd_get_global_user_ctx(httpd))};
    self->claim(sockfd, esp_timer_get_time());
    return ESP_OK;
}

void WebServer::on_close(httpd_handle_t httpd, int sockfd) {
    auto* self{static_cast<WebServer*>(httpd_get_global_user_ctx(httpd))};
    self->release(sockfd);
    // httpd leaves the socket to whoever takes this callback.
    close(sockfd);
}

void WebServer::keep_global_context(void* /*context*/) {}

void WebServer::claim(int sockfd, std::int64_t now_us) {
    for (Client& client : clients_) {
        if (client.sockfd.load() == NO_SOCKET) {
            client.sockfd.store(sockfd);
            client.budget_start_us.store(now_us);
            return;
        }
    }
}

void WebServer::refresh(int sockfd, std::int64_t now_us) {
    // NO_SOCKET marks an empty slot, never a client.
    if (sockfd == NO_SOCKET) {
        return;
    }
    for (Client& client : clients_) {
        if (client.sockfd.load() == sockfd) {
            client.budget_start_us.store(now_us);
            return;
        }
    }
}

void WebServer::release(int sockfd) {
    for (Client& client : clients_) {
        if (client.sockfd.load() == sockfd) {
            // The budget is cleared before the socket, because tick() reads the
            // budget first and must not judge a slot being emptied.
            client.budget_start_us.store(UNWATCHED);
            client.sockfd.store(NO_SOCKET);
            return;
        }
    }
}

void WebServer::tick(std::int64_t now_us) {
    for (Client& client : clients_) {
        std::int64_t budget_start_us{client.budget_start_us.load()};
        if (budget_start_us == UNWATCHED || (now_us - budget_start_us) < REQUEST_BUDGET_US) {
            continue;
        }
        // Only while the budget still reads as it did, which lets a request that
        // landed in the meantime keep its socket.
        if (!client.budget_start_us.compare_exchange_strong(budget_start_us, UNWATCHED)) {
            continue;
        }
        const int sockfd{client.sockfd.load()};
        if (sockfd != NO_SOCKET) {
            // Wakes the server task from its blocking read. That task closes the
            // socket.
            shutdown(sockfd, SHUT_RDWR);
        }
    }
}

bool WebServer::is_running() const {
    return httpd_ != nullptr;
}
