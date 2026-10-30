#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""clang-tidy on the lines a commit changes, in the libraries that hold themselves to the house style.

The libraries (dsp-next, synth, effects, the Deluge instrument, plugin-api, serialization) are checked with the
.clang-tidy that governs each file: the repo root's, plus the library's own stricter one where it has one. Only
the staged lines are checked, so legacy code is cleaned as it is touched rather than all at once.

Each file is checked on its own: a source file with its library's compile flags (the libraries are unity-built,
so their sources are not compile-database entries), a header as a C++ header with the same flags, which also
holds every header to compiling standalone. Any finding fails the check.

Needs build/compile_commands.json (`./dbt configure`). The pre-commit hook installs the pinned clang-tidy;
run by hand, the clang-tidy on PATH must be that version.

Usage:
    scripts/check_tidy.py FILE...           # check the staged lines of FILE...
    scripts/check_tidy.py --all-lines FILE...  # check every line
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DATABASE = REPO / "build" / "compile_commands.json"
TIDY_VERSION = "23.1.4"

# Library source roots, each with a path fragment that picks its compile flags out of the database.
LIBRARIES = {
    "src/deluge/dsp-next/": "/dsp-next/",
    "src/synth/": "/synth/",
    "src/effects/": "/src/effects/",
    "src/instruments/deluge/": "/instruments/deluge/",
    "src/plugin-api/": "/dsp-next/",  # header-only: any library's flags see it
    "src/serialization/": "/serialization/",
}
SOURCE_SUFFIXES = {".cpp", ".cc", ".cxx"}
HEADER_SUFFIXES = {".h", ".hpp", ".hh"}
# Flags the device database carries that clang's front end does not accept.
DROPPED_FLAGS = {"-mthumb-interwork", "-fdevirtualize-at-ltrans", "-fuse-linker-plugin"}
FINDING = re.compile(r"^(?P<path>[^:]+):(?P<line>\d+):\d+: (?:warning|error): ")
HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def library_of(path: str) -> str | None:
    return next((root for root in LIBRARIES if path.startswith(root)), None)


def flags_for(database: list[dict], library: str) -> tuple[str, list[str]]:
    """The directory and arguments of one of @p library's compile commands, minus the input and output."""
    fragment = LIBRARIES[library]
    entry = next(
        (
            e
            for e in database
            if fragment in e["file"] or fragment in e.get("output", "")
        ),
        None,
    )
    if entry is None:
        entry = next(e for e in database if "/dsp-next/" in e["file"])
    args = entry.get("arguments") or shlex.split(entry["command"])
    kept, skip = [], False
    for arg in args[1:]:
        if skip:
            skip = False
            continue
        if arg in ("-o", "-c", "-MF", "-MT", "-MQ"):
            skip = arg != "-c"
            continue
        if (
            arg in DROPPED_FLAGS
            or arg == entry["file"]
            or arg.endswith((".cxx", ".cpp", ".cc"))
        ):
            continue
        kept.append(arg)
    return entry["directory"], kept


def staged_lines(path: str) -> list[list[int]]:
    """The staged hunks' added and changed line ranges of @p path, as [first, last] pairs."""
    diff = subprocess.run(
        ["git", "diff", "--cached", "--unified=0", "--no-color", "--", path],
        cwd=REPO,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    ranges = []
    for line in diff.splitlines():
        match = HUNK.match(line)
        if match:
            first, count = int(match.group(1)), int(match.group(2) or "1")
            if count:
                ranges.append([first, first + count - 1])
    return ranges


def check(
    path: str, directory: str, flags: list[str], ranges: list[list[int]] | None
) -> list[str]:
    absolute = str(REPO / path)
    command = ["clang-tidy", "--quiet", f"--header-filter=^{re.escape(absolute)}$"]
    if ranges is not None:
        command.append(
            "--line-filter=" + json.dumps([{"name": absolute, "lines": ranges}])
        )
    command.append(absolute)
    command.append("--")
    if Path(path).suffix in HEADER_SUFFIXES:
        command += ["-x", "c++-header"]
    command += flags
    result = subprocess.run(
        command, cwd=directory, capture_output=True, text=True, check=False
    )
    findings, keep = [], False
    for line in (result.stdout + result.stderr).splitlines():
        match = FINDING.match(line)
        if match:
            keep = os.path.realpath(match.group("path")) == os.path.realpath(absolute)
        if keep and not line.endswith(" generated."):
            findings.append(line)
    return findings


def tidy_version() -> str:
    try:
        out = subprocess.run(
            ["clang-tidy", "--version"], capture_output=True, text=True, check=True
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return ""
    match = re.search(r"version (\d+\.\d+\.\d+)", out)
    return match.group(1) if match else ""


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("files", nargs="*")
    parser.add_argument(
        "--all-lines",
        action="store_true",
        help="check every line, not only the staged ones",
    )
    options = parser.parse_args()

    files = [
        f
        for f in options.files
        if library_of(f)
        and Path(f).suffix in SOURCE_SUFFIXES | HEADER_SUFFIXES
        and (REPO / f).exists()
    ]
    if not files:
        return 0
    if not DATABASE.exists():
        print(
            "check_tidy: build/compile_commands.json is missing; run ./dbt configure",
            file=sys.stderr,
        )
        return 1
    version = tidy_version()
    if version != TIDY_VERSION:
        print(
            f"check_tidy: clang-tidy {TIDY_VERSION} is required (found {version or 'none'})",
            file=sys.stderr,
        )
        return 1

    with open(DATABASE) as f:
        database = json.load(f)
    jobs = []
    for path in files:
        ranges = None if options.all_lines else staged_lines(path)
        if ranges == []:
            continue
        directory, flags = flags_for(database, library_of(path))
        jobs.append((path, directory, flags, ranges))

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results = list(pool.map(lambda job: check(*job), jobs))

    findings = [line for result in results for line in result]
    if findings:
        print("\n".join(findings))
        print(
            f"\ncheck_tidy: {sum(1 for l in findings if FINDING.match(l))} finding(s)",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
