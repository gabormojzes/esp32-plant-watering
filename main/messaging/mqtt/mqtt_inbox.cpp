#include "messaging/mqtt/mqtt_inbox.hpp"

#include <esp_log.h>

#include <algorithm>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"

namespace {

constexpr char TAG[]{"MQTT_INBOX"};

// A short queue absorbs a burst on a low-rate topic without dropping the
// earlier of two quick messages.
constexpr UBaseType_t QUEUE_LENGTH{2};

}  // namespace

MqttInbox::MqttInbox(MqttClient& client, const char* topic) : client_{client}, topic_{topic} {}

void MqttInbox::start() {
    if (queue_ != nullptr) {
        return;
    }
    queue_ = xQueueCreate(QUEUE_LENGTH, sizeof(Payload));
    if (queue_ == nullptr) {
        faults::raise(fault_definitions::MQTT_COMMAND_QUEUE_UNAVAILABLE);
        ESP_LOGE(TAG, "Queue create failed, topic %s disabled", topic_);
        return;
    }
    client_.subscribe(topic_, &MqttInbox::on_message, this);
}

void MqttInbox::on_message(std::span<const char> payload, bool retained, void* context) {
    static_cast<MqttInbox*>(context)->enqueue(payload, retained);
}

void MqttInbox::enqueue(std::span<const char> payload, bool retained) {
    // The broker sends a retained message again on every subscribe, which would
    // repeat an old message at each reconnect.
    if (retained) {
        ESP_LOGW(TAG, "Retained on topic %s, message dropped", topic_);
        return;
    }
    if (payload.size() > Payload::BUFFER_SIZE) {
        ESP_LOGW(TAG, "Payload too long on topic %s, message dropped", topic_);
        return;
    }
    payload_.length = static_cast<int>(payload.size());
    std::ranges::copy(payload, payload_.data.begin());
    // Zero timeout, because the event task must never block. A full queue
    // drops the newest message.
    if (xQueueSend(queue_, &payload_, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Queue full on topic %s, message dropped", topic_);
    }
}

bool MqttInbox::take(Payload& payload) {
    return queue_ != nullptr && xQueueReceive(queue_, &payload, 0) == pdTRUE;
}
