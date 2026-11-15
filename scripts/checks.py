#!/usr/bin/env python3
"""Runs the CI checks in containers with the tools CI uses.

usage: scripts/checks.py [JOB...]

  JOB          one or more of the jobs below (default: all of them, in this order)

jobs:
  format       Checks that every source is formatted as .clang-format says.
  test         Builds and runs the host tests.
  sanitize     Runs the host tests under AddressSanitizer and UndefinedBehaviorSanitizer.
  fuzz         Fuzzes each parser that reads network input for a minute.
  tidy         Runs clang-tidy over every source the host tests compile.

Each job runs CI's commands in a fresh container, as each CI job runs on a fresh runner, and the
run stops at the first job that fails. A summary of every job closes the run.

The tree is copied in as it stands when the run starts: the tracked files and new ones not yet
added, as a commit would leave them, but nothing gitignored. Nothing is written back. The first
run builds the image from scripts/checks/Dockerfile, as does the first run after it changes.
The script prints every command it runs. It needs Docker and git.

examples:
  scripts/checks.py
  scripts/checks.py format tidy
"""

import argparse
import hashlib
import io
import os
import shlex
import shutil
import subprocess
import sys
import tarfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCKERFILE = ROOT / "scripts" / "checks" / "Dockerfile"
IMAGE = "esp32-plant-watering-checks"

FUZZ_TARGETS = "fuzz_command fuzz_login fuzz_cookie fuzz_bearer fuzz_media_type"

# The commands of each job in .github/workflows/ci.yml.
JOBS = {
    "format": [
        "clang-format --version",
        "git ls-files '*.cpp' '*.hpp' '*.h' ':!test/host/third_party'"
        " | xargs clang-format --dry-run --Werror",
    ],
    "test": [
        "export CXX=g++-14",
        "cmake -S test/host -B test/host/build -DCMAKE_COMPILE_WARNING_AS_ERROR=ON",
        "cmake --build test/host/build",
        "ctest --test-dir test/host/build --output-on-failure",
    ],
    "sanitize": [
        "export CXX=g++-14 CC=gcc-14",
        "export ASAN_OPTIONS=detect_stack_use_after_return=1:strict_string_checks=1",
        "export UBSAN_OPTIONS=print_stacktrace=1",
        "cmake -S test/host -B test/host/build -DSANITIZE=ON -DCMAKE_COMPILE_WARNING_AS_ERROR=ON",
        "cmake --build test/host/build",
        "ctest --test-dir test/host/build --output-on-failure",
    ],
    "fuzz": [
        "export CC=clang-20 CXX=clang++-20",
        "cmake -S test/host -B test/host/build -DFUZZ=ON",
        f"cmake --build test/host/build --target {FUZZ_TARGETS}",
        "mkdir -p findings",
        # A finding is printed for replay instead of kept, since nothing leaves the container.
        f"for target in {FUZZ_TARGETS}; do"
        " test/host/build/$target -max_total_time=60 -dict=test/host/fuzz.dict"
        " -seed_inputs=test/host/fuzz_seeds/$target -artifact_prefix=findings/$target-"
        " || { echo 'Findings, as base64. See docs/testing.md to replay one.';"
        ' for f in findings/*; do echo "$f"; base64 -w0 "$f"; echo; done; exit 1; };'
        " done",
    ],
    "tidy": [
        "export CXX=g++-14",
        "cmake -S test/host -B test/host/build",
        "jq -r '.[].file' test/host/build/compile_commands.json"
        " | grep -v third_party | sort -u"
        " | xargs -P \"$(nproc)\" -n 4 clang-tidy -p test/host/build -quiet"
        " --warnings-as-errors='*'",
    ],
}

# Mark and ANSI color of each outcome in the summary
OUTCOMES = {
    "passed": ("✓", "32"),
    "failed": ("✗", "31"),
    "canceled": ("⊘", "33"),
    "skipped": ("-", "90"),
}

COLOR = sys.stdout.isatty() and "NO_COLOR" not in os.environ


def paint(text, color):
    return f"\033[{color}m{text}\033[0m" if COLOR else text


def parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        usage=argparse.SUPPRESS,
    )
    parser.add_argument("jobs", nargs="*", help=argparse.SUPPRESS)
    args = parser.parse_args()
    for job in args.jobs:
        if job not in JOBS:
            parser.error(f"unknown job '{job}'; pick from: {', '.join(JOBS)}")
    args.jobs = list(dict.fromkeys(args.jobs)) or list(JOBS)
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


def image_tag():
    """Builds the image unless one from this Dockerfile exists, and removes older ones."""
    digest = hashlib.sha256(DOCKERFILE.read_bytes()).hexdigest()[:12]
    tag = f"{IMAGE}:{digest}"
    exists = subprocess.run(["docker", "image", "inspect", tag], capture_output=True)
    if exists.returncode != 0:
        run(["docker", "build", "-t", tag, str(DOCKERFILE.parent)])
    listed = subprocess.run(
        ["docker", "images", "--format", "{{.Repository}}:{{.Tag}}", IMAGE],
        capture_output=True,
        text=True,
        check=True,
    )
    stale = [name for name in listed.stdout.split() if name != tag]
    if stale:
        run(["docker", "image", "rm", *stale])
    return tag


def tree_archive():
    """Packs what a commit would leave in the tree, plus .git for the format job's file list."""
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
        archive.add(ROOT / ".git", arcname=".git")
    return buffer.getvalue()


def container_script(job):
    lines = [
        "set -euo pipefail",
        'export CMAKE_BUILD_PARALLEL_LEVEL="$(nproc)"',
        "mkdir /work",
        "tar -C /work --no-same-owner -xf -",
        "cd /work",
        # Stages the copy's new and deleted files in its own index, which leaves git's file
        # list as the commit would.
        "git add -A",
    ]
    for command in JOBS[job]:
        lines += [f"echo {shlex.quote(f'$ {command}')}", command]
    return "\n".join(lines)


def run_job(tag, tree, job):
    """Runs one job in a fresh container and returns whether it passed."""
    command = ["docker", "run", "--rm", "-i", "--init", tag, "bash", "-c"]
    print(f"$ {shlex.join(command)} '<{job}>'", flush=True)
    result = subprocess.run([*command, container_script(job)], input=tree)
    return result.returncode == 0


def duration(seconds):
    minutes, seconds = divmod(round(seconds), 60)
    return f"{minutes}m {seconds}s" if minutes else f"{seconds}s"


def summarize(jobs, outcomes):
    width = max(len(job) for job in jobs)
    print(f"\n{paint('== summary', '1')}")
    for job in jobs:
        outcome, seconds = outcomes.get(job, ("skipped", None))
        mark, color = OUTCOMES[outcome]
        took = duration(seconds) if seconds is not None else outcome
        print(paint(f"{mark} {job:<{width}}  {took}", color))


def main():
    args = parse_args()
    check_docker()
    tag = image_tag()
    tree = tree_archive()

    outcomes = {}
    for job in args.jobs:
        print(paint(f"\n== {job}", "1;36"), flush=True)
        started = time.monotonic()
        try:
            passed = run_job(tag, tree, job)
        except KeyboardInterrupt:
            outcomes[job] = ("canceled", time.monotonic() - started)
            break
        outcomes[job] = ("passed" if passed else "failed", time.monotonic() - started)
        if not passed:
            break

    summarize(args.jobs, outcomes)
    # Only the last job run can have done anything but pass.
    last, _ = outcomes[args.jobs[len(outcomes) - 1]]
    sys.exit({"passed": 0, "failed": 1, "canceled": 130}[last])


if __name__ == "__main__":
    main()
