#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "messaging/mqtt/mqtt_client.hpp"
#include "messaging/payload.hpp"

// Latest-wins mailbox in front of one topic. send() never blocks, a dedicated
// task performs the blocking publish, and a newer frame replaces an unsent one.
class MqttPublisher {
public:
    MqttPublisher(MqttClient& client, const char* topic);
    // The publisher task keeps a pointer to this object, never to a copy.
    MqttPublisher(const MqttPublisher&) = delete;
    MqttPublisher& operator=(const MqttPublisher&) = delete;
    // Creates the mailbox and its task. On failure send() does nothing.
    void start();
    void send(const Payload& payload);

private:
    static void publisher_task(void* arg);
    void publish(const Payload& payload);

    MqttClient& client_;
    const char* topic_{nullptr};
    // A member because a Payload does not fit the publisher task's stack
    // alongside the publish path.
    Payload payload_{};
    QueueHandle_t mailbox_{nullptr};
};
