# Configuration

Build-time configuration decides what the device is: which radio it runs, which transports carry its
data, whether it serves a dashboard, and whether it drives a status LED. It is set in
`main/config.hpp`, bar the dashboard template in `main/CMakeLists.txt`, and compiled in. A running
device accepts no change to any of it. [Commands](commands.md) lists the settings it does accept.

```bash
cp main/config.example.hpp main/config.hpp
```

The template documents every setting where it is declared: credentials, pins, sensor calibration and
the rest. The ones below decide what the firmware contains at all.

The settings [Commands](commands.md) lists are boot defaults. Once a command has changed any of
them, the device keeps its stored values over `config.hpp`. [Firmware](firmware.md#stored-settings)
says how to reset them.

## Messaging

`config::MESSAGING` lists the transports that carry telemetry and commands. Each carries both, as
JSON, and any combination runs. JSON is the only format built in.

| `MessagingTransport` | Delivery | Settings used |
|---|---|---|
| `Mqtt` | Publish/subscribe through a broker | `MQTT_*` |
| `Http` | Request/response to a server on the device | `HTTP_*` |

An empty list runs the device without messaging. No telemetry leaves it, and nothing can command the
pump. It still waters on its own.

## WiFi

`config::WIFI_ENABLED` switches WiFi on, and `config::WIFI_MODE` picks the role the radio runs.

| `WifiMode` | The device | Settings used |
|---|---|---|
| `Sta` | Joins an existing network | `WIFI_STA_*` |
| `Ap` | Serves a network of its own at `192.168.4.1` | `WIFI_AP_*` |
| `ApSta` | Does both on the one radio | Both |

On a chip without a WiFi radio, such as the ESP32-H2 or ESP32-P4, only `config::WIFI_ENABLED{false}`
builds.

An `Ap` network needs no router, which makes it the way to reach a device where no network is in
range. It routes nothing onward; `Mqtt` connects only when the broker is itself a client of it.

`config::WIFI_AP_PASSWORD` is 8 to 63 characters. The network is never served open.

## Dashboard

`config::DASHBOARD_ENABLED` serves the dashboard at `GET /`. The dashboard is a client of the HTTP
API and requires `Http` in `config::MESSAGING`. The build rejects it otherwise.

The dashboard sits behind the login page at `GET /login`, which takes `config::DASHBOARD_USER` and
`config::DASHBOARD_PASSWORD`, and reaches `/api` with the session the login opens. Every other
client reaches `/api` with `config::HTTP_API_TOKEN`. Each has a minimum length (see
[HTTP API](http-api.md#credentials)).

The dashboard and its login page come from a template, a folder under `main/web/pages/` holding a
`dashboard.html` and a `login.html`. One ships: `default`. `DASHBOARD_TEMPLATE` in
`main/CMakeLists.txt` names the folder, and only that pair is built in. A name with no such folder
fails the build.

## Status LED

`config::LED_ENABLED` drives a WS2812 status LED on `config::LED_PIN`, showing the device's state
(see [Faults](faults.md#status-led)). Without it, faults show only in telemetry.

## What switching something off means

Anything left out is compiled out rather than disabled at runtime, and its settings go unused.
