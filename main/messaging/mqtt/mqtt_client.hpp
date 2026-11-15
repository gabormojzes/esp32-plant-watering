#pragma once

#include <mqtt_client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "messaging/mqtt/mqtt_info.hpp"
#include "messaging/payload.hpp"
#include "shared/constants.hpp"

// Wraps esp-mqtt and moves opaque payloads by topic. Subscriptions are
// registered before start() and replayed on every connect.
class MqttClient {
public:
    // The strings are PEM text, kept by pointer by esp-mqtt and required to outlive
    // the client. Empty ones count as unset.
    struct Tls {
        // What the broker's certificate must chain to. When empty, the bundle
        // attach_ca_bundle loads is checked instead.
        const char* ca_cert;
        esp_err_t (*attach_ca_bundle)(void* conf);
        // The device's own certificate and key, for a broker that asks for one
        const char* client_cert;
        const char* client_key;
    };

    struct Config {
        const char* protocol;
        const char* broker;
        std::uint16_t port;
        const char* client_id;
        const char* user;
        const char* password;
        // Retained online while connected, and offline as the will
        const char* status_topic;
        // Set exactly when protocol is mqtts. esp-mqtt refuses TLS settings on
        // any other scheme.
        const Tls* tls;
    };

    // Longest supported topic, terminator included, which bounds inbound topics
    // and the topic allowance reserved in the packet buffer.
    static constexpr std::size_t MAX_TOPIC_SIZE{64};
    // Longest broker URI, protocol://broker:port, terminator included. mqtts is
    // the longest scheme esp-mqtt takes, and a DNS name runs to 253 characters.
    static constexpr std::size_t URI_BUFFER_SIZE{
        std::string_view{"mqtts://"}.size() + 253 + std::string_view{":65535"}.size() + 1
    };

    // `retained` is true for a message the broker kept and hands over on
    // subscribing, and false for one published while subscribed.
    using MessageHandler = void (*)(std::span<const char> payload, bool retained, void* context);

    explicit MqttClient(const Config& config);
    // Tears the esp-mqtt client down, giving back its task and socket.
    ~MqttClient();
    // A copy would tear the client down twice.
    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    // Starts the esp-mqtt client, which then connects and reconnects on its own
    // task.
    void start();
    bool is_connected() const;
    MqttInfo info() const;
    // Registers a handler for a topic. Call only before start(), because the
    // subscription table is unguarded once the event task runs. The handler runs
    // on the esp-mqtt event task.
    void subscribe(const char* topic, MessageHandler handler, void* context);
    // Publishes at QoS 0 on the calling task. It first waits for esp-mqtt's lock,
    // which the client task holds through each connect attempt and its own writes,
    // then blocks up to NETWORK_TIMEOUT_MS to send.
    [[nodiscard]] bool publish(const char* topic, std::span<const char> payload);

private:
    struct Subscription {
        const char* topic{nullptr};
        MessageHandler handler{nullptr};
        void* context{nullptr};
        // Id of this session's SUBSCRIBE. 0 means none, since MQTT never numbers a
        // packet 0.
        int msg_id{0};
        // Set when the last SUBSCRIBE got no id or a refusing SUBACK
        bool missing{false};
    };

    // Socket timeout, which also bounds the TCP connect and TLS handshake together
    static constexpr int NETWORK_TIMEOUT_MS{10 * constants::MS_PER_S};
    // esp-mqtt in/out buffer. Holds one full packet: fixed header, topic and
    // payload.
    static constexpr int BUFFER_SIZE{2432};
    // 16 bytes cover the MQTT fixed header and length fields.
    static_assert(
        Payload::BUFFER_SIZE + MAX_TOPIC_SIZE + 16 <= BUFFER_SIZE,
        "a full packet must fit the esp-mqtt buffer"
    );
    // Sized with headroom, since a slot costs only a few bytes
    static constexpr std::size_t MAX_SUBSCRIPTIONS{4};

    static void event_handler(void* arg, esp_event_base_t base, std::int32_t id, void* data);
    // Destroys the esp-mqtt client. Safe when none exists.
    void release();
    [[nodiscard]] bool bring_up();
    [[nodiscard]] bool init_client();
    void on_connected();
    void on_subscribed(esp_mqtt_event_handle_t event);
    void report_subscriptions() const;
    void on_data(esp_mqtt_event_handle_t event);
    [[nodiscard]] bool build_uri(std::array<char, URI_BUFFER_SIZE>& uri) const;
    esp_mqtt_client_config_t make_config(const char* uri) const;

    Config config_{};
    esp_mqtt_client_handle_t handle_{nullptr};
    // Filled before start(). After start() only the esp-mqtt event task touches
    // it.
    std::array<Subscription, MAX_SUBSCRIPTIONS> subscriptions_{};
    std::size_t subscription_count_{0};
    // Written by the esp-mqtt event task, read by is_connected() and info() callers
    std::atomic<bool> connected_{false};
    // Connections made since boot. Written by the esp-mqtt event task, read by
    // info() callers.
    std::atomic<std::uint32_t> connections_{0};
    bool started_{false};
};
