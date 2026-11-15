# Faults

A component raises a fault when something goes wrong. A fault never stops the device. The status LED
shows the worst severity currently active, and telemetry carries every code raised since boot.

## Severity

| Severity | Meaning |
|---|---|
| Error | The device can no longer be relied on to perform its primary function |
| Warning | The device still performs its primary function, but something needs attention |

## Status LED

| LED | Meaning |
|---|---|
| Blue | Powered on, still bringing up |
| Green | Running, nothing faulted |
| Blinking yellow | A warning is active |
| Blinking red | An error is active |

A blink masks whatever the LED would otherwise show. When a fault's condition clears, it stops
driving the LED and the color underneath returns.

## Code format

Every code is six characters: severity (`E` error, `W` warning), a three-letter component, and a
two-digit id.

## Codes

| Code | Severity | Component | Meaning |
|---|---|---|---|
| `ESEN01` | Error | Sensor | Sensor ADC init failed |
| `ESEN02` | Error | Sensor | All sensor ADC reads failed |
| `ESEN03` | Error | Sensor | Sensor reading out of range |
| `ESEN04` | Error | Sensor | Sensor task start failed |
| `ESEN05` | Error | Sensor | Sensor calibration range maps nothing |
| `ESEN06` | Error | Sensor | Sensor smoothed reading unavailable |
| `EPMP01` | Error | Pump | Pump bring-up failed |
| `WLED01` | Warning | Status LED | Status LED init failed |
| `EWDG01` | Error | Watchdog | Watchdog task subscribe failed |
| `WSET01` | Warning | Settings | Settings storage unavailable |
| `WSET02` | Warning | Settings | Stored settings unreadable |
| `WSET03` | Warning | Settings | Settings not written |
| `WWIF01` | Warning | WiFi | WiFi bring-up failed |
| `WWIF02` | Warning | WiFi | WiFi link down |
| `WMQT01` | Warning | MQTT client | MQTT client start failed |
| `WMQT02` | Warning | MQTT client | MQTT broker refused the connection |
| `WMQT03` | Warning | MQTT client | MQTT topic not subscribed |
| `WCMD01` | Warning | Commands | MQTT command queue unavailable |
| `WCMD02` | Warning | Commands | HTTP command queue unavailable |
| `WTLM01` | Warning | Telemetry | MQTT telemetry mailbox unavailable |
| `WTLM02` | Warning | Telemetry | MQTT publisher task start failed |
| `WTLM03` | Warning | Telemetry | HTTP telemetry mailbox unavailable |
| `WWEB01` | Warning | Web server | Web server start failed |
| `WWEB02` | Warning | Web server | Web route registration failed |

## In telemetry

An entry never leaves the `faults` array. A code is listed once however often it is raised, and a
repeat only puts `active` back to `true` where the fault had cleared. A fault that clears stays
listed with `active` back to `false`. Conditions checked only during bring-up stay `active` until
reboot.
