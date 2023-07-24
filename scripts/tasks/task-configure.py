#! /usr/bin/env python3
import argparse
import subprocess
import sys
import util
import os


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="configure",
        description="Configure a CMake build (automatically called by build)",
    )
    parser.group = "Building"
    return parser


def main() -> int:
    (args, unknown_args) = argparser().parse_known_args()
    os.chdir(util.get_git_root())

    configure_args = [
        "-B",
        "build",
        "-G",
        "Ninja Multi-Config",
        "-DCMAKE_CROSS_CONFIGS:STRING=all",
        "-DCMAKE_DEFAULT_CONFIGS=Debug;Release",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE",
    ]
    result = subprocess.run(["cmake"] + configure_args, env=os.environ)
    return result.returncode


if __name__ == "__main__":
    main()
