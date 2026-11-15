#!/usr/bin/env python3
"""Builds, flashes and monitors the firmware for one profile and chip.

usage: scripts/firmware.py [dev|prod] [CHIP] COMMAND... [-p PORT]

  dev | prod   build profile (default: prod)
  CHIP         ESP-IDF target, e.g. esp32s3 (default: CONFIG_IDF_TARGET in sdkconfig.defaults)
  COMMAND      one or more of the commands below

commands:
  build        Compiles the firmware.
                 idf.py -B <dir> -D IDF_TARGET=<chip> -D SDKCONFIG_DEFAULTS=<files> build
  size         Builds if needed, then shows the flash and RAM the firmware takes.
                 idf.py -B <dir> ... size
  size-components, size-files
               The same, broken down by component or by source file.
                 idf.py -B <dir> ... size-components
  flash        Builds if needed, then writes the firmware to the device and clears its core dump.
                 idf.py -B <dir> ... flash
  monitor      Shows the serial log and decodes crash backtraces. Ctrl+] exits.
                 idf.py -B <dir> ... monitor
  coredump     Reads the core dump off the device and decodes it against this build.
                 parttool.py read_partition --partition-name=coredump --output=<file>
                 idf.py -B <dir> ... coredump-info --core <file>
  erase-nvs    Erases stored settings and WiFi credentials. The firmware stays.
                 parttool.py erase_partition --partition-name=nvs
  erase-flash  Erases the whole flash: firmware, stored settings, WiFi credentials and core dump.
               The device runs nothing until the next flash.
                 idf.py -B <dir> ... erase-flash
  clean        Deletes this profile and chip's build directory.
                 rm -rf <dir>

<dir> is build/<chip>-<profile>. <files> is "sdkconfig.defaults;sdkconfig.<profile>".
The script prints every command before running it, and after a command that builds, the firmware's
build id, which telemetry reports as device.firmware.build. The ESP-IDF environment must be sourced
first.

examples:
  scripts/firmware.py flash
  scripts/firmware.py dev flash monitor
  scripts/firmware.py esp32s3 build
  scripts/firmware.py size-components
  scripts/firmware.py dev coredump
  scripts/firmware.py erase-nvs -p /dev/cu.usbmodem1101
  scripts/firmware.py erase-flash flash
"""

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROFILES = ("dev", "prod")
IDF_ACTIONS = ("build", "size", "size-components", "size-files", "flash", "monitor", "erase-flash")
BUILDING_ACTIONS = ("build", "size", "size-components", "size-files", "flash")
COMMANDS = (*IDF_ACTIONS, "coredump", "erase-nvs", "clean")

# The first word of a core dump is its length, and a blank partition reads all ones there.
BLANK_CORE_DUMP = b"\xff\xff\xff\xff"
# The ELF note in which the crashed firmware records its ELF's SHA-256, as text
CORE_DUMP_INFO_NOTE = b"ESP_CORE_DUMP_INFO\0"


def parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        usage=argparse.SUPPRESS,
    )
    parser.add_argument("words", nargs="*", help=argparse.SUPPRESS)
    parser.add_argument(
        "-p", "--port", default=os.environ.get("ESPPORT"), help="serial port (default: $ESPPORT)"
    )
    args = parser.parse_intermixed_args()

    args.profile = "prod"
    args.chip = None
    args.commands = []
    for word in args.words:
        if word in PROFILES:
            args.profile = word
        elif word in COMMANDS:
            args.commands.append(word)
        elif re.fullmatch(r"esp32\w*", word):
            args.chip = word
        else:
            parser.error(f"unknown word '{word}'")
    if not args.commands:
        parser.error(f"name at least one command: {', '.join(COMMANDS)}")
    return args


def default_chip():
    defaults = (ROOT / "sdkconfig.defaults").read_text()
    match = re.search(r'^CONFIG_IDF_TARGET="(\w+)"', defaults, re.MULTILINE)
    if not match:
        sys.exit("sdkconfig.defaults sets no CONFIG_IDF_TARGET; name the chip")
    return match[1]


def defaults_files(profile, chip):
    """Returns every defaults file ESP-IDF applies for this profile and chip."""
    names = []
    for base in ("sdkconfig.defaults", f"sdkconfig.{profile}"):
        names += [base, f"{base}.{chip}"]
    return [ROOT / name for name in names if (ROOT / name).exists()]


def run(shown, command):
    print(f"$ {shlex.join(shown)}", flush=True)
    if subprocess.run(command).returncode != 0:
        sys.exit(1)


def firmware_build(build_dir):
    """Returns the build id the firmware reports: the leading hex digits of its ELF's SHA-256."""
    elf = build_dir / json.loads((build_dir / "project_description.json").read_text())["app_elf"]
    sdkconfig = (build_dir / "sdkconfig").read_text()
    length = re.search(r"^CONFIG_APP_RETRIEVE_LEN_ELF_SHA=(\d+)$", sdkconfig, re.MULTILINE)
    return hashlib.sha256(elf.read_bytes()).hexdigest()[: int(length[1])]


def core_dump_elf_sha(dump):
    """Returns the leading hex digits of the crashed firmware's ELF SHA-256, or None."""
    name = dump.find(CORE_DUMP_INFO_NOTE)
    if name < 0:
        return None
    # After the note's name, padded to 4 bytes, come a version word and then the text.
    sha = name + (len(CORE_DUMP_INFO_NOTE) + 3) // 4 * 4 + 4
    return dump[sha : dump.find(b"\0", sha)].decode("ascii", "replace") or None


def show_core_dump(idf_path, idf_args, build_dir, port):
    description = build_dir / "project_description.json"
    if not description.exists():
        sys.exit(f"{build_dir} holds no build; build and flash this profile and chip first")
    elf = build_dir / json.loads(description.read_text())["app_elf"]

    with tempfile.TemporaryDirectory() as tmp:
        dump_file = Path(tmp) / "coredump.bin"
        parttool_args = ["--port", port] if port else []
        parttool_args += ["read_partition", "--partition-name=coredump", f"--output={dump_file}"]
        parttool = f"{idf_path}/components/partition_table/parttool.py"
        run(["parttool.py", *parttool_args], [sys.executable, parttool, *parttool_args])

        dump = dump_file.read_bytes()
        if dump.startswith(BLANK_CORE_DUMP):
            print("No core dump on the device.")
            return
        crashed = core_dump_elf_sha(dump)
        if crashed is None:
            sys.exit("The coredump partition holds no readable dump. The next flash clears it.")
        # Decoded against any other ELF, the backtrace would point at the wrong code.
        built = hashlib.sha256(elf.read_bytes()).hexdigest()
        if not built.startswith(crashed):
            sys.exit(
                f"The dump is from firmware {crashed}, but {elf} is {built[: len(crashed)]}. "
                "Decode it with the profile, chip, source and config it was flashed with."
            )

        length = int.from_bytes(dump[:4], "little")
        print(f"Core dump of {length} bytes, in a partition of {len(dump)}")
        # A GDB reply later than esp-coredump's 3 s default is read as the answer to the next
        # question, which garbles the thread table. The wait ends as soon as the reply arrives.
        actions = ["coredump-info", "--core", str(dump_file), "--gdb-timeout-sec", "10"]
        run(
            ["idf.py", *idf_args, *actions],
            [sys.executable, f"{idf_path}/tools/idf.py", *idf_args, *actions],
        )


def main():
    args = parse_args()
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        sys.exit("ESP-IDF is not set up in this shell; source its export.sh first")

    os.chdir(ROOT)
    chip = args.chip or default_chip()
    build_dir = Path("build") / f"{chip}-{args.profile}"
    idf_args = ["-B", str(build_dir), "-D", f"IDF_TARGET={chip}"]
    idf_args += ["-D", f"SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.{args.profile}"]
    if args.port:
        idf_args += ["-p", args.port]

    # Defaults only fill options an sdkconfig lacks. An edited defaults file needs a fresh one.
    sdkconfig = build_dir / "sdkconfig"
    reconfigure = sdkconfig.exists() and any(
        f.stat().st_mtime > sdkconfig.stat().st_mtime for f in defaults_files(args.profile, chip)
    )
    if reconfigure:
        print(f"Defaults changed since {sdkconfig} was generated; regenerating it")
        sdkconfig.unlink()

    steps = []
    for command in args.commands:
        if command in IDF_ACTIONS and steps and steps[-1][0] in IDF_ACTIONS:
            steps[-1].append(command)
        else:
            steps.append([command])

    for step in steps:
        if step[0] in IDF_ACTIONS:
            actions = ["reconfigure", *step] if reconfigure else step
            reconfigure = False
            run(
                ["idf.py", *idf_args, *actions],
                [sys.executable, f"{idf_path}/tools/idf.py", *idf_args, *actions],
            )
            # The editor's compile database follows the last build.
            if (build_dir / "compile_commands.json").exists():
                shutil.copyfile(
                    build_dir / "compile_commands.json", "build/compile_commands.json"
                )
            if any(action in BUILDING_ACTIONS for action in step):
                print(f"Firmware build {firmware_build(build_dir)}")
        elif step[0] == "coredump":
            show_core_dump(idf_path, idf_args, build_dir, args.port)
        elif step[0] == "erase-nvs":
            parttool_args = ["--port", args.port] if args.port else []
            parttool_args += ["erase_partition", "--partition-name=nvs"]
            parttool = f"{idf_path}/components/partition_table/parttool.py"
            run(["parttool.py", *parttool_args], [sys.executable, parttool, *parttool_args])
        elif step[0] == "clean":
            print(f"$ rm -rf {build_dir}", flush=True)
            if build_dir.exists():
                shutil.rmtree(build_dir)


if __name__ == "__main__":
    main()
