#! /usr/bin/env python3
"""dbt gdb — attach GDB to the device through the server `dbt debug` runs.

Builds the device image (unless --no-build) and opens arm-none-eabi-gdb on the ELF `dbt build` stages at
build/<Config>/deluge.elf, connected to the debug server on :3333. It attaches to the running firmware; it does not
load an image.
"""

import argparse
import importlib
import os
import subprocess
import sys
from pathlib import Path

import util

# Debug does not link a device image (it overflows RAM); RelWithDebInfo is the optimised image with symbols.
CONFIGS = {"relwithdebinfo": "RelWithDebInfo", "release": "Release"}


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="gdb",
        description="Attach GDB to the device through the `dbt debug` server",
    )
    parser.group = "Debugging"
    parser.add_argument("-nb", "--no-build", action="store_true")
    parser.add_argument(
        "config",
        default="relwithdebinfo",
        nargs="?",
        choices=list(CONFIGS.keys()),
    )
    parser.add_argument(
        "-g",
        "--gdb-port",
        help="The port the debug server listens on",
        default=3333,
        type=int,
    )
    return parser


def main() -> int:
    args = argparser().parse_args()
    root = Path(util.get_git_root())

    if not args.no_build:
        rc = importlib.import_module("task-build").main([args.config])
        if rc:
            return rc

    elf_path = root / "build" / CONFIGS[args.config] / "deluge.elf"
    if not elf_path.is_file():
        util.note(f"ERROR: no image at {elf_path}. Drop --no-build.")
        return 1

    gdbinit = Path(os.environ["DBT_DEBUG_DIR"]) / "gdbinit"
    return subprocess.run(
        [
            "arm-none-eabi-gdb",
            "-ex",
            f"source {gdbinit!s}",
            "-ex",
            f"target extended-remote :{args.gdb_port}",
            str(elf_path),
        ],
        check=False,
    ).returncode


if __name__ == "__main__":
    sys.exit(main())
