#include "messaging/mqtt/mqtt_client.hpp"

#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <span>
#include <string_view>
#include <utility>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"
#include "network/ip_stack.hpp"
#include "shared/string_utils.hpp"

namespace {

constexpr char TAG[]{"MQTT_CLIENT"};

// Ping cadence, which also bounds how fast a silently dead link is noticed
constexpr int KEEPALIVE_S{15};
// The lowest level above idle, since connecting, reconnecting and carrying
// payloads are never urgent. esp-mqtt picks a far higher one when left to itself.
constexpr int TASK_PRIORITY{1};

constexpr std::string_view STATUS_ONLINE{"online"};
constexpr std::string_view STATUS_OFFLINE{"offline"};

}  // namespace

MqttClient::MqttClient(const Config& config) : config_{config} {}

MqttClient::~MqttClient() {
    release();
}

void MqttClient::release() {
    if (handle_ == nullptr) {
        return;
    }
    // Stops the event task. No event arrives after this. The result needs no
    // checking. A destroy fails only for a null client.
    esp_mqtt_client_destroy(handle_);
    handle_ = nullptr;
    started_ = false;
    connected_ = false;
}

void MqttClient::start() {
    if (started_) {
        return;
    }
    started_ = bring_up();
    if (!started_) {
        faults::raise(fault_definitions::MQTT_CLIENT_START_FAILED);
    }
}

bool MqttClient::bring_up() {
    if (!ip_stack::up()) {
        ESP_LOGE(TAG, "No network interface, staying offline");
        return false;
    }
    if (!init_client()) {
        return false;
    }
    ESP_LOGD(TAG, "Starting client");
    const esp_err_t err{esp_mqtt_client_start(handle_)};
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Client start failed: %s", esp_err_to_name(err));
        // Release the client created above, leaving nothing behind after a failed
        // start.
        release();
        return false;
    }
    return true;
}

void MqttClient::subscribe(const char* topic, MessageHandler handler, void* context) {
    if (started_) {
        ESP_LOGW(TAG, "Subscribe after start ignored for topic %s", topic);
        return;
    }
    if (subscription_count_ >= subscriptions_.size()) {
        ESP_LOGW(TAG, "Subscription table full, topic %s ignored", topic);
        return;
    }
    subscriptions_[subscription_count_++] = {
        .topic = topic, .handler = handler, .context = context
    };
}

bool MqttClient::init_client() {
    // esp-mqtt copies the URI, which makes a stack-local buffer safe
    std::array<char, URI_BUFFER_SIZE> uri{};
    if (!build_uri(uri)) {
        return false;
    }
    const esp_mqtt_client_config_t client_config{make_config(uri.data())};
    handle_ = esp_mqtt_client_init(&client_config);
    if (handle_ == nullptr) {
        ESP_LOGE(TAG, "Client init failed");
        return false;
    }
    const esp_err_t err{
        esp_mqtt_client_register_event(handle_, MQTT_EVENT_ANY, &MqttClient::event_handler, this)
    };
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Event registration failed: %s", esp_err_to_name(err));
        // Release the client created above. Deaf to its own events, it would never
        // report a connection.
        release();
        return false;
    }
    return true;
}

bool MqttClient::build_uri(std::array<char, URI_BUFFER_SIZE>& uri) const {
    const int uri_length{std::snprintf(
        uri.data(), uri.size(), "%s://%s:%u", config_.protocol, config_.broker, config_.port
    )};
    if (uri_length < 0) {
        ESP_LOGE(TAG, "URI encoding failed for broker %s:%u", config_.broker, config_.port);
        return false;
    }
    if (std::cmp_greater_equal(uri_length, uri.size())) {
        // Refuse to connect with a truncated URI, which may resolve to an unintended
        // host.
        ESP_LOGE(TAG, "URI truncated for broker %s:%u", config_.broker, config_.port);
        return false;
    }
    return true;
}

esp_mqtt_client_config_t MqttClient::make_config(const char* uri) const {
    esp_mqtt_client_config_t client_config{};
    client_config.broker.address.uri = uri;
    client_config.credentials.client_id = config_.client_id;
    // Credentials stay unset when empty, and no auth is attempted.
    if (!string_utils::is_empty(config_.user)) {
        client_config.credentials.username = config_.user;
    }
    if (!string_utils::is_empty(config_.password)) {
        client_config.credentials.authentication.password = config_.password;
    }
    if (config_.tls != nullptr) {
        const Tls& tls{*config_.tls};
        if (string_utils::is_empty(tls.ca_cert)) {
            client_config.broker.verification.crt_bundle_attach = tls.attach_ca_bundle;
        } else {
            client_config.broker.verification.certificate = tls.ca_cert;
        }
        if (!string_utils::is_empty(tls.client_cert)) {
            client_config.credentials.authentication.certificate = tls.client_cert;
        }
        if (!string_utils::is_empty(tls.client_key)) {
            client_config.credentials.authentication.key = tls.client_key;
        }
    }
    // esp-mqtt copies the topic and the message.
    client_config.session.last_will.topic = config_.status_topic;
    client_config.session.last_will.msg = STATUS_OFFLINE.data();
    client_config.session.last_will.msg_len = static_cast<int>(STATUS_OFFLINE.size());
    client_config.session.last_will.qos = 0;
    client_config.session.last_will.retain = 1;
    client_config.session.keepalive = KEEPALIVE_S;
    client_config.network.timeout_ms = NETWORK_TIMEOUT_MS;
    client_config.buffer.size = BUFFER_SIZE;
    client_config.task.priority = TASK_PRIORITY;
    return client_config;
}

void MqttClient::event_handler(void* arg, esp_event_base_t /*base*/, std::int32_t id, void* data) {
    auto* self{static_cast<MqttClient*>(arg)};
    auto* event{static_cast<esp_mqtt_event_handle_t>(data)};

    switch (static_cast<esp_mqtt_event_id_t>(id)) {
        case MQTT_EVENT_CONNECTED:
            self->connected_.store(true);
            self->connections_.fetch_add(1);
            faults::clear(fault_definitions::MQTT_CONNECTION_REFUSED);
            ESP_LOGD(TAG, "Connected");
            self->on_connected();
            break;

        case MQTT_EVENT_DISCONNECTED:
            self->connected_.store(false);
            ESP_LOGD(TAG, "Disconnected");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            self->on_subscribed(event);
            break;

        case MQTT_EVENT_DATA:
            self->on_data(event);
            break;

        case MQTT_EVENT_ERROR:
            // The broker refused the CONNECT. The fault holds until a later
            // CONNECT is accepted.
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                faults::raise(fault_definitions::MQTT_CONNECTION_REFUSED);
            }
            // Expected while the broker is unreachable, since esp-mqtt raises this
            // on every reconnect attempt.
            ESP_LOGW(
                TAG,
                "Transport error: type %d, esp-tls %d, errno %d, refused %d",
                event->error_handle->error_type,
                event->error_handle->esp_tls_last_esp_err,
                event->error_handle->esp_transport_sock_errno,
                event->error_handle->connect_return_code
            );
            if (event->error_handle->esp_tls_stack_err != 0 ||
                event->error_handle->esp_tls_cert_verify_flags != 0) {
                ESP_LOGW(
                    TAG,
                    "TLS error: mbedtls -0x%04x, cert flags 0x%x",
                    static_cast<unsigned>(event->error_handle->esp_tls_stack_err),
                    static_cast<unsigned>(event->error_handle->esp_tls_cert_verify_flags)
                );
            }
            break;

        default:
            ESP_LOGV(TAG, "Unhandled event: %" PRId32, id);
            break;
    }
}

void MqttClient::on_connected() {
    // On every connect, which also replaces the offline will of a session this
    // one took over.
    const int status_id{esp_mqtt_client_publish(
        handle_,
        config_.status_topic,
        STATUS_ONLINE.data(),
        static_cast<int>(STATUS_ONLINE.size()),
        /*qos=*/0,
        /*retain=*/1
    )};
    if (status_id < 0) {
        // A failed write drops the connection, and the reconnect publishes again.
        ESP_LOGW(TAG, "Status publish failed for topic %s", config_.status_topic);
    }

    // Subscribe again on every connect, because the broker forgets the session
    // between them.
    for (Subscription& subscription : std::span{subscriptions_}.first(subscription_count_)) {
        // QoS 0 end to end, matching publish(). Delivery is best-effort, and a
        // lost payload is never retried.
        subscription.msg_id = esp_mqtt_client_subscribe(handle_, subscription.topic, 0);
        // Kept even when negative, because no SUBACK carries a negative id and an
        // earlier session's answer must not count for this one.
        if (subscription.msg_id < 0) {
            // No id came back, and no SUBACK can match, which leaves the topic
            // missing for the rest of this session.
            subscription.missing = true;
            ESP_LOGW(TAG, "Subscribe failed for topic %s", subscription.topic);
        } else {
            ESP_LOGD(TAG, "Subscribed to topic %s", subscription.topic);
        }
    }
    report_subscriptions();
}

void MqttClient::on_subscribed(esp_mqtt_event_handle_t event) {
    // One topic per SUBSCRIBE, which makes the SUBACK's single code that topic's.
    for (Subscription& subscription : std::span{subscriptions_}.first(subscription_count_)) {
        if (subscription.msg_id == event->msg_id) {
            subscription.missing =
                event->error_handle->error_type == MQTT_ERROR_TYPE_SUBSCRIBE_FAILED;
            if (subscription.missing) {
                ESP_LOGW(TAG, "Subscribe refused for topic %s", subscription.topic);
            }
        }
    }
    report_subscriptions();
}

void MqttClient::report_subscriptions() const {
    // Raised while any topic lacks a subscription. One topic's grant never
    // clears another's.
    const std::span<const Subscription> held{std::span{subscriptions_}.first(subscription_count_)};
    if (std::ranges::any_of(held, &Subscription::missing)) {
        faults::raise(fault_definitions::MQTT_SUBSCRIPTION_MISSING);
    } else {
        faults::clear(fault_definitions::MQTT_SUBSCRIPTION_MISSING);
    }
}

void MqttClient::on_data(esp_mqtt_event_handle_t event) {
    // esp-mqtt leaves topic null on the later chunks of a fragmented packet.
    if (event->topic == nullptr) {
        return;
    }
    // Packets larger than the esp-mqtt buffer arrive fragmented. Drop the whole
    // packet rather than pass on part of it.
    if (event->data_len != event->total_data_len) {
        ESP_LOGW(
            TAG,
            "Fragmented packet dropped (%d of %d bytes)",
            event->data_len,
            event->total_data_len
        );
        return;
    }
    if (event->topic_len < 0 || std::cmp_greater_equal(event->topic_len, MAX_TOPIC_SIZE)) {
        ESP_LOGW(
            TAG,
            "Over-long topic dropped (%d of max %zu bytes)",
            event->topic_len,
            MAX_TOPIC_SIZE - 1
        );
        return;
    }
    // esp-mqtt does not null-terminate the topic
    const std::string_view topic{event->topic, static_cast<std::size_t>(event->topic_len)};
    for (const Subscription& subscription : std::span{subscriptions_}.first(subscription_count_)) {
        if (topic != subscription.topic) {
            continue;
        }
        ESP_LOGD(
            TAG,
            "Received on topic %.*s: %.*s",
            static_cast<int>(topic.size()),
            topic.data(),
            event->data_len,
            event->data
        );
        subscription.handler(
            {event->data, static_cast<std::size_t>(event->data_len)},
            event->retain,
            subscription.context
        );
        return;
    }
    ESP_LOGD(TAG, "No handler for topic %.*s", static_cast<int>(topic.size()), topic.data());
}

bool MqttClient::publish(const char* topic, std::span<const char> payload) {
    if (handle_ == nullptr) {
        return false;
    }
    // Not retained, which makes a late subscriber wait for the next frame rather
    // than read a stale one.
    const int msg_id{esp_mqtt_client_publish(
        handle_, topic, payload.data(), static_cast<int>(payload.size()), /*qos=*/0, /*retain=*/0
    )};
    return msg_id >= 0;
}

bool MqttClient::is_connected() const {
    return connected_.load();
}

MqttInfo MqttClient::info() const {
    const std::uint32_t connections{connections_.load()};
    return {
        .connected = connected_.load(),
        .reconnects = connections == 0 ? 0 : connections - 1,
    };
}
