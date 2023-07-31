#! /usr/bin/env python3

# Copyright 2023 Kate Whitlock
import argparse
import errno
import os
import io
import fnmatch
from pathlib import Path
import util
from functools import partial

EXEC_EXT = ".exe" if os.name == "nt" else ""


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


def get_clang_format():
    git_path = util.get_git_root()
    for path in git_path.rglob(f"clang-format{EXEC_EXT}"):
        return path
    # failed to find it in toolchains, falling back to path, then string
    return util.find_cmd_with_fallback("clang-format")


def get_header_and_source_files(path, recursive: bool):
    glob = path.rglob if recursive else path.glob
    globs = [
        glob("*.cc"),
        glob("*.hh"),
        glob("*.[ch]xx"),
        glob("*.[ch]pp"),
        glob("*.[ch]"),
    ]
    return [file for files in globs for file in list(files)]


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


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="format",
        description="Formats files using clang-format (Assumes .clang-format present in directory structure)",
    )
    parser.group = "Miscellaneous"
    parser.add_argument(
        "-nr",
        "--no-recursive",
        help="do not recursively descend and process files",
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
        "directory",
        nargs="?",
        help="the directory of source files to format (defaults to whole project)",
    )
    return parser


def main() -> int:
    args = argparser().parse_args()
    directory = (
        Path(args.directory)
        if args.directory is not None
        else (util.get_git_root().absolute() / "src")
    )
    files = get_header_and_source_files(directory, not args.no_recursive)
    excludes = excludes_from_file(".clang-format-ignore")
    files = exclude(files, excludes)
    clang_format = get_clang_format()
    check = args.check

    if args.quiet:
        result = util.do_parallel(partial(format_file, clang_format, False, check), files)
    elif args.verbose:
        # Single-process for output purposes :/
        result = [0] * len(files)
        for (i, file) in enumerate(files):
            result[i] = format_file(clang_format, True, check, file)
            print(f"Formatting {file}")
    else:
        result = util.do_parallel_progressbar(
            partial(format_file, clang_format, False, check), files, "Formatting: "
        )

    if any(map(lambda x: x != 0, result)):
        print("Done, formatting mismatch detected")
        return 1

    print("Done!")
    return 0


if __name__ == "__main__":
    main()
