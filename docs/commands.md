# Commands

The device takes settings changes and pump requests as JSON commands, the same over every
transport ([MQTT](mqtt.md), [HTTP API](http-api.md)).

## Fields

| Field | Type | Range | Effect |
|---|---|---|---|
| `sensor.target` | number | `0` to `100`, rounded to a tenth | Moisture target, % |
| `sensor.deadband` | number | `0` to `100`, rounded to a tenth | Hysteresis either side of the target, % |
| `sensor.calibration.adcMin` | integer | `0` or more, below `adcMax` | Raw reading of the sensor in water, read as 100% |
| `sensor.calibration.adcMax` | integer | Above `adcMin`, up to `4095` (`8191` on the ESP32-S2) | Raw reading of the dry sensor, read as 0% |
| `pump.duration` | integer | `1` to `300` (5 min) | Run length, seconds |
| `pump.waitTime` | integer | `0` to `86400` (1 day) | Wait between runs, seconds |
| `pump.automatic` | boolean | `true` or `false` | Water whenever the soil calls for it. `false` starts no automatic run and stops one in progress |

```json
{
  "sensor": {
    "target": 50,
    "deadband": 0.5,
    "calibration": {
      "adcMin": 0,
      "adcMax": 2900
    }
  },
  "pump": {
    "duration": 60,
    "waitTime": 3600,
    "automatic": true
  }
}
```

A group with nothing to set can be left out entirely. Anything else in the payload is ignored, and a
field carrying the wrong type reads the same as one that is absent.

## Requests

Two pump fields act once instead of setting anything, and are never stored.

| Field | Type | Effect |
|---|---|---|
| `pump.run` | boolean | `true` runs the pump for `pump.duration`, whatever the soil reads |
| `pump.stop` | boolean | `true` ends the run in progress, automatic or manual |

```json
{
  "pump": {
    "run": true
  }
}
```

A run starts whenever the pump is off and not faulted: during the boot lockout or the wait after a
run, with `pump.automatic` off, and on wet soil. A run asked for while the pump runs is ignored. A
manual run ends at `pump.duration` or on `pump.stop`, and neither the reading nor `pump.automatic`
ends it.

However a run ends, the wait counts from that moment, the same as after an automatic run. With
`pump.waitTime` at `0`, soil still below the band starts an automatic run straight after a stop.
`pump.automatic` at `false` is what keeps the pump off.

`false` asks for nothing, and a payload carrying both gets the stop. Settings in the same payload
apply first: `{"pump":{"duration":30,"run":true}}` waters for 30 seconds.

## Validation

The percent fields run on tenths. A finer value is rounded to the nearest tenth, and that tenth is
set: `55.55` sets `55.6` rather than being refused. Rounding moves a value by at most `0.05` and
happens before the `0` to `100` check, which judges the value the device would run on. `100.02` sets
`100`, while `100.06` sets nothing and drops its partner too, since target and deadband are judged as
one band.

`target - deadband` and `target + deadband` must both land in `0` to `100`, or neither field
applies. A payload carrying only one of them is judged against the value in use. Moving a wide band
across the scale takes both in one payload.

The calibration fields are whole raw readings, as `sensor.raw` reports them in
[telemetry](telemetry.md): `adcMin` is the sensor's raw reading in water, `adcMax` its raw reading
dry. `adcMin` must be below `adcMax`, or neither applies.

A raw reading above `adcMax` reads as a disconnected sensor: it raises `ESEN03` and stops
automatic watering, as long as `adcMax` stays below what the sensor reads unplugged.

A refused field leaves the value in use untouched, and the rest of the payload still applies - bar
the band and the calibration, each a pair that stands or falls together.

## Persistence

Accepted values apply immediately and persist to flash (NVS), surviving a reboot.
[Firmware](firmware.md#stored-settings) says how to reset them.
