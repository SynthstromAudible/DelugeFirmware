#! /usr/bin/env python3

# Copyright 2023 Kate Whitlock
import argparse
import os
from pathlib import Path
import util
from functools import partial
import mmap


LICENSE_TEMPLATE = """/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
*/"""

LICENSE_CHECK = "This file is part of The Synthstrom Audible Deluge Firmware."


def license_file(dry_run: bool, verbose: bool, path: Path):
    path = str(path.absolute())
    with open(path, "rb", 0) as file, mmap.mmap(
        file.fileno(), 0, access=mmap.ACCESS_READ
    ) as s:
        found = s.find(LICENSE_CHECK.encode()) != -1
    if not dry_run and not found:
        util.prepend_file(LICENSE_TEMPLATE, Path(path))


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="license",
        description="Add Synthstrom Deluge license prelude to files (if not present)",
    )
    parser.group = "Miscellaneous"
    parser.add_argument(
        "-r",
        "--recursive",
        help="recursively descend and process files",
        action="store_true",
    )
    parser.add_argument(
        "-q", "--quiet", help="don't show any output", action="store_true"
    )
    parser.add_argument(
        "-d", "--dry-run", help="don't actually perform the task", action="store_true"
    )
    parser.add_argument(
        "-v", "--verbose", help="print the changes happening", action="store_true"
    )
    parser.add_argument("directory", help="the directory of source files to format")
    return parser


def main() -> int:
    args = argparser().parse_args()
    files = util.get_header_and_source_files(Path(args.directory), args.recursive)
    if files:
        if args.quiet:
            util.do_parallel(partial(license_file, args.dry_run, False), files)
        elif args.verbose:
            # Single-process for output purposes :/
            for file in files:
                license_file(args.dry_run, True, file)
        else:
            util.do_parallel_progressbar(
                partial(license_file, args.dry_run, False), files, "Formatting: "
            )
        print("Done!")
        return 0
    else:
        print("No files found! Did you mean to add '-r'?")
        return -1


if __name__ == "__main__":
    main()
