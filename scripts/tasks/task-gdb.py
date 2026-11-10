#! /usr/bin/env python3
import argparse
import importlib
import os
import subprocess
import sys
from pathlib import Path

import util

# Based on dbt_tools/gdb.py by litui


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="gdb",
        description="Build firmware and connect GDB to a debug server on localhost:3333",
        epilog="Start the debug server separately with dbt debug before connecting.",
    )
    parser.group = "Debugging"
    parser.add_argument(
        "-nb", "--no-build", action="store_true", help="Use an existing CMake build"
    )
    parser.add_argument(
        "config",
        default="debug",
        nargs="?",
        type=str.lower,
        choices=["debug", "release", "relwithdebinfo"],
        help="Build configuration (default: debug)",
    )
    return parser


def main() -> int:
    args = argparser().parse_args()

    build = importlib.import_module("task-build")
    config = build.BUILD_CONFIGS[args.config]
    os.chdir(util.get_git_root())

    if not args.no_build:
        result = build.main([args.config])
        if result != 0:
            return result

    # CMake records the target path, including any configured version suffix.
    elf_manifest = Path("build") / config / "deluge-elf-path.txt"
    try:
        elf_path = Path(elf_manifest.read_text().strip())
        if not elf_path.is_file():
            raise FileNotFoundError(f"Firmware ELF not found: {elf_path}")
    except OSError as error:
        print(
            f"Unable to locate firmware: {error}\n"
            f"Run './dbt build {args.config}' before debugging.",
            file=sys.stderr,
        )
        return 1

    gdbinit = Path(os.environ["DBT_DEBUG_DIR"]) / "gdbinit"

    result = subprocess.run(
        [
            "arm-none-eabi-gdb",
            "-ex",
            f"source {gdbinit!s}",
            "-ex",
            "target extended-remote :3333",
            elf_path,
        ],
        check=False,
    )
    return result.returncode


if __name__ == "__main__":
    main()
