# Firmware

How to build the firmware and flash it, what a flash keeps on the device, and how to read what a
crash left behind.

## Chips and ESP-IDF versions

The firmware is developed and tested with **ESP-IDF v5.5.6** on an **ESP32-C6**
([install guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32c6/get-started/)).
Builds are also checked for these chips and ESP-IDF versions:

| Chips | ESP-IDF |
|---|---|
| ESP32, ESP32-S2, ESP32-S3, ESP32-C2, ESP32-C3, ESP32-C6, ESP32-H2, ESP32-P4 | >= 5.4.2 and <= 5.5.6 |
| ESP32-C5, ESP32-C61 | >= 5.5.1 and <= 5.5.6 |

The default chip is `CONFIG_IDF_TARGET` in [`sdkconfig.defaults`](../sdkconfig.defaults). Another
chip needs its own pins in `main/config.hpp` (see [Hardware](hardware.md#pin-map)).

## Profiles

The build has two profiles layered on [`sdkconfig.defaults`](../sdkconfig.defaults):

| Profile | File | Logging | Optimization |
|---|---|---|---|
| **dev** | [`sdkconfig.dev`](../sdkconfig.dev) | Full `ESP_LOGx` | Default |
| **prod** | [`sdkconfig.prod`](../sdkconfig.prod) | Stripped | Size (`-Os`) |

## Build and flash

A build needs `main/config.hpp` (see [Configuration](configuration.md)) and the ESP-IDF environment,
sourced in each terminal:

```bash
. ~/esp/v5.5.6/esp-idf/export.sh
```

`scripts/firmware.py` takes a profile, a chip and one or more commands:

```bash
scripts/firmware.py flash              # Flashes prod to the default chip. -p <port> names the port.
scripts/firmware.py dev flash monitor  # Logs stream in dev only.
scripts/firmware.py esp32s3 build
```

Each profile and chip builds in its own `build/<chip>-<profile>/`, and switching between them needs
no cleaning. `scripts/firmware.py -h` lists every command.

The script prints each `idf.py` command it runs, and after a command that builds, the firmware's
build id, which telemetry reports as `device.firmware.build`. Rebuilding the same source and config
on the same machine gives the same id. The same build without the script:

```bash
idf.py -B build/esp32c6-prod -D IDF_TARGET=esp32c6 \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.prod" build
idf.py -B build/esp32c6-prod flash
```

## Flash layout

The image declares 2 MB (`CONFIG_ESPTOOLPY_FLASHSIZE_2MB`), and
[`partitions.csv`](../partitions.csv) gives the app a single slot followed by one for a core dump,
ending at 2 MB; one build runs on a 2, 4 or 8 MB module. The declared size is a floor. Under the
chip's real size the tail goes unused, and over it the image will not boot.

## Stored settings

Flashing leaves NVS untouched; a reflashed device keeps whatever it was last commanded (see
[Commands](commands.md#persistence)). A flash does clear the core dump, which belongs to the
firmware it replaces. Bumping `config::SETTINGS_VERSION` in `main/config.hpp` drops the stored
settings on the next boot and re-seeds from the boot defaults.

Clearing the partition itself, including what the WiFi driver keeps there, takes an erase of the
`nvs` partition with the monitor closed, and a device reset after it:

```bash
scripts/firmware.py erase-nvs
```

## Troubleshooting

A crash leaves a core dump in flash, which telemetry reports as `device.coreDump`. Only the first
crash since the last flash is kept. The `coredump` command reads it from the device. It needs the
profile and chip the device was flashed with, and a serial port that no monitor holds open:

```bash
scripts/firmware.py coredump      # -p <port> names the port.
scripts/firmware.py dev coredump  # After a dev flash
```

It prints the crashed task, the reason, a backtrace with source lines and every other task's state,
or `No core dump on the device.` It decodes only against the firmware the device was flashed with,
which a rebuild from the same source and config on the same machine gives back. After any change it
refuses, since a backtrace decoded against another build points at the wrong code.
