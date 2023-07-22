#!/usr/bin/env python

import argparse
import importlib
from pathlib import Path
import sys
import os
import textwrap

PROG_NAME = sys.argv[0].split('.')[0]

TASKS_DIR = str(Path(__file__).absolute().parent / "scripts/tasks")
SCRIPTS_DIR = str(Path(__file__).absolute().parent / "scripts")
DBT_DEBUG_DIR = str(Path(__file__).absolute().parent / "scripts/debug")

os.environ["DBT_DEBUG_DIR"] = DBT_DEBUG_DIR

def print_tasks_usage(tasks):
    grouped = {}
    for name, stem in tasks.items():
        argparser = importlib.import_module(stem).argparser()
        try:
            group = argparser.group
        except AttributeError:
            group = "Ungrouped"
        grouped[group] = grouped.get(group, []) + [argparser]
        
    for group, argparsers in sorted(grouped.items()):
        print(textwrap.indent(group + ':', ' ' * 2))
        for argparser in argparsers:
            # get our argparsers (lazy import)
            usage : str = argparser.format_usage().strip().removeprefix("usage: ")
            #usage = f"{PROG_NAME} " + usage
            print(textwrap.indent(usage, ' ' * 4))
            if argparser.description:
                print(textwrap.indent(argparser.description, ' ' * 6))


def print_help(argparser: argparse.ArgumentParser, tasks: dict):
    argparser.print_help()
    print("")
    print("subcommands: ")
    print_tasks_usage(tasks)
    

def main() -> int:
    # Create the main parser
    parser = argparse.ArgumentParser(
        prog=f"{PROG_NAME}" or "task",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        add_help=False,
    )
    parser.add_argument("subcommand", metavar="<subcommand>")

    # Specify the folder containing the task files
    task_files = Path(TASKS_DIR).glob("task-*.py")

    sys.path.append(TASKS_DIR)
    sys.path.insert(0, SCRIPTS_DIR)

    tasks = {}
    for task_file in task_files:
        task = task_file.stem
        task_name = task.replace("task-", "")
        tasks[task_name] = task
    
    # is the subcommand in our list of tasks?
    if len(sys.argv) > 1 and sys.argv[1] in tasks:
        task_name = sys.argv[1]

        # Remove our own program name/path from the arguments
        if sys.argv[1:]:
            sys.argv = sys.argv[1:]

        # Call out to our task. (lazy import)
        retcode = importlib.import_module(tasks[task_name]).main()
        sys.exit(retcode)
    

    # nothing on the command line
    if len(sys.argv) == 1:
        print_help(parser, tasks)
        exit()
        

    args = parser.parse_args()
    
    if args.subcommand not in tasks:
      print(f"{PROG_NAME}: '{args.subcommand}' is not a valid subcommand.")
      print("")
      print("Valid subcommands:")
      print_tasks_usage(tasks)


if __name__ == '__main__':
    try:
        retcode = main()
        sys.exit(retcode)
    except KeyboardInterrupt:
        sys.exit(130)
