#! /usr/bin/env python3
import argparse
import platform
import subprocess
import shutil
import os
import sys

# Based on dbt_tools/shell.py by litui

PLATFORM = platform.system().lower()


def get_shell():
    if PLATFORM == "windows":
        shell_command = shutil.which("cmd.exe")
    else:
        shell_command = (
            os.environ.get("SHELL") or shutil.which("bash") or shutil.which("sh")
        )
    return shell_command


def argparser():
    parser = argparse.ArgumentParser(
        prog="shell",
        description="Open a shell in the DBT environment",
    )
    parser.group = "Development"
    return parser


def main():
    subprocess.run([get_shell()] + sys.argv[1:] , env=os.environ)


if __name__ == "__main__":
    main()
