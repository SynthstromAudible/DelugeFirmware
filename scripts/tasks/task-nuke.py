#! /usr/bin/env python3
import argparse
from pathlib import Path
import shutil
import util
import os
import stat


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="nuke",
        description="Indiscriminately removes any build tree contents and artifacts",
    )
    parser.group = "Building"
    return parser


def readonly_handler(func, path, execinfo):
    # shutil.rmtree can fail after building tests, due to a cpputest's git checkout
    # under build tree containing read-only files.
    os.chmod(path, stat.S_IWRITE)
    func(path)


def main() -> int:
    build_dirs = util.get_git_root().glob("*build*")
    for dir in build_dirs:
        print(f"Removing {dir}", end="")
        try:
            shutil.rmtree(dir, onerror=readonly_handler)
            print(" - ok!")
        except Exception:
            print(" - failed!")
    return 0


if __name__ == "__main__":
    main()
