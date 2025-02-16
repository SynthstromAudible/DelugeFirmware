#! /usr/bin/env python3
import argparse
import platform
import subprocess
import os
import sys

PLATFORM = platform.system().lower()


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="exec",
        description="Execute a command in the DBT environment",
    )
    parser.group = "Development"
    return parser


def main() -> int:
    result = subprocess.run(sys.argv[1:], env=os.environ, shell=True)
    return result.returncode


if __name__ == "__main__":
    main()
