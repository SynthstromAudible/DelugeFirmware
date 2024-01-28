#! /usr/bin/env python3
import argparse
import subprocess
import sys
import util
import os


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="scons",
        description="Run SCons in the current environment (deprecated)",
    )
    parser.group = "Building"
    return parser


def main() -> int:
    scons_args = sys.argv[1:]
    if not scons_args:
        scons_args = ["all"]

    scons_args += ["--warn=target-not-built"]

    if not os.environ.get("DBT_VERBOSE"):
        scons_args += ["-Q"]

    os.chdir(util.get_git_root())
    result = subprocess.run(["scons"] + scons_args, env=os.environ)
    return result.returncode


if __name__ == "__main__":
    main()
