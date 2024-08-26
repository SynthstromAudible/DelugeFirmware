#! /usr/bin/env python3
import argparse
import importlib
import subprocess
import sys
from typing import Sequence
import util
import os

# Map of build configuration names to the name CMake uses for them
BUILD_CONFIGS = {
    "release": "Release",
    "debug": "Debug",
    "relwithdebinfo": "RelWithDebInfo",
    "all": "all",
}


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="build",
        description="Build the Deluge firmware",
    )
    parser.group = "Building"
    parser.add_argument(
        "-S", "--no-status", help="Disable ninja-build status line", action="store_true"
    )
    parser.add_argument(
        "-v",
        "--verbose",
        help="Print compilation commands as they are used",
        action="store_true",
    )
    parser.add_argument(
        "-c",
        "--clean-first",
        help="Cleanup any old build artefacts before building",
        action="store_true",
    )
    parser.add_argument(
        "-t",
        "--type",
        help="The type of metadata tag (only applicable with -m, see `dbt configure -h` for options)",
    )

    parser.add_argument(
        "-m",
        "--tag-metadata",
        help="Tag the build with a metadata suffix",
        action="store_true",
    )
    parser.add_argument(
        "config",
        nargs="?",
        choices=list(BUILD_CONFIGS.keys()) + list(BUILD_CONFIGS.values()),
    )
    return parser


def main(argv: Sequence[str] = None) -> int:
    (args, unknown_args) = argparser().parse_known_args(argv)

    os.chdir(util.get_git_root())

    if args.tag_metadata:
        configure_args = []
        if args.type:
            configure_args += ["-t", args.type]
        # configure with tagging
        result = importlib.import_module("task-configure").main(
            ["-m"] + configure_args + unknown_args
        )
        if result != 0:
            return result

    elif not os.path.exists("build"):
        result = importlib.import_module("task-configure").main()
        if result != 0:
            return result

    build_args = []
    build_args += ["--build", "build"]
    build_args += ["--target", "deluge"]

    if args.config and args.config != "all":
        config = (
            BUILD_CONFIGS[args.config] if args.config in BUILD_CONFIGS else args.config
        )
        build_args += ["--config", config]

    if args.verbose:
        build_args += ["--verbose"]
    else:
        os.environ["NINJA_STATUS"] = "[%s/%t %p :: %e] "

    if args.clean_first:
        build_args += ["--clean-first"]

    # Append unknown arguments to CMake arglist
    build_args += unknown_args

    if args.no_status:
        build_args += ["--", "--quiet"]  # pass quiet directly to ninja

    result = subprocess.run(["cmake"] + build_args, env=os.environ)
    return result.returncode


if __name__ == "__main__":
    main()
