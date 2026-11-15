# Testing

The hardware-independent logic has host-side unit tests (doctest + CTest), and the parsers that
read network input have fuzz targets. Neither needs ESP-IDF or a device.

## What CI checks

Every push to `main` and every pull request runs [`ci.yml`](../.github/workflows/ci.yml):

| Job | Checks |
|---|---|
| `format` | Every source is formatted as [`.clang-format`](../.clang-format) says |
| `host-tests` | The host tests build without a warning and pass |
| `host-tests-sanitized` | The same, under AddressSanitizer and UndefinedBehaviorSanitizer |
| `fuzz` | Each fuzz target runs a minute without a finding |
| `clang-tidy` | Every source the host tests compile passes [`.clang-tidy`](../.clang-tidy) |
| `firmware-build` | The prod firmware builds for the ESP32-C6 from `config.example.hpp` |

## Running the checks as CI does

[`scripts/checks.py`](../scripts/checks.py) runs every job but the firmware build in a Docker
container. It needs Docker, Python 3 and git.

```bash
scripts/checks.py               # Every job, in the order above
scripts/checks.py format tidy   # Jobs: format test sanitize fuzz tidy
```

Each job runs in a fresh container, as each CI job runs on a fresh runner. The run stops at the
first job that fails and closes with a summary of every job: passed, failed, canceled with Ctrl+C,
or skipped.

The containers get a copy of the tree as it stands when the run starts, as a commit would leave it:
the tracked files and new ones not yet added, but nothing gitignored, which keeps `main/config.hpp`
out of them. Nothing is written back, and builds on the host are left alone. The first run builds
the image from [`scripts/checks/Dockerfile`](../scripts/checks/Dockerfile), which takes a few
minutes, as does the first run after the Dockerfile changes.

Each job runs the commands of its CI job, with the same tools:

| Tool | Version | From |
|---|---|---|
| clang-format and clang-tidy | 22.1.8 | PyPI, exact |
| CMake | 3.31.6 | PyPI, exact |
| GCC | 14 | Ubuntu 24.04's archive |
| Clang, for fuzzing | 20 | Ubuntu 24.04's archive |

The Dockerfile and `ci.yml` install these the same way. The exact versions move only when both files
change; the others follow Ubuntu's updates. One difference: clang-tidy runs on every core.

The firmware build is not among these jobs: `scripts/matrix.py` builds it in the `espressif/idf`
container (see below), and `scripts/firmware.py` on the host (see
[Firmware](firmware.md#build-and-flash)).

## Building for every chip

[`scripts/matrix.py`](../scripts/matrix.py) builds the prod firmware for every chip
[Firmware](firmware.md#chips-and-esp-idf-versions) lists, in the `espressif/idf` container. It needs
Docker, Python 3 and git.

```bash
scripts/matrix.py                             # Every chip, on ESP-IDF v5.5.6
scripts/matrix.py esp32c6 esp32h2             # Only these chips
scripts/matrix.py -v esp32c5                  # With each build's full output
scripts/matrix.py --idf v5.4.2 esp32 esp32c6  # On another ESP-IDF version
```

Each chip builds from `config.example.hpp`, with the pump on GPIO4, which every chip has:

| Build | Config | Passes when |
|---|---|---|
| `full` | Every feature on, but WiFi off on the ESP32-H2 and ESP32-P4, which have no radio | It builds with every feature's code in it |
| `min` | WiFi, messaging, the dashboard and the LED off | It builds with only what the bare minimum needs |
| `wifi-forced` | ESP32-H2 and ESP32-P4 only: WiFi on | The build stops on "this chip has no WiFi radio" |

A warning in `main/` fails the build. Each build runs in a fresh container on a copy of the tree, as
`scripts/checks.py` does, and nothing is written back. The builds run one after another. The first
run takes longer, as it fills the compiler cache and downloads the components, both kept in Docker
volumes for the runs after it.

Each build prints one line. A failed one adds what failed it, and `-v` streams its whole output
instead. A summary of every build closes the run, and the exit status is 0 only when every build
passed.

## Running natively

The host tests need CMake >= 3.20 and a C++23 compiler (GCC >= 13 or Clang >= 19; on macOS, Apple
Clang >= 21 from Xcode 26).

```bash
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build
```

`-DSANITIZE=ON` on the first command runs the same tests under AddressSanitizer and
UndefinedBehaviorSanitizer.

CI's clang-format, clang-tidy and CMake install from PyPI at the same versions:

```bash
pip install clang-format==22.1.8 clang-tidy==22.1.8 cmake==3.31.6
```

On macOS, clang-tidy can report less than it does on Linux. The container's result is the one CI
gets.

## Fuzzing

Each `test/host/fuzz_*.cpp` builds a target of the same name. They need Clang >= 19 with
libFuzzer, which Apple's Clang lacks. Outside the container they run on Linux (Ubuntu 24.04:
`clang-20` and `libclang-rt-20-dev`):

```bash
cmake -S test/host -B test/host/build/fuzz -DFUZZ=ON \
    -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20
cmake --build test/host/build/fuzz --target fuzz_command
test/host/build/fuzz/fuzz_command -max_total_time=60 -dict=test/host/fuzz.dict \
    -seed_inputs=test/host/fuzz_seeds/fuzz_command
```

A finding is saved as a file named after its kind (`crash-...`, `leak-...`, `timeout-...` or
`oom-...`). `scripts/checks.py fuzz` prints each one's name and its contents in base64 instead,
which decodes back into a file:

```bash
echo '<base64>' | base64 --decode > finding
```

The target replays a file passed to it:

```bash
test/host/build/fuzz/fuzz_command finding
```
