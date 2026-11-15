// SPDX-FileCopyrightText: 2016 Tuan PM
// SPDX-License-Identifier: Apache-2.0
//
// Changed from the original: cut down to what the host tests use, with host
// behavior added.

#pragma once

// Host stub for esp-mqtt, with only the symbols the firmware uses and no broker
// behind them. A client keeps the handler registered with it, and a test hands
// that handler the events the esp-mqtt task would.

#include <esp_err.h>
#include <esp_event.h>

#include <cstddef>
#include <cstdint>

// As ESP-IDF 5.5.6 declares them in mqtt_client.h, without the MQTT 5 members.
// esp_mqtt_client_config_t leaves out the two network fields spelled in
// esp_transport.h types.
typedef struct esp_mqtt_client* esp_mqtt_client_handle_t;

typedef enum esp_mqtt_event_id_t {
    MQTT_EVENT_ANY = -1,
    MQTT_EVENT_ERROR = 0,
    MQTT_EVENT_CONNECTED,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_SUBSCRIBED,
    MQTT_EVENT_UNSUBSCRIBED,
    MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_DATA,
    MQTT_EVENT_BEFORE_CONNECT,
    MQTT_EVENT_DELETED,
    MQTT_USER_EVENT,
} esp_mqtt_event_id_t;

typedef enum esp_mqtt_connect_return_code_t {
    MQTT_CONNECTION_ACCEPTED = 0,
    MQTT_CONNECTION_REFUSE_PROTOCOL,
    MQTT_CONNECTION_REFUSE_ID_REJECTED,
    MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE,
    MQTT_CONNECTION_REFUSE_BAD_USERNAME,
    MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED
} esp_mqtt_connect_return_code_t;

typedef enum esp_mqtt_error_type_t {
    MQTT_ERROR_TYPE_NONE = 0,
    MQTT_ERROR_TYPE_TCP_TRANSPORT,
    MQTT_ERROR_TYPE_CONNECTION_REFUSED,
    MQTT_ERROR_TYPE_SUBSCRIBE_FAILED
} esp_mqtt_error_type_t;

typedef enum esp_mqtt_transport_t {
    MQTT_TRANSPORT_UNKNOWN = 0x0,
    MQTT_TRANSPORT_OVER_TCP,
    MQTT_TRANSPORT_OVER_SSL,
    MQTT_TRANSPORT_OVER_WS,
    MQTT_TRANSPORT_OVER_WSS
} esp_mqtt_transport_t;

typedef enum esp_mqtt_protocol_ver_t {
    MQTT_PROTOCOL_UNDEFINED = 0,
    MQTT_PROTOCOL_V_3_1,
    MQTT_PROTOCOL_V_3_1_1,
    MQTT_PROTOCOL_V_5,
} esp_mqtt_protocol_ver_t;

typedef struct esp_mqtt_error_codes {
    esp_err_t esp_tls_last_esp_err;
    int esp_tls_stack_err;
    int esp_tls_cert_verify_flags;
    esp_mqtt_error_type_t error_type;
    esp_mqtt_connect_return_code_t connect_return_code;
    int esp_transport_sock_errno;
} esp_mqtt_error_codes_t;

typedef struct esp_mqtt_event_t {
    esp_mqtt_event_id_t event_id;
    esp_mqtt_client_handle_t client;
    char* data;
    int data_len;
    int total_data_len;
    int current_data_offset;
    char* topic;
    int topic_len;
    int msg_id;
    int session_present;
    esp_mqtt_error_codes_t* error_handle;
    bool retain;
    int qos;
    bool dup;
    esp_mqtt_protocol_ver_t protocol_ver;
} esp_mqtt_event_t;

typedef esp_mqtt_event_t* esp_mqtt_event_handle_t;

typedef struct esp_mqtt_client_config_t {
    struct broker_t {
        struct address_t {
            const char* uri;
            const char* hostname;
            esp_mqtt_transport_t transport;
            const char* path;
            std::uint32_t port;
        } address;
        struct verification_t {
            bool use_global_ca_store;
            esp_err_t (*crt_bundle_attach)(void* conf);
            const char* certificate;
            std::size_t certificate_len;
            const struct psk_key_hint* psk_hint_key;
            bool skip_cert_common_name_check;
            const char** alpn_protos;
            const char* common_name;
        } verification;
    } broker;
    struct credentials_t {
        const char* username;
        const char* client_id;
        bool set_null_client_id;
        struct authentication_t {
            const char* password;
            const char* certificate;
            std::size_t certificate_len;
            const char* key;
            std::size_t key_len;
            const char* key_password;
            int key_password_len;
            bool use_secure_element;
            void* ds_data;
            bool use_ecdsa_peripheral;
            std::uint8_t ecdsa_key_efuse_blk;
        } authentication;
    } credentials;
    struct session_t {
        struct last_will_t {
            const char* topic;
            const char* msg;
            int msg_len;
            int qos;
            int retain;
        } last_will;
        bool disable_clean_session;
        int keepalive;
        bool disable_keepalive;
        esp_mqtt_protocol_ver_t protocol_ver;
        int message_retransmit_timeout;
    } session;
    struct network_t {
        int reconnect_timeout_ms;
        int timeout_ms;
        int refresh_connection_after_ms;
        bool disable_auto_reconnect;
        struct ifreq* if_name;
    } network;
    struct task_t {
        int priority;
        int stack_size;
    } task;
    struct buffer_t {
        int size;
        int out_size;
    } buffer;
    struct outbox_config_t {
        std::uint64_t limit;
    } outbox;
} esp_mqtt_client_config_t;

#define esp_mqtt_client_subscribe esp_mqtt_client_subscribe_single

// The client esp-mqtt leaves opaque
struct esp_mqtt_client {
    esp_event_handler_t event_handler{nullptr};
    void* event_handler_arg{nullptr};
};

// The one client alive, or null while there is none
inline esp_mqtt_client_handle_t mqtt_stub_client{nullptr};

inline esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* /*config*/) {
    mqtt_stub_client = new esp_mqtt_client{};
    return mqtt_stub_client;
}

inline esp_err_t esp_mqtt_client_register_event(
    esp_mqtt_client_handle_t client,
    esp_mqtt_event_id_t /*event*/,
    esp_event_handler_t event_handler,
    void* event_handler_arg
) {
    client->event_handler = event_handler;
    client->event_handler_arg = event_handler_arg;
    return ESP_OK;
}

inline esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t /*client*/) {
    return ESP_OK;
}

inline int esp_mqtt_client_subscribe_single(
    esp_mqtt_client_handle_t /*client*/, const char* /*topic*/, int /*qos*/
) {
    return 1;
}

inline int esp_mqtt_client_publish(
    esp_mqtt_client_handle_t /*client*/,
    const char* /*topic*/,
    const char* /*data*/,
    int /*len*/,
    int /*qos*/,
    int /*retain*/
) {
    return 0;
}

inline esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client) {
    delete client;
    mqtt_stub_client = nullptr;
    return ESP_OK;
}

// Hands an event to the registered handler, as the esp-mqtt task does.
inline void mqtt_stub_emit(esp_mqtt_event_t& event) {
    mqtt_stub_client->event_handler(
        mqtt_stub_client->event_handler_arg, nullptr, event.event_id, &event
    );
}
