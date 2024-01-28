#! /usr/bin/env python3
import argparse
from pathlib import Path
import shutil
import util
import os


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="nuke",
        description="Indiscriminately removes any build tree contents and artifacts",
    )
    parser.group = "Building"
    return parser


def main() -> int:
    build_dirs = util.get_git_root().glob("*build*")
    for dir in build_dirs:
        print(f"Removing {dir}")
        shutil.rmtree(dir, ignore_errors=True)

    return 0


if __name__ == "__main__":
    main()
