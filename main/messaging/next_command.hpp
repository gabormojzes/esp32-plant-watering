#pragma once

#include <esp_log.h>

#include <optional>

#include "messaging/command.hpp"
#include "messaging/message_format.hpp"
#include "messaging/payload.hpp"

// Takes payloads from `source` until one decodes, skipping any that do not.
// Returns nullopt once the source is empty.
template <typename Source>
[[nodiscard]] std::optional<Command> next_command(
    Source& source, const MessageFormat& format, Payload& payload
) {
    // Static, because the log caches a tag's level by its address
    static constexpr char tag[]{"MESSAGING"};

    while (source.take(payload)) {
        if (std::optional<Command> command{format.decode(payload)}) {
            return command;
        }
        ESP_LOGW(tag, "Payload is not %s, skipped", format.media_type());
        ESP_LOGD(tag, "Raw payload: %.*s", payload.length, payload.data.data());
    }
    return std::nullopt;
}
