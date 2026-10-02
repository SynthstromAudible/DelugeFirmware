#! /usr/bin/env python3
import argparse
import importlib
import os
import subprocess
from collections.abc import Sequence

import util

# Toolchain trees. The shipping device build is clang (default, `build/`); the
# GCC tree is a fallback second opinion (`build-gcc/`). They can never share
# objects: GCC mangles int32_t as `long`, clang as `int`.
TREES = {
    "clang": ("build", "scripts/cmake/CMakeToolchainDelugeClang.cmake"),
    "gcc": ("build-gcc", "scripts/cmake/CMakeToolchainDeluge.cmake"),
}


class CondensedChoiceFormatter(argparse.ArgumentDefaultsHelpFormatter):
    def _format_action_invocation(self, action):
        if not action.option_strings or action.nargs == 0:
            return super()._format_action_invocation(action)
        default = self._get_default_metavar_for_optional(action)
        args_string = self._format_args(action, default)
        return ", ".join(action.option_strings) + " " + args_string


def argparser() -> argparse.ArgumentParser:
    def fmt(prog):
        return CondensedChoiceFormatter(prog)

    parser = argparse.ArgumentParser(
        prog="configure",
        description="Configure a CMake build (automatically called by build)",
        formatter_class=fmt,
    )
    parser.add_argument(
        "-f", "--force", help="Forcibly (re)configure the project.", action="store_true"
    )
    parser.add_argument(
        "-m",
        "--tag-metadata",
        help="Tag any builds with this configuration with a metadata suffix",
        action="store_true",
    )
    parser.add_argument(
        "-t",
        "--type",
        help="The type of metadata tag (only applicable with -m)",
        default="dev",
        choices=["dev", "nightly", "alpha", "beta", "rc", "release"],
    )
    parser.add_argument(
        "--toolchain",
        help="Which toolchain tree to configure",
        default="clang",
        choices=list(TREES.keys()),
    )
    parser.group = "Building"
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    (args, unknown_args) = argparser().parse_known_args(argv)

    project_root = util.get_git_root()
    (tree_dir, toolchain_file) = TREES[args.toolchain]
    build_dir = project_root.absolute() / tree_dir
    source_dir = project_root.absolute()

    if args.force:
        result = importlib.import_module("task-nuke").main()
        if result != 0:
            return result

    configure_args = []
    configure_args += ["-B", build_dir]  # build location
    configure_args += ["-S", source_dir]  # project root
    configure_args += ["-G", "Ninja Multi-Config"]  # generator

    configure_args += [
        "-DCMAKE_CROSS_CONFIGS:STRING=all",  # which configs will reference others
        "-DCMAKE_DEFAULT_CONFIGS=Debug;Release",  # set the default (empty) configs
        "-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE",  # export compile commands
        f"-DCMAKE_TOOLCHAIN_FILE={project_root.absolute() / toolchain_file}",
    ]

    # Append unknown arguments to CMake arglist
    configure_args += unknown_args

    # add the metadata suffix (see project CMakeLists.txt)
    if args.tag_metadata:
        configure_args += [
            "-DENABLE_VERSIONED_OUTPUT:BOOL=TRUE",
            f"-DRELEASE_TYPE:STRING={args.type.lower()}",
        ]

    result = subprocess.run(["cmake"] + configure_args, env=os.environ, check=False)
    return result.returncode


if __name__ == "__main__":
    main()
