#! /usr/bin/env python3
import argparse
import importlib
import os
import subprocess
import sys
import webbrowser
from collections.abc import Sequence

import util


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
    (_args, _unknown_args) = argparser().parse_known_args(argv)

    project_root = util.get_git_root()
    build_dir = project_root.absolute() / "build"
    # source_dir = project_root.absolute()
    index_page = build_dir / "html/index.html"

    if not os.path.exists("build"):
        result = importlib.import_module("task-configure").main()
        if result != 0:
            return result

    build_args = []
    build_args += ["--build", "build"]
    build_args += ["--target", "doxygen"]
    # With Ninja Multi-Config, the plain `doxygen` target expands to multiple
    # config-specific targets (e.g. Debug + Release) that race on the same
    # output directories. Build a single config to avoid intermittent failures.
    build_args += ["--config", "Debug"]

    result = subprocess.run(["cmake"] + build_args, env=os.environ, check=False)
    if result.returncode == 0:
        try:
            if webbrowser.open(str(index_page.absolute())):
                return 0
        except (webbrowser.Error, OSError):
            # In CI/headless environments there may be no browser available.
            # Docs have already been generated successfully at this point.
            return 0
        return 0
    return 1


if __name__ == "__main__":
    main()
