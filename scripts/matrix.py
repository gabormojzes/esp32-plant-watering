#!/usr/bin/env python3
"""Builds the prod firmware for every chip in containers, with every feature and with the fewest.

usage: scripts/matrix.py [CHIP...] [--idf TAG] [-v]

  CHIP         one or more of the chips below (default: all of them, in this order)
  --idf TAG    the espressif/idf image tag to build with (default: v5.5.6, pinned to its digest)
  -v           streams each build's output as it runs

chips:
  esp32 esp32s2 esp32s3 esp32c2 esp32c3 esp32c5 esp32c6 esp32c61 esp32h2 esp32p4

builds of each chip:
  full         Every feature on, as config.example.hpp ships, but WiFi off on a chip without a
               radio (esp32h2, esp32p4).
  min          The bare minimum: WiFi, messaging, the dashboard and the LED off.
  wifi-forced  Only on a chip without a radio: WiFi on, which the build must refuse.

A full or min build passes when it builds, which a warning in main/ fails, and holds exactly the
symbols its features compile in. A wifi-forced build passes when it stops on "this chip has no
WiFi radio". Every build drives the pump from GPIO4, since GPIO22 does not exist on every chip.

Each build runs in a fresh container, one after another. The tree is copied in as it stands when
the run starts: the tracked files and new ones not yet added, as a commit would leave them, but
nothing gitignored, with main/config.hpp made from main/config.example.hpp. Nothing is written
back. A failed build prints what failed it, and a summary of every build closes the run. The
compiler cache and downloaded components are kept in the Docker volumes plant-ccache and
plant-cm. The script prints every command it runs. It needs Docker and git.

examples:
  scripts/matrix.py
  scripts/matrix.py esp32c6 esp32h2
  scripts/matrix.py -v esp32c5
  scripts/matrix.py --idf v5.4.2 esp32 esp32c6
"""

import argparse
import io
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG_TEMPLATE = ROOT / "main" / "config.example.hpp"
IMAGE = "espressif/idf"
DEFAULT_IDF = "v5.5.6"
# A tag can be moved to another image. The default stays on the one it named when it was chosen.
DEFAULT_IDF_DIGEST = "sha256:bf72fe7a15c22849a9a94147301165a545fb8905d97c176cf748e3e4614cac84"

CHIPS = (
    "esp32",
    "esp32s2",
    "esp32s3",
    "esp32c2",
    "esp32c3",
    "esp32c5",
    "esp32c6",
    "esp32c61",
    "esp32h2",
    "esp32p4",
)
NO_WIFI_CHIPS = ("esp32h2", "esp32p4")
XTENSA_CHIPS = ("esp32", "esp32s2", "esp32s3")

DOCKER_OPTIONS = [
    "-e", "IDF_CCACHE_ENABLE=1",
    "-e", "CCACHE_DIR=/ccache",
    "-v", "plant-ccache:/ccache",
    "-v", "plant-cm:/root/.cache",
]

# What each set of features compiles in, as names in the app's symbols. A feature that is off
# leaves a null object in its place.
FULL_SYMBOLS = frozenset(
    {
        "Dashboard",
        "HttpTransport",
        "JsonFormat",
        "LedIndicator",
        "Login",
        "MqttClient",
        "MqttTransport",
        "RgbLed",
        "TransportGroup",
        "WebServer",
        "WifiManager",
        "cJSON_Parse",
        "json_codec",
    }
)
FULL_NO_WIFI_SYMBOLS = FULL_SYMBOLS - {"WifiManager"} | {"NullNetworkProvider"}
MIN_SYMBOLS = frozenset({"NullIndicator", "NullNetworkProvider", "NullWebHost", "TransportGroup"})
TRACKED_SYMBOLS = FULL_SYMBOLS | MIN_SYMBOLS

NO_WIFI_REFUSAL = "this chip has no WiFi radio"

# Prefixes the lines the container reports results on, among the build's output.
RESULT_MARK = "@@matrix"

ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
NINJA_PROGRESS = re.compile(r"\[\d+/\d+\] ")
EXCERPT_LINES = 60

# Mark and ANSI color of each outcome in the summary
OUTCOMES = {
    "passed": ("✓", "32"),
    "failed": ("✗", "31"),
    "canceled": ("⊘", "33"),
    "skipped": ("-", "90"),
}

COLOR = sys.stdout.isatty() and "NO_COLOR" not in os.environ


@dataclass(frozen=True)
class Build:
    chip: str
    variant: str
    config: str
    # None for a build that must be refused
    symbols: frozenset | None

    @property
    def name(self):
        return f"{self.chip}-{self.variant}"


@dataclass(frozen=True)
class Result:
    outcome: str
    seconds: float
    note: str
    details: tuple = ()


def paint(text, color):
    return f"\033[{color}m{text}\033[0m" if COLOR else text


def parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        usage=argparse.SUPPRESS,
    )
    parser.add_argument("chips", nargs="*", help=argparse.SUPPRESS)
    parser.add_argument("--idf", default=DEFAULT_IDF, help=argparse.SUPPRESS)
    parser.add_argument("-v", dest="verbose", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_intermixed_args()
    for chip in args.chips:
        if chip not in CHIPS:
            parser.error(f"unknown chip '{chip}'; pick from: {', '.join(CHIPS)}")
    args.chips = list(dict.fromkeys(args.chips)) or list(CHIPS)
    return args


def run(command):
    print(f"$ {shlex.join(command)}", flush=True)
    if subprocess.run(command).returncode != 0:
        sys.exit(1)


def check_docker():
    if not shutil.which("docker"):
        sys.exit("Docker is not installed")
    running = subprocess.run(["docker", "info"], capture_output=True).returncode == 0
    if not running:
        sys.exit("Docker is not running; start it first")


def pull_image(image):
    exists = subprocess.run(["docker", "image", "inspect", image], capture_output=True)
    if exists.returncode != 0:
        run(["docker", "pull", image])


def tree_archive():
    """Packs what a commit would leave in the tree."""
    if not (ROOT / ".git").is_dir():
        sys.exit("Not a git checkout; the copy into the container follows git's file list")
    listed = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        capture_output=True,
        check=True,
    )
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w") as archive:
        for name in filter(None, listed.stdout.split(b"\0")):
            name = os.fsdecode(name)
            # A tracked file deleted from the tree is still listed until the deletion is staged.
            if os.path.lexists(ROOT / name):
                archive.add(ROOT / name, arcname=name, recursive=False)
    return buffer.getvalue()


def with_config(tree, config):
    """Adds config as main/config.hpp to a copy of the tree's archive."""
    buffer = io.BytesIO(tree)
    data = config.encode()
    info = tarfile.TarInfo("main/config.hpp")
    info.size = len(data)
    info.mode = 0o644
    info.mtime = int(time.time())
    with tarfile.open(fileobj=buffer, mode="a") as archive:
        archive.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


def replace_once(text, pattern, replacement):
    result, count = re.subn(pattern, replacement, text, flags=re.MULTILINE | re.DOTALL)
    if count != 1:
        sys.exit(f"{CONFIG_TEMPLATE.name} matches {pattern!r} {count} times, not once")
    return result


def configs():
    """Returns the full, no-WiFi and bare-minimum configs, made from the current template."""
    template = CONFIG_TEMPLATE.read_text()
    full = replace_once(template, r"\bPUMP_PIN\{\w+\}", "PUMP_PIN{GPIO_NUM_4}")
    no_wifi = replace_once(full, r"\bWIFI_ENABLED\{\w+\}", "WIFI_ENABLED{false}")
    minimal = replace_once(
        no_wifi,
        r"^inline constexpr std::array<MessagingTransport, \d+> MESSAGING\{.*?\};",
        "inline constexpr std::array<MessagingTransport, 0> MESSAGING{};",
    )
    minimal = replace_once(minimal, r"\bDASHBOARD_ENABLED\{\w+\}", "DASHBOARD_ENABLED{false}")
    minimal = replace_once(minimal, r"\bLED_ENABLED\{\w+\}", "LED_ENABLED{false}")
    return full, no_wifi, minimal


def builds(chips):
    full, no_wifi, minimal = configs()
    for chip in chips:
        if chip in NO_WIFI_CHIPS:
            yield Build(chip, "full", no_wifi, FULL_NO_WIFI_SYMBOLS)
            yield Build(chip, "min", minimal, MIN_SYMBOLS)
            yield Build(chip, "wifi-forced", full, None)
        else:
            yield Build(chip, "full", full, FULL_SYMBOLS)
            yield Build(chip, "min", minimal, MIN_SYMBOLS)


def project_name():
    cmake = (ROOT / "CMakeLists.txt").read_text()
    match = re.search(r"^project\((\w+)\)", cmake, re.MULTILINE)
    if not match:
        sys.exit("CMakeLists.txt names no project")
    return match[1]


def container_script(build, project):
    nm = "xtensa-esp-elf-nm" if build.chip in XTENSA_CHIPS else "riscv32-esp-elf-nm"
    idf = (
        f"idf.py -B build -D IDF_TARGET={build.chip}"
        " -D SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.prod' build"
    )
    symbols = "|".join(sorted(TRACKED_SYMBOLS))
    return "\n".join(
        [
            "set -euo pipefail",
            "mkdir /work",
            "tar -C /work --no-same-owner -xf -",
            "cd /work",
            f"echo {shlex.quote(f'$ {idf}')}",
            idf,
            f'echo "{RESULT_MARK} bin $(stat -c %s build/{project}.bin)"',
            f"{nm} -C build/{project}.elf | grep -oE {shlex.quote(symbols)} | sort -u"
            f" | sed 's/^/{RESULT_MARK} symbol /' || true",
        ]
    )


def feed(pipe, data):
    try:
        pipe.write(data)
        pipe.close()
    except BrokenPipeError:
        pass


def run_build(image, tree, build, project, verbose):
    """Runs one build in a fresh container and returns its exit code and output lines."""
    container = f"esp32-plant-watering-matrix-{os.getpid()}-{build.name}"
    command = ["docker", "run", "--rm", "-i", "--init", "--name", container, *DOCKER_OPTIONS]
    command += [image, "bash", "-c"]
    print(f"$ {shlex.join(command)} '<{build.chip} {build.variant}>'", flush=True)
    archive = with_config(tree, build.config)
    process = subprocess.Popen(
        [*command, container_script(build, project)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    threading.Thread(target=feed, args=(process.stdin, archive), daemon=True).start()
    lines = []
    try:
        for raw in process.stdout:
            line = raw.decode(errors="replace").rstrip("\n")
            plain = ANSI_ESCAPE.sub("", line)
            if verbose:
                print(line if COLOR else plain, flush=True)
            # Paths read as in the repo, not as in the container's copy.
            lines.append(plain.replace("/work/", ""))
    except KeyboardInterrupt:
        # By name, because the client can outlive its container after a Ctrl+C.
        subprocess.run(["docker", "rm", "-f", container], capture_output=True)
        process.kill()
        process.wait()
        raise
    return process.wait(), lines


def failure_excerpt(lines):
    """Returns what each failed step printed, or the end of the output when no step failed."""
    excerpt = []
    step = None
    for line in lines:
        if line.startswith("FAILED:"):
            excerpt.append(line)
            step = "command"
        elif step == "command":
            # The command line follows ninja's FAILED: line, and only the compiler's messages
            # after it say what went wrong.
            step = "messages"
        elif step == "messages":
            if NINJA_PROGRESS.match(line) or line.startswith("ninja: build stopped"):
                step = None
            else:
                excerpt.append(line)
    if not excerpt:
        excerpt = lines[-EXCERPT_LINES:]
    if len(excerpt) > EXCERPT_LINES:
        hidden = len(excerpt) - EXCERPT_LINES
        excerpt = [*excerpt[:EXCERPT_LINES], f"... {hidden} more lines; -v shows them"]
    return excerpt


def judge(build, code, lines, seconds, verbose):
    excerpt = () if verbose else tuple(failure_excerpt(lines))
    if build.symbols is None:
        if code != 0 and any(NO_WIFI_REFUSAL in line for line in lines):
            return Result("passed", seconds, "refused as expected")
        if code == 0:
            return Result("failed", seconds, "built, but must be refused")
        return Result("failed", seconds, "failed, but not on the missing radio", excerpt)
    if code != 0:
        return Result("failed", seconds, "build failed", excerpt)

    size = None
    found = set()
    for line in lines:
        words = line.split()
        if words[:2] == [RESULT_MARK, "bin"]:
            size = words[2]
        elif words[:2] == [RESULT_MARK, "symbol"]:
            found.add(words[2])
    missing = sorted(build.symbols - found)
    unexpected = sorted(found - build.symbols)
    if missing or unexpected:
        details = []
        if missing:
            details.append(f"missing: {' '.join(missing)}")
        if unexpected:
            details.append(f"should not be there: {' '.join(unexpected)}")
        return Result("failed", seconds, "wrong symbols compiled in", tuple(details))
    return Result("passed", seconds, f"{size} bytes")


def duration(seconds):
    minutes, seconds = divmod(round(seconds), 60)
    return f"{minutes}m {seconds}s" if minutes else f"{seconds}s"


def report(result):
    mark, color = OUTCOMES[result.outcome]
    print(paint(f"{mark} {duration(result.seconds)}  {result.note}", color))
    for line in result.details:
        print(f"  {line}")
    sys.stdout.flush()


def summarize(planned, results):
    width = max(len(build.name) for build in planned)
    print(f"\n{paint('== summary', '1')}")
    counts = dict.fromkeys(OUTCOMES, 0)
    for build in planned:
        result = results.get(build.name)
        outcome = result.outcome if result else "skipped"
        counts[outcome] += 1
        mark, color = OUTCOMES[outcome]
        took = duration(result.seconds) if result else ""
        note = result.note if result else "skipped"
        print(paint(f"{mark} {build.name:<{width}}  {took:>6}  {note}", color))
    print(", ".join(f"{count} {outcome}" for outcome, count in counts.items() if count))


def main():
    args = parse_args()
    check_docker()
    image = f"{IMAGE}:{args.idf}"
    if args.idf == DEFAULT_IDF:
        image += f"@{DEFAULT_IDF_DIGEST}"
    pull_image(image)
    tree = tree_archive()
    project = project_name()
    planned = list(builds(args.chips))

    results = {}
    for build in planned:
        print(paint(f"\n== {build.chip} {build.variant}", "1;36"), flush=True)
        started = time.monotonic()
        try:
            code, lines = run_build(image, tree, build, project, args.verbose)
        except KeyboardInterrupt:
            results[build.name] = Result("canceled", time.monotonic() - started, "canceled")
            report(results[build.name])
            break
        results[build.name] = judge(build, code, lines, time.monotonic() - started, args.verbose)
        report(results[build.name])

    summarize(planned, results)
    outcomes = {result.outcome for result in results.values()}
    if "canceled" in outcomes:
        sys.exit(130)
    sys.exit(1 if "failed" in outcomes else 0)


if __name__ == "__main__":
    main()
