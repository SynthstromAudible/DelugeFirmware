#! /usr/bin/env python3
"""The old spelling of `dbt test`, kept for one release. Each command says what replaced it."""

import argparse
import importlib.util
import sys
from pathlib import Path


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="harness",
        description="Renamed: use `dbt test` (this alias prints the new spelling and runs it)",
    )
    parser.group = "Development"
    sub = parser.add_subparsers(dest="command")
    p_list = sub.add_parser("list")
    p_list.add_argument("--markdown", action="store_true")
    p_run = sub.add_parser("run")
    p_run.add_argument("name")
    p_run.add_argument("--skip-checks", action="store_true")
    sub.add_parser("doctor")
    p_ci = sub.add_parser("ci")
    p_ci.add_argument("--tier", choices=["pr", "nightly"], required=True)
    sub.add_parser("clean")
    return parser


def _task_test():
    spec = importlib.util.spec_from_file_location(
        "task_test", Path(__file__).parent / "task-test.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    args = argparser().parse_args()
    match args.command:
        case "list":
            new = ["--list", "--tier", "manual"] + (
                ["--markdown"] if args.markdown else []
            )
        case "run":
            new = [args.name] + (["--skip-checks"] if args.skip_checks else [])
        case "doctor":
            new = ["--doctor"]
        case "ci":
            new = ["--ci-matrix", "--tier", args.tier]
        case "clean":
            new = ["--clean"]
        case _:
            argparser().print_help()
            return 1
    print(f"note: `dbt harness` is now `dbt test {' '.join(new)}`", file=sys.stderr)
    return _task_test().main(new)


if __name__ == "__main__":
    sys.exit(main())
