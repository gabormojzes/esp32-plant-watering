#pragma once

#include <esp_http_server.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "web/access_gate.hpp"
#include "web/web_host.hpp"

// The HTTP server, on esp_http_server. Routes are registered before start(),
// and their handlers run on the server's task behind each route's AccessSet.
class WebServer : public WebHost {
public:
    struct Config {
        std::uint16_t port;
    };

    WebServer(const Config& config, const AccessGate& access_gate);
    // Stops the server and frees its task and sockets.
    ~WebServer() override;
    // A copy would stop the server twice.
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    // Starts the server, which then accepts connections on its own task.
    void start() override;
    // Gives up on a client that has held the server past its budget.
    void tick(std::int64_t now_us) override;
    void route(
        const char* uri, HttpMethod method, AccessSet access, Handler handler, void* context
    ) override;

private:
    // Sized for every route the firmware registers. A new route needs it raised, or
    // the route is dropped.
    static constexpr std::size_t MAX_ROUTES{8};
    // A held socket costs RAM whether or not it is being served
    static constexpr std::uint16_t MAX_CLIENTS{4};
    // Marks a slot with nothing to watch. The clock never reads negative.
    static constexpr std::int64_t UNWATCHED{-1};
    static constexpr int NO_SOCKET{-1};

    struct Client {
        std::atomic<int> sockfd{NO_SOCKET};
        // When the budget started. A client given up on is reset to UNWATCHED, which
        // keeps it from being given up on twice.
        std::atomic<std::int64_t> budget_start_us{UNWATCHED};
    };

    // A registered route, through which gate() reaches the handler and context
    struct Route {
        httpd_uri_t uri{};
        AccessSet access;
        Handler handler{nullptr};
        void* context{nullptr};
    };

    static esp_err_t on_open(httpd_handle_t httpd, int sockfd);
    static void on_close(httpd_handle_t httpd, int sockfd);
    // The context is this server, which httpd would otherwise free().
    static void keep_global_context(void* context);
    // Runs for every request, checking what the route demands before handing over.
    static esp_err_t gate(httpd_req_t* request);

    void claim(int sockfd, std::int64_t now_us);
    void refresh(int sockfd, std::int64_t now_us);
    void release(int sockfd);

    // True once the socket and its task are claimed.
    bool is_running() const;
    // Starts httpd. Leaves `httpd_` null on failure.
    void bring_up();

    Config config_{};
    const AccessGate& access_gate_;
    httpd_handle_t httpd_{nullptr};
    // Filled before start(), which registers them
    std::array<Route, MAX_ROUTES> routes_{};
    std::size_t route_count_{0};
    std::array<Client, MAX_CLIENTS> clients_{};
};
