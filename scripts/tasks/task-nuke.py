#! /usr/bin/env python3
import argparse
from pathlib import Path
import shutil
import util
import os

def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="nuke",
        description="Indiscriminately removes any build trees",
    )
    parser.group = "Building"
    return parser


def main() -> int:
    os.chdir(util.get_git_root())

    build_dirs = Path('.').glob('*build*')
    for dir in build_dirs:
      print(f"Removing {dir}")
      shutil.rmtree(dir, ignore_errors=True)

    return 0

if __name__ == "__main__":
    main()
