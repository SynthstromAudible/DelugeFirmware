#! /usr/bin/env python3
"""dbt gdb — attach GDB to the device through the server `dbt debug` runs.

Builds the device image (unless --no-build) and opens arm-none-eabi-gdb on the ELF `dbt build` stages at
build/<Config>/deluge.elf, connected to the debug server on port 3333. It attaches to the running firmware; it does not
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
    # The image path and entry point, for gdbinit's `deluge-load` (the OpenOCD server loads the ELF itself,
    # over the AHB-AP, which is faster than GDB's `load`). GDB has no Python here to look them up itself.
    image_vars = []
    entry = elf_entry(elf_path)
    if entry is not None:
        image_vars = [
            "-ex",
            f'set $deluge_elf = "{elf_path.resolve().as_posix()}"',
            "-ex",
            f"set $deluge_entry = {entry:#x}",
        ]
    return subprocess.run(
        [
            "arm-none-eabi-gdb",
            "-ex",
            f"source {gdbinit!s}",
            *image_vars,
            "-ex",
            f"target extended-remote :{args.gdb_port}",
            str(elf_path),
        ],
        check=False,
    ).returncode


def elf_entry(elf_path: Path) -> int | None:
    """The ELF header's entry point (e_entry), or None if the file isn't a 32-bit little-endian ELF."""
    with open(elf_path, "rb") as f:
        header = f.read(0x1C)
    if (
        len(header) < 0x1C
        or header[:4] != b"\x7fELF"
        or header[4] != 1
        or header[5] != 1
    ):
        return None
    return int.from_bytes(header[0x18:0x1C], "little")


if __name__ == "__main__":
    sys.exit(main())
