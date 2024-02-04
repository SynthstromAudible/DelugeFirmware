#! /usr/bin/env python3
import argparse
import os
import subprocess
import sys
import util


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="test", description="Run deluge tests")

    return parser


def cmake_build() -> int:
    cmake_args = ["cmake"]
    cmake_args += ["--build", "build/tests/"]

    return subprocess.run(cmake_args, env=os.environ).returncode


def cmake_configure() -> int:
    cmake_args = ["cmake"]
    cmake_args += ["-S", "tests/"]
    cmake_args += ["-B", "build/tests"]
    cmake_args += ["-G", "Ninja Multi-Config"]  # generator

    return subprocess.run(cmake_args, env=os.environ).returncode


def main() -> int:
    (args, unknown_args) = argparser().parse_known_args()

    os.chdir(util.get_git_root())

    if not os.path.exists("build/tests"):
        result = cmake_configure()
        if result != 0:
            return result

    return cmake_build()


if __name__ == "__main__":
    sys.exit(main())
