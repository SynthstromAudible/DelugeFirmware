#! /usr/bin/env python3
import argparse
import importlib
import subprocess
import sys
from pathlib import Path
import os

# Based on dbt_tools/gdb.py by litui


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="gdb",
        description="Open a GDB remote for the proper target",
    )
    parser.group = "Debugging"
    parser.add_argument("-nb", "--no-build", action="store_true")
    parser.add_argument(
        "target",
        default="oled",
        const="oled",
        nargs="?",
        choices=["7seg", "oled"],
    )
    return parser


def main() -> int:
    args = argparser().parse_args()

    scons_target = f"dbt-build-debug-{args.target}"

    if not args.no_build:
        sys.argv = ["build", scons_target]
        importlib.import_module("task-build").main()

    elf_path = Path(scons_target) / f"Deluge-debug-{args.target}.elf"

    gdbinit = Path(os.environ["DBT_DEBUG_DIR"]) / "gdbinit"

    result = subprocess.run(
        [
            "arm-none-eabi-gdb",
            "-ex",
            f"source {str(gdbinit)}",
            "-ex",
            "target extended-remote :3333",
            elf_path,
        ]
    )
    return result.returncode


if __name__ == "__main__":
    main()
