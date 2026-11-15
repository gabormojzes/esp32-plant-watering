#pragma once

#include <optional>

#include "messaging/command.hpp"
#include "messaging/payload.hpp"
#include "messaging/telemetry.hpp"

// The JSON shape of the documents docs/telemetry.md and docs/commands.md
// describe
namespace json_codec {

// Writes the frame as JSON. Returns false, with an empty payload, when a string
// would need escaping or the frame does not fit.
[[nodiscard]] bool encode_telemetry(const Telemetry& telemetry, Payload& payload);

// Parses a command. Returns nullopt for invalid JSON or nesting past the depth
// cap. Fields the payload does not carry stay unset, and carried values are
// returned as sent.
std::optional<Command> decode_command(const Payload& payload);

}  // namespace json_codec
