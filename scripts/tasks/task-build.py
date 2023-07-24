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
    parser.add_argument("-q", "--quiet", help="Quiet output", action="store_true")
    parser.add_argument(
        "target",
        default="all",
        const="all",
        nargs="?",
        choices=["7seg", "oled", "all", "clean"],
    )
    parser.add_argument(
        "config",
        default="debug",
        const="debug",
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

    if not os.path.exists("build"):
        result = importlib.import_module("task-configure").main()
        if result != 0:
            return result

    build_args = [
        "--build",
        "build",
        "--config",
        f"{args.config.title()}",
        "--target",
        target,
    ]
    build_args += unknown_args
    if args.quiet:
        build_args += ["--", "--quiet"] # pass quiet to ninja
    
    os.environ['NINJA_STATUS'] = "[%s/%t %p :: %e] "

    result = subprocess.run(["cmake"] + build_args, env=os.environ)
    return result.returncode


if __name__ == "__main__":
    main()
