#include "messaging/json_format.hpp"

#include "messaging/json_codec.hpp"

const char* JsonFormat::media_type() const {
    return MEDIA_TYPE;
}

bool JsonFormat::encode(const Telemetry& telemetry, Payload& payload) const {
    return json_codec::encode_telemetry(telemetry, payload);
}

std::optional<Command> JsonFormat::decode(const Payload& payload) const {
    return json_codec::decode_command(payload);
}
