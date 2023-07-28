#! /usr/bin/env python3
import argparse
import importlib
import subprocess
import sys
import util
import os


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
        "-m",
        "--tag-metadata",
        help="Tag the build with a metadata suffix",
        action="store_true",
    )
    parser.add_argument(
        "target",
        default="all",
        const="all",
        nargs="?",
        choices=["7seg", "oled", "all", "clean"],
    )
    parser.add_argument(
        "config",
        nargs="?",
        choices=["release", "debug", "relwithdebinfo"],
    )
    return parser


def main() -> int:
    (args, unknown_args) = argparser().parse_known_args()
    if args.target in ["7seg", "oled"]:
        target = "deluge" + args.target.upper()
    else:
        target = args.target

    os.chdir(util.get_git_root())

    if args.tag_metadata:
        # configure with tagging
        result = importlib.import_module("task-configure").main(["-m"] + unknown_args)
        if result != 0:
            return result

    elif not os.path.exists("build"):
        result = importlib.import_module("task-configure").main()
        if result != 0:
            return result

    build_args = []
    build_args += ["--build", "build"]
    build_args += ["--target", target]

    if args.config:
        build_args += ["--config", f"{args.config.title()}"]

    if args.verbose:
        build_args += ["--verbose"]

    if args.clean_first:
        build_args += ["--clean-first"]

    # Append unknown arguments to CMake arglist
    build_args += unknown_args

    if args.no_status:
        build_args += ["--", "--quiet"]  # pass quiet directly to ninja

    os.environ["NINJA_STATUS"] = "[%s/%t %p :: %e] "

    result = subprocess.run(["cmake"] + build_args, env=os.environ)
    return result.returncode


if __name__ == "__main__":
    main()
