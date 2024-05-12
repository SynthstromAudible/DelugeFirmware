#! /usr/bin/env python3
import argparse
import os
import subprocess
import sys
import util


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="test", description="Run deluge tests")
    parser.add_argument(
        "-n",
        "--no-run",
        action="store_true",
        help="Only build the tests, without running them.",
    )

    return parser


def cmake_build() -> int:
    cmake_args = ["cmake"]
    cmake_args += ["--build", "build/tests/"]

    return subprocess.run(cmake_args, env=os.environ).returncode


def cmake_test() -> int:
    cmake_args = ["ctest"]
    cmake_args += ["--test-dir", "build/tests", "-C", "Debug"]

    return subprocess.run(
        cmake_args, env=dict(os.environ, CTEST_OUTPUT_ON_FAILURE="1")
    ).returncode


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

    build = cmake_build()
    if build != 0 or args.no_run:
        return build

    return cmake_test()


if __name__ == "__main__":
    sys.exit(main())
