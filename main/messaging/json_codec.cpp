#include "messaging/json_codec.hpp"

#include <cJSON.h>
#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <utility>

#include "shared/json_utils.hpp"

namespace {

constexpr char TAG[]{"JSON_CODEC"};

// Nullptr unless the field is an object. Lookups on nullptr find nothing, which
// covers a missing group.
const cJSON* object_field(const cJSON* object, const char* key) {
    const cJSON* item{cJSON_GetObjectItemCaseSensitive(object, key)};
    return (cJSON_IsObject(item) != 0) ? item : nullptr;
}

std::optional<double> number_field(const cJSON* object, const char* key) {
    const cJSON* item{cJSON_GetObjectItemCaseSensitive(object, key)};
    if (cJSON_IsNumber(item) == 0) {
        return std::nullopt;
    }
    return item->valuedouble;
}

std::optional<bool> bool_field(const cJSON* object, const char* key) {
    const cJSON* item{cJSON_GetObjectItemCaseSensitive(object, key)};
    if (cJSON_IsBool(item) == 0) {
        return std::nullopt;
    }
    return cJSON_IsTrue(item) != 0;
}

const char* json_bool(bool value) {
    return value ? "true" : "false";
}

// Strings are written unescaped, and one needing an escape would malform the
// document.
bool strings_fit_unescaped(const Telemetry& telemetry) {
    const auto writable_unescaped{[](const char* field, const char* text) {
        if (json_utils::is_safe_string(text)) {
            return true;
        }
        ESP_LOGE(TAG, "%s carries a quote, a backslash or a control character", field);
        return false;
    }};

    const Telemetry::Device& device{telemetry.device};
    if (!writable_unescaped("device.name", device.name) ||
        !writable_unescaped("device.resetReason", device.reset_reason) ||
        !writable_unescaped("device.firmware.version", device.firmware.version) ||
        !writable_unescaped("device.firmware.build", device.firmware.build) ||
        !writable_unescaped("device.chip.model", device.chip.model) ||
        !writable_unescaped("device.chip.revision", device.chip.revision)) {
        return false;
    }
    if (device.network.wifi &&
        !writable_unescaped("device.network.wifi.mode", device.network.wifi->mode)) {
        return false;
    }
    return std::ranges::all_of(
        std::span{telemetry.faults}.first(telemetry.fault_count),
        [&writable_unescaped](const Telemetry::Fault& fault) {
            return writable_unescaped("faults[].code", fault.code);
        }
    );
}

char* next_byte(Payload& payload) {
    return payload.data.data() + payload.length;
}

std::size_t bytes_left(const Payload& payload) {
    return payload.data.size() - static_cast<std::size_t>(payload.length);
}

// False once a write has failed or did not fit.
bool is_intact_frame(const Payload& payload) {
    return payload.length >= 0;
}

// Formats onto the end of the frame. A write that fails or does not fit sets
// the length to -1, and the frame takes nothing more after it.
// Variadic in the C way, which lets the compiler check each argument against
// the format.
// NOLINTNEXTLINE(modernize-avoid-variadic-functions)
[[gnu::format(printf, 2, 3)]] void append(Payload& payload, const char* format, ...) {
    if (!is_intact_frame(payload)) {
        return;
    }
    const std::size_t left{bytes_left(payload)};
    std::va_list arguments{};
    va_start(arguments, format);
    const int written{std::vsnprintf(next_byte(payload), left, format, arguments)};
    va_end(arguments);
    // Judged before the sum, which then stays below the buffer size
    const bool fits{written >= 0 && std::cmp_less(written, left)};
    payload.length = fits ? payload.length + written : -1;
}

void append_nullable(Payload& payload, const char* key, std::optional<std::int64_t> value) {
    if (value) {
        append(payload, R"(,"%s":%lld)", key, static_cast<long long>(*value));
    } else {
        append(payload, R"(,"%s":null)", key);
    }
}

// Writes the text quoted, or null when there is none. The text must need no JSON
// escape.
void append_text_or_null(Payload& payload, const char* text) {
    if (text != nullptr) {
        append(payload, R"("%s")", text);
    } else {
        append(payload, "null");
    }
}

void append_nullable_text(Payload& payload, const char* key, const char* text) {
    append(payload, R"(,"%s":)", key);
    append_text_or_null(payload, text);
}

template <std::size_t Size>
const char* text_or_null(const std::optional<std::array<char, Size>>& text) {
    return text ? text->data() : nullptr;
}

// Addresses are hex digits, dots and colons, which JSON carries unescaped.
void append_station(Payload& payload, const Telemetry::Station& station) {
    append(payload, R"(,"station":{"mac":)");
    append_text_or_null(payload, text_or_null(station.mac));
    append_nullable_text(payload, "bssid", text_or_null(station.bssid));
    append_nullable(payload, "rssi", station.rssi);
    append_nullable(payload, "reconnects", station.reconnects);
    append_nullable_text(payload, "ipv4", text_or_null(station.ipv4));
    append(payload, "}");
}

void append_access_point(Payload& payload, const Telemetry::AccessPoint& access_point) {
    append(payload, R"(,"accessPoint":{"clients":%u)", static_cast<unsigned>(access_point.clients));
    append_nullable_text(payload, "ipv4", text_or_null(access_point.ipv4));
    append(payload, "}");
}

void append_nullable_tenths(Payload& payload, const char* key, std::optional<float> value) {
    if (value) {
        append(payload, R"(,"%s":%.1f)", key, static_cast<double>(*value));
    } else {
        append(payload, R"(,"%s":null)", key);
    }
}

void append_heap_usage(Payload& payload, const Telemetry::HeapUsage& usage) {
    append(
        payload,
        "{"
        "\"total\":%" PRIu32
        ","
        "\"free\":%" PRIu32
        ","
        "\"minFree\":%" PRIu32
        ","
        "\"largestFreeBlock\":%" PRIu32
        ","
        "\"freeBlocks\":%" PRIu32 "}",
        usage.total,
        usage.free,
        usage.min_free,
        usage.largest_free_block,
        usage.free_blocks
    );
}

void append_heap(Payload& payload, const Telemetry::Heap& heap) {
    append(payload, R"(,"heap":{"internal":)");
    append_heap_usage(payload, heap.internal);
    append(payload, R"(,"psram":)");
    if (heap.psram) {
        append_heap_usage(payload, *heap.psram);
    } else {
        append(payload, "null");
    }
    append(payload, "}");
}

}  // namespace

// Hand-formatted rather than built with cJSON, because every field is known and
// formatting into the caller's buffer needs no heap. Only the fault list varies
// in length, and is appended entry by entry.
bool json_codec::encode_telemetry(const Telemetry& telemetry, Payload& payload) {
    payload.length = 0;
    if (!strings_fit_unescaped(telemetry)) {
        return false;
    }

    const Telemetry::Device& device{telemetry.device};
    append(
        payload,
        "{"
        "\"sequence\":%" PRIu64
        ","
        "\"device\":{"
        "\"name\":\"%s\","
        "\"uptime\":%lld,"
        "\"resetReason\":\"%s\","
        "\"coreDump\":%s,"
        "\"firmware\":{"
        "\"version\":\"%s\","
        "\"build\":\"%s\""
        "},"
        "\"chip\":{"
        "\"model\":\"%s\","
        "\"revision\":\"%s\"",
        telemetry.sequence,
        device.name,
        static_cast<long long>(device.uptime_s),
        device.reset_reason,
        json_bool(device.core_dump),
        device.firmware.version,
        device.firmware.build,
        device.chip.model,
        device.chip.revision
    );
    // One decimal, the temperature sensor's precision.
    append_nullable_tenths(payload, "temperature", device.chip.temperature);
    append(payload, "}");
    append_heap(payload, device.heap);
    append(payload, R"(,"network":{)");

    if (device.network.wifi) {
        const Telemetry::Wifi& wifi{*device.network.wifi};
        append(payload, R"("wifi":{"mode":"%s")", wifi.mode);
        append_nullable(payload, "channel", wifi.channel);
        if (wifi.station) {
            append_station(payload, *wifi.station);
        }
        if (wifi.access_point) {
            append_access_point(payload, *wifi.access_point);
        }
        append(payload, "}");
    }

    append(payload, R"(},"messaging":{)");

    if (device.messaging.mqtt) {
        const Telemetry::Mqtt& mqtt{*device.messaging.mqtt};
        append(
            payload,
            R"("mqtt":{"connected":%s,"reconnects":%)" PRIu32 "}",
            json_bool(mqtt.connected),
            mqtt.reconnects
        );
    }

    append(
        payload,
        "}"
        "},"
        "\"sensor\":{"
        "\"target\":%.2f,"
        "\"deadband\":%.2f,"
        "\"calibration\":{"
        "\"adcMin\":%u,"
        "\"adcMax\":%u"
        "},"
        "\"latest\":%.2f,"
        "\"smoothed\":%.2f,"
        "\"raw\":%" PRIu32
        ","
        "\"ready\":%s,"
        "\"faulted\":%s"
        "},"
        "\"pump\":{"
        "\"runCount\":%" PRIu32
        ","
        "\"on\":%s,"
        "\"currentRunTime\":%lld,"
        "\"idleTime\":%lld,"
        "\"automatic\":%s,"
        "\"duration\":%lld,"
        "\"waitTime\":%lld,"
        "\"remainingWaitTime\":%lld,"
        "\"totalRunTime\":%lld,"
        "\"faulted\":%s"
        "},"
        "\"faults\":[",
        telemetry.sensor.target,
        telemetry.sensor.deadband,
        static_cast<unsigned>(telemetry.sensor.calibration.adc_min),
        static_cast<unsigned>(telemetry.sensor.calibration.adc_max),
        telemetry.sensor.latest,
        telemetry.sensor.smoothed,
        telemetry.sensor.raw,
        json_bool(telemetry.sensor.ready),
        json_bool(telemetry.sensor.faulted),
        telemetry.pump.run_count,
        json_bool(telemetry.pump.on),
        static_cast<long long>(telemetry.pump.current_run_time_s),
        static_cast<long long>(telemetry.pump.idle_time_s),
        json_bool(telemetry.pump.automatic),
        static_cast<long long>(telemetry.pump.duration_s),
        static_cast<long long>(telemetry.pump.wait_time_s),
        static_cast<long long>(telemetry.pump.remaining_wait_time_s),
        static_cast<long long>(telemetry.pump.total_run_time_s),
        json_bool(telemetry.pump.faulted)
    );

    for (std::size_t i{0}; i < telemetry.fault_count; ++i) {
        append(
            payload,
            R"(%s{"code":"%s","active":%s})",
            i == 0 ? "" : ",",
            telemetry.faults[i].code,
            json_bool(telemetry.faults[i].active)
        );
    }
    append(payload, "]}");

    // An overrun drops the whole frame, leaving the payload empty on false rather
    // than half a document.
    if (!is_intact_frame(payload)) {
        payload.length = 0;
        return false;
    }
    return true;
}

// A real parser for the inbound side, where fields are optional, in any order, and
// unknown ones are ignored. cJSON allocates a node per token and recurses once
// per nesting level. Payload::BUFFER_SIZE caps the tokens, and CJSON_NESTING_LIMIT
// in main/CMakeLists.txt caps the depth.
std::optional<Command> json_codec::decode_command(const Payload& payload) {
    cJSON* doc{cJSON_ParseWithLength(payload.data.data(), payload.length)};
    if (doc == nullptr) {
        return std::nullopt;
    }

    const cJSON* sensor{object_field(doc, "sensor")};
    const cJSON* calibration{object_field(sensor, "calibration")};
    const cJSON* pump{object_field(doc, "pump")};

    const Command command{
        .sensor =
            {
                .target = number_field(sensor, "target"),
                .deadband = number_field(sensor, "deadband"),
                .calibration =
                    {
                        .adc_min = number_field(calibration, "adcMin"),
                        .adc_max = number_field(calibration, "adcMax"),
                    },
            },
        .pump = {
            .duration_s = number_field(pump, "duration"),
            .wait_time_s = number_field(pump, "waitTime"),
            .automatic = bool_field(pump, "automatic"),
            .run = bool_field(pump, "run"),
            .stop = bool_field(pump, "stop"),
        },
    };

    cJSON_Delete(doc);
    return command;
}
