#!/usr/bin/env python3

# Copyright Kate Whitlock (2023)
import os
import re
import argparse


def snake_case(name):
    s1 = re.sub("(.)([A-Z][a-z]+)", r"\1_\2", name)
    s2 = re.sub("([A-Za-z0-9])([A-Z])", r"\1_\2", s1).lower()
    return s2


def main():
    parser = argparse.ArgumentParser(description="Rename files to snake case.")
    parser.add_argument(
        "-v", "--verbose", help="print the list of renamed files", action="store_true"
    )
    parser.add_argument(
        "-r", "--recursive", help="process directory recursively", action="store_true"
    )
    parser.add_argument(
        "-d", "--dry-run", help="don't actually do the action", action="store_true"
    )
    parser.add_argument("directory")

    args = parser.parse_args()

    path = args.directory
    paths = (
        [os.path.join(dp, f) for dp, dn, fn in os.walk(path) for f in fn]
        if args.recursive
        else os.listdir(path)
    )

    for path in paths:
        dirname, filename = os.path.split(path)
        new_filename = snake_case(os.path.basename(filename))
        new_path = os.path.join(dirname, new_filename)
        if new_path != path:
            if not args.dry_run:
                os.rename(path, "temp")
                os.rename("temp", new_path)
            if args.verbose:
                print(f"Renamed: {path}\n => {new_path}")


if __name__ == "__main__":
    main()
