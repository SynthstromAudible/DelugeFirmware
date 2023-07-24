#! /usr/bin/env python3
import argparse
import util
import os
from pathlib import Path


def generate_cmake_recursive(target_dir, verbose=False):
    """
    Recursively generates CMakeLists.txt files for each folder in the directory structure,
    populating targets with sources and adding relevant subdirectories

    Parameters:
        directory (str): The path to the root directory.
        target_name (str): The name of the CMake target to add the source files to.

    Returns:
        None
    """
    target = os.path.basename(target_dir)
    for dirpath, dirs, files in os.walk(target_dir, topdown=False):
        cmake_file_path = os.path.join(dirpath, "CMakeLists.txt")

        # Collect source files in the current directory
        sources = [
            filename
            for filename in files
            if filename.endswith((".c", ".cpp", ".S", ".s"))
        ]

        # No sources or CMakeLists.txt, go to next directory
        if not sources:
            continue

        contents = f"target_sources({target} PUBLIC\n"
        for source in sources:
            if verbose:
                print(f"Found: {os.path.join(dirpath, source)}")
            contents += f"    {str(source)}\n"
        contents += ")\n"

        # Create or update the CMakeLists.txt file
        with open(cmake_file_path, "w") as file:
            file.write(contents)
        if verbose:
            print(f"Wrote: {cmake_file_path}\n")

def add_subdirectories(target_dir):
    for cmake_file_path in Path(target_dir).rglob('CMakeLists.txt'):
        parent_cmake_file_path = cmake_file_path.parent.parent / 'CMakeLists.txt'

        add_subdirectory_string = f"add_subdirectory({cmake_file_path.parent.name})\n"

        # Check if the add_subdirectory line already exists
        has_add_subdirectory = False
        if os.path.exists(parent_cmake_file_path):
            with open(parent_cmake_file_path, "r") as parent_file:
                has_add_subdirectory = (
                    add_subdirectory_string.strip() in parent_file.read().strip()
                )

        # Add the current directory to the parent CMakeLists.txt
        if has_add_subdirectory:
            continue

        if os.path.exists(parent_cmake_file_path):
            with open(parent_cmake_file_path, "a") as parent_file:
                parent_file.write(add_subdirectory_string)
        else:
            with open(parent_cmake_file_path, "w") as parent_file:
                parent_file.write(add_subdirectory_string)


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="buildgen",
        description="Destructively regenenerate all CMakeLists.txt under src/*/",
    )
    parser.group = "Miscellaneous"
    parser.add_argument(
        "-v",
        "--verbose",
        help="Print filenames as they are detected and processed",
        action="store_true",
    )
    return parser


def main() -> int:
    args = argparser().parse_args()
    os.chdir(util.get_git_root())

    for dir in Path("src").glob("*/"):
        generate_cmake_recursive(dir, args.verbose)
        add_subdirectories(dir)

    print("Complete!")
    return 0


if __name__ == "__main__":
    main()
