// SPDX-FileCopyrightText: 2021 Amazon.com, Inc. or its affiliates
// SPDX-License-Identifier: MIT
//
// Changed from the original: cut down to what the host tests use, with host
// behavior added.

#pragma once

// Host stub for the FreeRTOS queue API. It is a working fixed-capacity queue,
// since the endpoints' behavior at a full queue or a peeked slot is under test.
// Items are copied by size, as FreeRTOS does. Queues are never freed, as on the
// device.

#include <freertos/FreeRTOS.h>

#include <cstddef>
#include <cstring>

// As ESP-IDF 5.5.6 declares it in freertos/queue.h
typedef struct QueueDefinition* QueueHandle_t;

// The queue FreeRTOS leaves opaque
struct QueueDefinition {
    QueueDefinition(std::size_t length, std::size_t item_size)
        : length_{length}, item_size_{item_size}, storage_{new char[length * item_size]} {}

    ~QueueDefinition() {
        delete[] storage_;
    }

    // A copy would free the storage twice.
    QueueDefinition(const QueueDefinition&) = delete;
    QueueDefinition& operator=(const QueueDefinition&) = delete;

    bool send(const void* item) {
        if (count_ == length_) {
            return false;
        }
        std::memcpy(slot((head_ + count_) % length_), item, item_size_);
        ++count_;
        return true;
    }

    // Replaces whatever the slot holds, which is why FreeRTOS only allows it on
    // a queue of one.
    void overwrite(const void* item) {
        std::memcpy(slot(head_), item, item_size_);
        count_ = 1;
    }

    bool receive(void* item) {
        if (!peek(item)) {
            return false;
        }
        head_ = (head_ + 1) % length_;
        --count_;
        return true;
    }

    bool peek(void* item) const {
        if (count_ == 0) {
            return false;
        }
        std::memcpy(item, slot(head_), item_size_);
        return true;
    }

private:
    char* slot(std::size_t index) const {
        return storage_ + (index * item_size_);
    }

    std::size_t length_;
    std::size_t item_size_;
    char* storage_;
    std::size_t head_{0};
    std::size_t count_{0};
};

inline QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    return new QueueDefinition{length, item_size};
}

inline void vQueueDelete(QueueHandle_t queue) {
    delete queue;
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t /*xTicksToWait*/) {
    return queue->send(item) ? pdTRUE : pdFALSE;
}

inline BaseType_t xQueueOverwrite(QueueHandle_t queue, const void* item) {
    queue->overwrite(item);
    return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t /*xTicksToWait*/) {
    return queue->receive(item) ? pdTRUE : pdFALSE;
}

inline BaseType_t xQueuePeek(QueueHandle_t queue, void* item, TickType_t /*xTicksToWait*/) {
    return queue->peek(item) ? pdTRUE : pdFALSE;
}
