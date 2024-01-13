#! /usr/bin/env python3

# Copyright 2023 Kate Whitlock
import argparse
from pathlib import Path
import shutil

VERSION = Path('./toolchain/REQUIRED_VERSION').read_text().strip()

def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="envclean",
        description="Remove extraneous toolchain envs",
    )
    parser.group = "Miscellaneous"
    parser.add_argument(
        "-d", "--dry-run", help="don't actually remove anything", action="store_true"
    )
    return parser


def main() -> int:
    args = argparser().parse_args()
    toolchains = Path.cwd().glob('toolchain/v*/')
    toolchains_to_delete = [path for path in toolchains if not str(path).endswith(VERSION)]
    for toolchain in toolchains_to_delete:
        print(f"Removing {toolchain.name}...")
        if not args.dry_run:
          shutil.rmtree(toolchain)


if __name__ == "__main__":
    main()
