#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <span>

#include "messaging/mqtt/mqtt_client.hpp"
#include "messaging/payload.hpp"

// FIFO of inbound payloads from one topic, filled on the esp-mqtt event task and
// drained by the consumer at its own pace. Retained messages are left out.
class MqttInbox {
public:
    MqttInbox(MqttClient& client, const char* topic);
    // The subscription keeps a pointer to this object, never to a copy.
    MqttInbox(const MqttInbox&) = delete;
    MqttInbox& operator=(const MqttInbox&) = delete;
    // Creates the queue and subscribes. On failure the topic is never
    // subscribed.
    void start();
    // Takes the oldest pending payload. Returns false when there is none.
    [[nodiscard]] bool take(Payload& payload);

private:
    static void on_message(std::span<const char> payload, bool retained, void* context);
    void enqueue(std::span<const char> payload, bool retained);

    MqttClient& client_;
    const char* topic_{nullptr};
    QueueHandle_t queue_{nullptr};
    // A member because a Payload does not fit the event task's stack. Only that
    // task uses it, one message at a time.
    Payload payload_{};
};
