#! /usr/bin/env python3

# Copyright 2023 Kate Whitlock
import argparse
import os
from pathlib import Path
import util
from functools import partial

EXEC_EXT = ".exe" if os.name == "nt" else ""


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


def format_file(clang_format: str, verbose: bool, path: Path):
    path = str(path.absolute())
    util.run([clang_format, "--style=file", "-i", path], verbose, verbose)


def argparser():
    parser = argparse.ArgumentParser(
        prog="task format",
        description="Formats files using clang-format (Assumes .clang-format present in directory structure)",
    )
    parser.add_argument(
        "-nr",
        "--no-recursive",
        help="do not recursively descend and process files",
        action="store_true"
    )
    parser.add_argument(
        "-q", "--quiet", help="don't show any output", action="store_true"
    )
    parser.add_argument(
        "-v", "--verbose", help="print the changes happening", action="store_true"
    )
    parser.add_argument(
        "directory", help="the directory of source files to format"
    )
    return parser


def main():
    args = argparser().parse_args()
    files = get_header_and_source_files(Path(args.directory), not args.no_recursive)
    clang_format = get_clang_format()
    if files:
        if args.quiet:
            util.do_parallel(partial(format_file, clang_format, False), files)
        elif args.verbose:
            # Single-process for output purposes :/
            for file in files:
                format_file(clang_format, True, file)
                print(f"Formatting {file}")
        else:
            util.do_parallel_progressbar(partial(format_file, clang_format, False), files, "Formatting: ")
        print("Done!")
    else:
        print("No files found! Did you mean to add '-r'?")


if __name__ == "__main__":
    main()
