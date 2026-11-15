#include "messaging/mqtt/mqtt_publisher.hpp"

#include <esp_log.h>
#include <freertos/task.h>

#include <cstdint>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"

namespace {

constexpr char TAG[]{"MQTT_PUBLISHER"};

constexpr std::uint32_t TASK_STACK{4096};
// The lowest level above idle, since publishing is never urgent and the task is
// almost always parked on the mailbox or the socket.
constexpr UBaseType_t TASK_PRIORITY{1};
// One slot, because xQueueOverwrite, which send() relies on, requires a queue
// of length 1.
constexpr UBaseType_t QUEUE_LENGTH{1};

}  // namespace

MqttPublisher::MqttPublisher(MqttClient& client, const char* topic)
    : client_{client}, topic_{topic} {}

void MqttPublisher::start() {
    if (mailbox_ != nullptr) {
        return;
    }
    mailbox_ = xQueueCreate(QUEUE_LENGTH, sizeof(Payload));
    if (mailbox_ == nullptr) {
        faults::raise(fault_definitions::MQTT_TELEMETRY_MAILBOX_UNAVAILABLE);
        ESP_LOGE(TAG, "Mailbox create failed, publishing disabled for topic %s", topic_);
        return;
    }
    const bool task_started{
        xTaskCreate(
            &MqttPublisher::publisher_task, "mqtt_pub", TASK_STACK, this, TASK_PRIORITY, nullptr
        ) == pdPASS
    };
    if (!task_started) {
        faults::raise(fault_definitions::MQTT_PUBLISHER_TASK_START_FAILED);
        ESP_LOGE(TAG, "Task create failed, publishing disabled for topic %s", topic_);
        vQueueDelete(mailbox_);
        mailbox_ = nullptr;
    }
}

void MqttPublisher::send(const Payload& payload) {
    if (mailbox_ == nullptr) {
        return;
    }
    // The result needs no checking. An overwrite always succeeds.
    xQueueOverwrite(mailbox_, &payload);
}

void MqttPublisher::publisher_task(void* arg) {
    auto* self{static_cast<MqttPublisher*>(arg)};
    // Deliberately not on the task watchdog, because parking on the mailbox or a
    // stalled socket is normal here.
    while (true) {
        if (xQueueReceive(self->mailbox_, &self->payload_, portMAX_DELAY) == pdTRUE) {
            self->publish(self->payload_);
        }
    }
}

void MqttPublisher::publish(const Payload& payload) {
    if (!client_.publish(topic_, {payload.data.data(), static_cast<std::size_t>(payload.length)})) {
        ESP_LOGW(TAG, "Publish failed for topic %s", topic_);
        return;
    }
    ESP_LOGD(TAG, "Sent to topic %s: %.*s", topic_, payload.length, payload.data.data());
}
