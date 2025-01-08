#! /usr/bin/env python3
import argparse
import importlib
import shutil
import subprocess
import sys
import util
import os


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="sizediff",
        description="Get a size difference from the current HEAD and where the branch forked from community",
    )
    parser.group = "Development"
    return parser


def main() -> int:
    (args, unknown_args) = argparser().parse_known_args()

    os.chdir(util.get_git_root())
    os.makedirs(".cache/sizediff", exist_ok=True)

    # Get the commit hash of the community branch
    old_hash = util.run_get_output(["git", "merge-base", "--fork-point", "community"])

    # get the current commit hash
    new_hash = util.run_get_output(["git", "rev-parse", "HEAD"])

    # get if git is dirty
    git_dirty = (
        subprocess.run(["git", "diff", "--quiet"], capture_output=True).returncode != 0
    )

    # Read the current commit hash in .cache/sizediff/old-commit
    needs_old_rebuild = True
    try:
        with open(".cache/sizediff/old-commit", "r") as f:
            old_commit = f.read().strip()
            if old_commit == old_hash:
                needs_old_rebuild = False
    except FileNotFoundError:
        pass

    needs_new_rebuild = True
    try:
        with open(".cache/sizediff/new-commit", "r") as f:
            new_commit = f.read().strip()
            if new_commit == new_hash and not git_dirty:
                needs_new_rebuild = False
    except FileNotFoundError:
        pass

    if needs_old_rebuild and git_dirty:
        print(
            "Cannot build old commit because you have uncommitted changes, please commit or stash them"
        )
        return 1

    if needs_new_rebuild:
        importlib.import_module("task-build").main(["Release"])
        with open(".cache/sizediff/new-commit", "w") as f:
            f.write(new_hash)
        shutil.copy("build/Release/deluge.elf", ".cache/sizediff/deluge_new.elf")

    if needs_old_rebuild:
        subprocess.run(["git", "checkout", old_hash])
        importlib.import_module("task-build").main(["Release"])
        with open(".cache/sizediff/old-commit", "w") as f:
            f.write(old_hash)
        shutil.copy("build/Release/deluge.elf", ".cache/sizediff/deluge_old.elf")
        subprocess.run(["git", "checkout", new_hash])

    # Get the size of the old and new binaries using the size command
    old_size = (
        util.run_get_output(["arm-none-eabi-size", ".cache/sizediff/deluge_old.elf"])
        .split("\n")[1]
        .split("\t")[3]
        .strip()
    )
    new_size = (
        util.run_get_output(["arm-none-eabi-size", ".cache/sizediff/deluge_new.elf"])
        .split("\n")[1]
        .split("\t")[3]
        .strip()
    )

    print(f"Size difference: {int(new_size) - int(old_size):+} bytes")


if __name__ == "__main__":
    main()
