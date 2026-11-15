# MQTT

The device connects to a broker when `Mqtt` is listed in `config::MESSAGING` (see
[Configuration](configuration.md#messaging)). One connection carries both [telemetry](telemetry.md)
and [commands](commands.md).

| Setting | Carries |
|---|---|
| `config::MQTT_TOPIC_TELEMETRY` | Frames the device publishes |
| `config::MQTT_TOPIC_COMMAND` | Commands the device subscribes to |
| `config::MQTT_TOPIC_STATUS` | Whether the device is online, retained |

A frame that comes due while the broker is unreachable is skipped, not deferred.

Each device needs its own `config::MQTT_CLIENT_ID`. Two devices sharing one keep pushing each other
off the broker.

## Commands

The device takes a command as it is published. One published while the device is offline never
reaches it, and a retained one is ignored when the broker hands it over on a later connect.

## Status

The status topic carries `online` or `offline`, retained. The device publishes `online` each time it
connects, and the broker publishes `offline` once it loses the device.

## TLS

`config::MQTT_PROTOCOL{"mqtts"}` connects over TLS 1.2 or 1.3, conventionally on port 8883. The
device checks the broker's certificate, which must name `config::MQTT_BROKER`, hostname or IP
address alike. It checks against `config::MQTT_CA_CERT`, or against the public CAs built into
ESP-IDF when that is empty.

A broker that asks for the device's own certificate takes `config::MQTT_CLIENT_CERT` and an
unencrypted `config::MQTT_CLIENT_KEY`, set together.

Each is PEM text, pasted whole and unindented into `main/config.hpp` as a raw string, the `BEGIN`
and `END` lines included:

```cpp
inline constexpr char MQTT_CA_CERT[]{R"(-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
)"};
```

`main/config.hpp` is gitignored, which keeps the key out of the repository. The firmware image
carries it unencrypted, readable by anyone with access to the device's flash.

Certificate expiry goes unchecked, since the device keeps no clock.
