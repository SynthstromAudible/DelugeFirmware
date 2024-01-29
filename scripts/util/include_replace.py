#!/usr/bin/env python3

# Copyright Kate Whitlock (2023)
from typing import *
from subprocess import Popen, PIPE
from pathlib import Path
import os
import re
import sys
import argparse

import pprint
import in_place

pp = pprint.PrettyPrinter(indent=4)


def get_git_renames(start: str, end: str):
    p = Popen(
        f"git diff --relative -M --name-status --diff-filter=R {start}..{end}",
        stdout=PIPE,
        stderr=PIPE,
    )
    stdout, stderr = p.communicate()
    status_list = stdout.decode("utf-8").splitlines()
    return [(line.split("\t")[1], line.split("\t")[2]) for line in status_list]


def filter_headers(filename):
    return Path(filename).suffix in [".h", ".hpp", ".hh"]


def reduce_rename_map(filemap) -> Tuple[bool, str]:
    reduced = True
    for old, new in filemap.items():
        if new in filemap:
            reduced = False
            filemap[old] = filemap[new]
            del filemap[new]
    return (reduced, filemap)


# from https://stackoverflow.com/a/34482761
def progressbar(it, prefix: str, size: int = 60, out=sys.stdout):
    count = len(it)

    def show(j):
        x = int(size * j / count)
        print(
            f"{prefix}[{u'#'*x}{('-'*(size-x))}] {j}/{count}",
            end="\r",
            file=out,
            flush=True,
        )

    show(0)
    for i, item in enumerate(it):
        yield item
        show(i + 1)
    print("\n", flush=True, file=out)


def main():
    parser = argparse.ArgumentParser(description="Process files for include changes.")
    parser.add_argument(
        "-v",
        "--verbose",
        help="print the list of renamed files (disables progressbar)",
        action="store_true",
    )
    parser.add_argument(
        "-d", "--dry-run", help="don't execute the replacements", action="store_true"
    )
    parser.add_argument(
        "-s", "--start", help="the first commit to check", default="HEAD~1"
    )
    parser.add_argument(
        "-e", "--end", help="the latest commit to check", default="HEAD", type=str
    )
    parser.add_argument("directory", default=".")

    args = parser.parse_args()

    renamed_files = get_git_renames(args.start, args.end)
    renamed_headers = {old: new for old, new in renamed_files if filter_headers(old)}

    reduced = False
    while not reduced:
        (reduced, renamed_headers) = reduce_rename_map(renamed_headers)

    regex_map = {}
    for old_file, new_file in renamed_headers.items():
        old_short_re = re.compile(f'^#include [<"]{os.path.basename(old_file)}[>"]')
        old_full_re = re.compile(f'^#include [<"]{old_file}[>"]')
        new_str = f'#include "{new_file}"'
        regex_map[old_short_re] = new_str
        regex_map[old_full_re] = new_str

    path = args.directory
    paths = [os.path.join(dp, f) for dp, dn, fn in os.walk(path) for f in fn]

    iterator = paths if args.verbose else progressbar(paths, "Processing: ")

    for file_path in iterator:
        try:
            if args.dry_run:
                file = open(file_path, encoding="utf-8")
            else:
                file = in_place.InPlace(file_path, encoding="utf-8")
        except Exception as e:
            print(f"Error with file: {file_path}")
            raise e

        for line in file:
            for old_re, new_str in regex_map.items():
                if args.verbose:
                    if re.search(old_re, line):
                        print(line.strip())
                        print(f"=> {new_str}")
                        print()
                if not args.dry_run:
                    line = re.sub(old_re, new_str, line)
            if not args.dry_run:
                file.write(line)
        file.close()


if __name__ == "__main__":
    main()
