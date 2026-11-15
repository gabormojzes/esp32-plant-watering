# Telemetry

The device reports its state as a JSON frame, produced every `config::TELEMETRY_INTERVAL_S`. The
frame is the same over every transport ([MQTT](mqtt.md), [HTTP API](http-api.md)).

## Fields

| Field | Type | Unit | Description |
|---|---|---|---|
| `sequence` | integer | | Frame number since boot, starting at 1; a gap means frames were skipped |
| `device.name` | string | | Name for this device, from `config::DEVICE_NAME` |
| `device.uptime` | integer | s | Seconds since boot |
| `device.resetReason` | string | | Why the chip last reset: `powerOn`, `external`, `software`, `panic`, `interruptWdt`, `taskWdt`, `otherWdt`, `deepSleep`, `brownout`, `sdio`, `usb`, `jtag`, `efuse`, `powerGlitch`, `cpuLockup` or `unknown` |
| `device.coreDump` | boolean | | A crash left a core dump in flash, kept until the next flash. A `panic`, `taskWdt` or `interruptWdt` reset writes one, and only the first is kept. See [Troubleshooting](firmware.md#troubleshooting) |
| `device.firmware.version` | string | | Firmware version, from `PROJECT_VER` in the top-level `CMakeLists.txt` |
| `device.firmware.build` | string | | Build id: the leading hex digits of the firmware ELF's SHA-256, as a core dump records them. `scripts/firmware.py` prints it after a build. See [Troubleshooting](firmware.md#troubleshooting) |
| `device.chip.model` | string | | The chip family the firmware runs on, such as `esp32c6` |
| `device.chip.revision` | string | | Silicon revision as `v` major `.` minor, such as `v0.1` |
| `device.chip.temperature` | number or `null` | °C | Temperature inside the chip, which runs warmer than the air around it. Accurate to about 1 °C between -10 and 80 °C. `null` on a chip without the sensor, such as the original ESP32, or when a reading fails |
| `device.heap.internal.total` | integer | B | Size of the internal RAM heap. Includes the heap's own bookkeeping, which keeps `free` below it |
| `device.heap.internal.free` | integer | B | Free internal RAM |
| `device.heap.internal.minFree` | integer | B | Lowest free internal RAM since boot. Can read slightly low, never high |
| `device.heap.internal.largestFreeBlock` | integer | B | Largest single block of internal RAM that can still be allocated |
| `device.heap.internal.freeBlocks` | integer | | Pieces the free internal RAM is split into |
| `device.heap.psram` | object or `null` | | The same five figures for PSRAM; `null` when the device has no PSRAM in use |
| `device.network` | object | | Network information; empty while `config::WIFI_ENABLED` is off |
| `device.network.wifi.mode` | string | | The role the radio runs, from `config::WIFI_MODE`: `sta`, `ap` or `apsta` |
| `device.network.wifi.channel` | integer or `null` | | The channel the radio runs on, shared by both roles. In `apsta` the access point moves to the joined network's channel. `null` while the radio is down, and while a station alone is still looking for its network |
| `device.network.wifi.station` | object | | The station, present in `sta` and `apsta` |
| `device.network.wifi.station.mac` | string or `null` | | The station's MAC address, as `aa:bb:cc:dd:ee:ff`; `null` while the radio is down |
| `device.network.wifi.station.bssid` | string or `null` | | MAC address of the access point joined, which tells mesh nodes and extenders apart; `null` while not joined |
| `device.network.wifi.station.rssi` | integer or `null` | dBm | Signal strength of the access point joined; `null` while not joined |
| `device.network.wifi.station.reconnects` | integer or `null` | | Times the link came back after dropping, since boot; the first link is not one, however long it takes. `null` while the radio is down |
| `device.network.wifi.station.ipv4` | string or `null` | | The address the network assigned; `null` until it has |
| `device.network.wifi.accessPoint` | object | | The device's own network, present in `ap` and `apsta` |
| `device.network.wifi.accessPoint.clients` | integer | | Devices joined to it |
| `device.network.wifi.accessPoint.ipv4` | string or `null` | | The device's address on it; `null` while the radio is down |
| `device.messaging` | object | | Messaging information; empty unless `config::MESSAGING` lists `Mqtt` |
| `device.messaging.mqtt.connected` | boolean | | Connected to the broker |
| `device.messaging.mqtt.reconnects` | integer | | Times the connection to the broker came back after dropping, since boot; the first connection is not one |
| `sensor.target` | number | % | Moisture target |
| `sensor.deadband` | number | % | Hysteresis either side of the target |
| `sensor.calibration.adcMin` | integer | count | Raw reading of the sensor in water, read as 100%, see [Commands](commands.md) |
| `sensor.calibration.adcMax` | integer | count | Raw reading of the dry sensor, read as 0% |
| `sensor.latest` | number | % | Latest reading |
| `sensor.smoothed` | number | % | Smoothed reading, exponential moving average. Starts afresh from the first good reading after an unusable one |
| `sensor.raw` | integer | count | Raw ADC reading, for calibration. Updated whenever the sensor returns a reading, even one unusable as moisture; unchanged when it returns none |
| `sensor.ready` | boolean | | A reading has landed |
| `sensor.faulted` | boolean | | The latest reading was unusable; `latest` and `smoothed` keep the last good one |
| `pump.runCount` | integer | | Runs since boot |
| `pump.on` | boolean | | Pump currently running |
| `pump.currentRunTime` | integer | s | Seconds into the current run; `0` while the pump is off |
| `pump.idleTime` | integer | s | Seconds since the pump last stopped, or since boot before the first run; `0` while it runs |
| `pump.automatic` | boolean | | Automatic watering is on; while off, no automatic run starts |
| `pump.duration` | integer | s | Configured run length |
| `pump.waitTime` | integer | s | Configured wait between runs |
| `pump.remainingWaitTime` | integer | s | Seconds before an automatic run may start, `0` when eligible. Counts down while automatic watering is off too |
| `pump.totalRunTime` | integer | s | Seconds the pump has run since boot |
| `pump.faulted` | boolean | | The pump cannot switch; no run starts |
| `faults` | array | | Every fault raised since boot, empty when there are none |
| `faults[].code` | string | | The fault's code |
| `faults[].active` | boolean | | The condition still holds |

[Faults](faults.md) maps each fault code.

## Example

```json
{
  "sequence": 20173,
  "device": {
    "name": "Device 1",
    "uptime": 20173,
    "resetReason": "powerOn",
    "coreDump": false,
    "firmware": {
      "version": "0.1.0",
      "build": "b6c3172ee"
    },
    "chip": {
      "model": "esp32c6",
      "revision": "v0.1",
      "temperature": 38.4
    },
    "heap": {
      "internal": {
        "total": 362396,
        "free": 237968,
        "minFree": 225232,
        "largestFreeBlock": 217088,
        "freeBlocks": 9
      },
      "psram": null
    },
    "network": {
      "wifi": {
        "mode": "sta",
        "channel": 6,
        "station": {
          "mac": "00:00:5e:00:53:af",
          "bssid": "00:00:5e:00:53:01",
          "rssi": -67,
          "reconnects": 0,
          "ipv4": "192.168.1.5"
        }
      }
    },
    "messaging": {
      "mqtt": {
        "connected": true,
        "reconnects": 0
      }
    }
  },
  "sensor": {
    "target": 65.00,
    "deadband": 0.50,
    "calibration": {
      "adcMin": 0,
      "adcMax": 2930
    },
    "latest": 65.43,
    "smoothed": 65.39,
    "raw": 1013,
    "ready": true,
    "faulted": false
  },
  "pump": {
    "runCount": 0,
    "on": false,
    "currentRunTime": 0,
    "idleTime": 20173,
    "automatic": true,
    "duration": 1,
    "waitTime": 3600,
    "remainingWaitTime": 0,
    "totalRunTime": 0,
    "faulted": false
  },
  "faults": [
    { "code": "WWIF02", "active": false }
  ]
}
```
