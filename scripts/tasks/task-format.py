#! /usr/bin/env python3

# Copyright 2023 Kate Whitlock
import argparse
import errno
import os
import io
from itertools import groupby
import fnmatch
from pathlib import Path
import util
from functools import partial

# EXEC_EXT = ".exe" if os.name == "nt" else ""
EXEC_EXT = ""  # Disabled until next DBT release

DBT_VERSION = util.get_dbt_version()


def excludes_from_file(ignore_file):
    excludes = []
    try:
        with io.open(ignore_file, "r", encoding="utf-8") as f:
            for line in f:
                if line.startswith("#"):
                    # ignore comments
                    continue
                pattern = line.rstrip()
                if not pattern:
                    # allow empty lines
                    continue
                excludes.append(pattern)
    except EnvironmentError as e:
        if e.errno != errno.ENOENT:
            raise
    return excludes


def exclude(files, excludes):
    out = []
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

    files_and_directories = (
        [Path(f) for f in args.files_and_directories]
        if args.files_and_directories
        else [util.get_git_root().absolute() / p for p in ["src", "tests"]]
    )
    temp = [[], []]
    directories, files = temp
    for is_file, group in groupby(files_and_directories, lambda p: p.is_file()):
        for thing in group:
            temp[is_file].append(thing)

    found_files = set(
        file
        for dir in directories
        for file in get_header_and_source_files(dir, not args.no_recursive)
    )
    remaining_files = set(files).difference(found_files)
    valid_files = get_valid_header_and_source_files(remaining_files)
    files = found_files.union(valid_files)

    excludes = excludes_from_file(".clang-format-ignore")
    files = exclude(files, excludes)

    check = args.check

    if args.quiet:
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

    if any(map(lambda x: x != 0, result)):
        print("Done, formatting mismatch detected")
        return 1

    print("Done!")
    return 0


# if __name__ == "__main__":
#     main()
