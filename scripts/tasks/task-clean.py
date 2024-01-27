#! /usr/bin/env python3
import argparse
import subprocess
import sys
import util
import os


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="clean",
        description="Cleanup any build artifacts in the current environment",
    )
    parser.group = "Building"
    return parser


def main() -> int:
    os.chdir(util.get_git_root())

    if not os.path.exists("build"):
        print("Already clean!")
        exit()

    cmake_args = [
        "--build",
        "build",
        "--target",
        "clean",
    ]
    result = subprocess.run(["cmake"] + cmake_args, env=os.environ)
    return result.returncode


if __name__ == "__main__":
    main()
