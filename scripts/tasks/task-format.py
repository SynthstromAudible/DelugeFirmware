#! /usr/bin/env python3

# Copyright 2023 Kate Whitlock
import argparse
import errno
import fnmatch
import os
import shutil
from functools import partial
from itertools import groupby
from pathlib import Path

import util

EXEC_EXT = ".exe" if os.name == "nt" else ""

DBT_VERSION = util.get_dbt_version()


def excludes_from_file(ignore_file):
    excludes = []
    try:
        with open(ignore_file, "r", encoding="utf-8") as f:
            for line in f:
                if line.startswith("#"):
                    # ignore comments
                    continue
                pattern = line.rstrip()
                if not pattern:
                    # allow empty lines
                    continue
                excludes.append(pattern)
    except OSError as e:
        if e.errno != errno.ENOENT:
            raise
    return excludes


def exclude(files, excludes):
    fpaths = [f for f in files]
    for pattern in excludes:
        fpaths = [x for x in fpaths if not fnmatch.fnmatch(x, pattern)]
    return fpaths


def get_clang_format_cmd():
    tool_path = util.get_git_root() / "toolchain" / f"v{DBT_VERSION}"
    for path in tool_path.rglob(f"clang-format{EXEC_EXT}"):
        if path.is_file():
            os.environ["PATH"] += os.pathsep + str(path.parent.absolute())
            return path
    # failed to find it in toolchains, falling back to path, then string
    return util.find_cmd_with_fallback("clang-format")


globs = [
    "*.cc",
    "*.hh",
    "*.[ch]xx",
    "*.[ch]pp",
    "*.[ch]",
]


def get_header_and_source_files(path, recursive: bool):
    glob = path.rglob if recursive else path.glob
    return [file for pattern in globs for file in list(glob(pattern))]


def get_valid_header_and_source_files(files):
    return list(
        filter(
            bool,
            [
                fnmatch.fnmatch(file, pattern) and file
                for pattern in globs
                for file in files
            ],
        )
    )


def format_file(clang_format: str, verbose: bool, check: bool, path: Path):
    path = str(path.absolute())
    command = [clang_format, "--style=file"]
    if check:
        command.append("--dry-run")
        command.append("-Werror")
    else:
        command.append("-i")
    command.append(path)
    return util.run(command, verbose, verbose)


def format_stdio(filename: str):
    command = [get_clang_format_cmd(), "--style=file", "--assume-filename", filename]
    return util.run(command)


# ruff lints and formats Python; it is version-pinned in .pre-commit-config.yaml,
# so we drive it through pre-commit to guarantee the same ruff CI uses.
RUFF_HOOKS = ("ruff-check", "ruff-format")


def ruff_scope(args) -> list[str] | None:
    """pre-commit file-scope args for the ruff hooks, or None if there's nothing to do."""
    if args.changed:
        # pre-commit defaults to the staged files, matching --changed above.
        return []
    if args.files_and_directories:
        targets = []
        for entry in args.files_and_directories:
            path = Path(entry)
            if path.is_dir():
                glob = path.glob if args.no_recursive else path.rglob
                targets += [str(p) for p in glob("*.py")]
            elif path.suffix == ".py":
                targets.append(str(path))
        if not targets:
            return None
        return ["--files", *targets]
    return ["--all-files"]


def format_python(args) -> int:
    """Lint-fix and format Python via the ruff pre-commit hooks.

    The hooks fix in place; in --check mode they still report a non-zero exit
    when a file needed changes (pre-commit cannot check without applying fixes).
    """
    scope = ruff_scope(args)
    if scope is None:
        return 0
    pre_commit = shutil.which("pre-commit")
    if pre_commit is None:
        print("Warning: pre-commit not found, skipping Python (ruff) formatting")
        return 0
    result = 0
    for hook in RUFF_HOOKS:
        result |= util.run([pre_commit, "run", hook, *scope])
    return result


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="format",
        description="Formats files using clang-format (Assumes .clang-format present in directory structure)",
    )
    parser.group = "Miscellaneous"
    parser.add_argument(
        "-nr",
        "--no-recursive",
        help="do not recursively descend and process files in directories",
        action="store_true",
    )
    parser.add_argument(
        "-q", "--quiet", help="don't show any output", action="store_true"
    )
    parser.add_argument(
        "-v", "--verbose", help="print the changes happening", action="store_true"
    )
    parser.add_argument(
        "-c", "--check", help="check for format compliance locally", action="store_true"
    )
    parser.add_argument(
        "-g", "--changed", help="format only changed files", action="store_true"
    )
    parser.add_argument(
        "-i",
        "--stdio",
        help="format stdin to stdout (must also specify a single filename for clang-format to use)",
        action="store_true",
    )
    parser.add_argument(
        "files_and_directories",
        nargs="*",
        help="files and/or directories of source files to format (defaults to whole project)",
    )
    return parser


def main() -> int:
    args = argparser().parse_args()
    if args.stdio:
        if len(args.files_and_directories) != 1:
            print("Error: must specify the file name when using stdio")
            return 1
        filename = args.files_and_directories[0]
        if not Path(filename).is_file():
            print("Error: must specify a valid file name when using stdio")
            return 1
        return format_stdio(filename)

    files_and_directories = []
    if args.changed:
        files_and_directories = [
            Path(f)
            for f in util.run_get_output(
                ["git", "diff", "--name-only", "--cached", "--diff-filter=ACMRTUXB"]
            ).splitlines()
        ]
    elif args.files_and_directories:
        files_and_directories = [Path(f) for f in args.files_and_directories]
    else:
        files_and_directories = [
            util.get_git_root().absolute() / p for p in ["src", "tests"]
        ]

    temp = [[], []]
    directories, files = temp
    for is_file, group in groupby(files_and_directories, lambda p: p.is_file()):
        for thing in group:
            temp[is_file].append(thing)

    found_files = {
        file
        for dir in directories
        for file in get_header_and_source_files(dir, not args.no_recursive)
    }
    remaining_files = set(files).difference(found_files)
    valid_files = get_valid_header_and_source_files(remaining_files)
    files = found_files.union(valid_files)

    excludes = excludes_from_file(".clang-format-ignore")
    files = exclude(files, excludes)

    check = args.check

    # Format C/C++ sources with clang-format.
    if not files:
        result = []
    elif args.quiet:
        result = util.do_parallel(
            partial(format_file, get_clang_format_cmd(), False, check), files
        )
    elif args.verbose:
        clang_format = get_clang_format_cmd()
        # Single-process for output purposes :/
        result = [0] * len(files)
        for i, file in enumerate(files):
            result[i] = format_file(clang_format, True, check, file)
            print(f"Formatting {file}")
    else:
        result = util.do_parallel_progressbar(
            partial(format_file, get_clang_format_cmd(), False, check),
            files,
            "Formatting: ",
        )

    # Lint-fix and format Python sources with ruff (via the pinned pre-commit hooks).
    ruff_result = format_python(args)

    if any(x != 0 for x in result) or ruff_result != 0:
        print("Done, formatting mismatch detected")
        return 1

    print("Done!")
    return 0


# if __name__ == "__main__":
#     main()
