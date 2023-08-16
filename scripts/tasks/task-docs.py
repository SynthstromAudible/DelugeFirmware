#! /usr/bin/env python3
import argparse
import importlib
import subprocess
import sys
from typing import Sequence
import util
import os
import webbrowser


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="docs",
        description="Open the docs for the Deluge Firmware (rebuilding if necessary)",
    )
    parser.add_argument(
        "-R",
        "--no-rebuild",
        help="Do not attempt to rebuild documentation",
        action="store_true",
    )
    parser.group = "Development"
    return parser


def main(argv: Sequence[str] = sys.argv) -> int:
    (args, unknown_args) = argparser().parse_known_args(argv)

    project_root = util.get_git_root()
    build_dir = project_root.absolute() / "build"
    source_dir = project_root.absolute()
    index_page = build_dir / "html/index.html"

    if not os.path.exists("build"):
        result = importlib.import_module("task-configure").main()
        if result != 0:
            return result

    build_args = []
    build_args += ["--build", "build"]
    build_args += ["--target", "doxygen"]

    result = subprocess.run(["cmake"] + build_args, env=os.environ)
    if result.returncode == 0:
        if webbrowser.open(index_page.absolute()):
            return 0
    return 1


if __name__ == "__main__":
    main()
