#! /usr/bin/env python3
"""Run the tests declared in harness/registry.toml.

./dbt test                                   # the pr tier: what a pull request runs
./dbt test --level unit                      # one level (repeatable)
./dbt test --tier nightly                    # everything up to and including nightly
./dbt test pattern-load song-save-channel    # by name, whatever their tier
./dbt test unit -- -R mem_guard              # arguments after -- go to ctest
./dbt test --list [--markdown]               # the registry, with prerequisites resolved
./dbt test --doctor                          # what is missing, and how to get it
./dbt test --capture <label>                 # run the estate and record its marker lines
./dbt test --ci-matrix --tier pr             # the selection as JSON, for a CI matrix
./dbt test --clean                           # drop the shared cargo target dir
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import util

sys.path.insert(0, str(Path(__file__).parent))

import harness_registry as hr

# One shared cargo target dir for every cargo-built test. Cargo namespaces artifacts by fingerprint, so packages with
# divergent [patch] sections coexist safely; the cost is that concurrent runs serialize on cargo's target-dir lock.
SHARED_TARGET_SUBDIR = Path("target") / "harness"

# A marker line: an upper-case tag followed by a space, '=', ':' or a further '-TAG'. What a capture records.
MARKER = re.compile(r"^[A-Z][A-Z0-9_]*(-[A-Z0-9_]+)*([ =:]|$)")


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="test",
        description="Run the tests declared in harness/registry.toml (default: the pr tier)",
    )
    parser.group = "Development"
    parser.add_argument(
        "names", nargs="*", help="Tests to run by name, whatever their tier"
    )
    parser.add_argument(
        "--level",
        action="append",
        choices=hr.LEVELS,
        default=[],
        help="Only this level",
    )
    parser.add_argument(
        "--tier",
        choices=hr.TIERS,
        default="pr",
        help="Run every test up to and including this tier (default pr)",
    )
    parser.add_argument(
        "--list", action="store_true", help="List the selection instead of running it"
    )
    parser.add_argument(
        "--markdown",
        action="store_true",
        help="With --list: the README table, for every test",
    )
    parser.add_argument(
        "--doctor", action="store_true", help="Report missing prerequisites"
    )
    parser.add_argument(
        "--clean", action="store_true", help="Remove the shared cargo target dir"
    )
    parser.add_argument(
        "--ci-matrix",
        action="store_true",
        help="Print the selection as JSON for a CI matrix",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="A test skipped for a missing prerequisite fails the run (CI)",
    )
    parser.add_argument(
        "--skip-checks",
        action="store_true",
        help="Run tests even when a prerequisite probe fails",
    )
    parser.add_argument(
        "-n",
        "--build-only",
        action="store_true",
        help="Build the selection without running it",
    )
    parser.add_argument(
        "--junit", metavar="DIR", help="Collect each test's JUnit XML into DIR"
    )
    parser.add_argument(
        "--capture",
        metavar="LABEL",
        help="Run every gate above unit level (up to --tier nightly) and record its marker lines under "
        ".superpowers/estate/LABEL/, for diffing two trees",
    )
    return parser


@dataclass
class Outcome:
    name: str
    result: str  # PASS | FAIL | SKIP
    note: str = ""


def _load(root: Path) -> list[hr.Harness]:
    try:
        return hr.load_registry(root)
    except (OSError, ValueError) as exc:
        print(f"test: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc


def _environment(root: Path, h: hr.Harness) -> dict[str, str]:
    env = dict(h.env_default, **os.environ)
    env.update(h.env)
    env.setdefault("CARGO_TARGET_DIR", str(root / SHARED_TARGET_SUBDIR))
    return env


def _steps(
    root: Path, h: hr.Harness, passthrough: list[str], junit_dir: Path | None
) -> list[tuple[str, list[str]]]:
    """The commands that build and run `h`, as (phase, argv) pairs."""
    if h.runner != "ctest":
        steps = [("build", h.build)] if h.build else []
        return steps + [("run", h.run)]

    tree = root / h.tree
    steps = []
    if not (tree / "CMakeCache.txt").exists():
        steps.append(
            (
                "configure",
                ["cmake", "-S", str(root / h.source), "-B", str(tree), *h.configure],
            )
        )
    steps.append(("build", ["cmake", "--build", str(tree)]))
    # --no-tests=error: a -R filter that matches nothing must fail, not pass having run nothing.
    run = [
        "ctest",
        "--test-dir",
        str(tree),
        "--output-on-failure",
        "--no-tests=error",
        *h.ctest_args,
    ]
    if junit_dir is not None:
        run += ["--output-junit", str(junit_dir / f"{h.name}.xml")]
    steps.append(("run", run + passthrough))
    return steps


def _run_one(
    root: Path,
    h: hr.Harness,
    passthrough: list[str],
    build_only: bool,
    junit_dir: Path | None,
    log,
) -> bool:
    env = _environment(root, h)
    workdir = root / h.path
    for phase, argv in _steps(root, h, passthrough, junit_dir):
        if build_only and phase == "run":
            break
        print(f"==> {h.name}: {phase}: {' '.join(argv)}", flush=True)
        if log is None:
            result = subprocess.run(argv, cwd=workdir, env=env, check=False)
        else:
            result = subprocess.run(
                argv,
                cwd=workdir,
                env=env,
                check=False,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
        if result.returncode != 0:
            print(
                f"test: '{h.name}' {phase} failed (exit {result.returncode})",
                file=sys.stderr,
            )
            return False

    if junit_dir is not None and h.junit and not build_only:
        produced = Path(h.junit.replace("${CARGO_TARGET_DIR}", env["CARGO_TARGET_DIR"]))
        if not produced.is_absolute():
            produced = root / produced
        if produced.is_file():
            shutil.copyfile(produced, junit_dir / f"{h.name}.xml")
    return True


def cmd_run(
    root: Path, selection: list[hr.Harness], args, passthrough: list[str], named: bool
) -> int:
    junit_dir = Path(args.junit).absolute() if args.junit else None
    if junit_dir is not None:
        junit_dir.mkdir(parents=True, exist_ok=True)

    outcomes: list[Outcome] = []
    for h in selection:
        if h.status == "unavailable":
            print(f"warning: '{h.name}' is marked unavailable -- {h.gates}\n")
        missing = [r for r in h.requires if not hr.probe(r, root)]
        if missing and not args.skip_checks:
            outcomes.append(Outcome(h.name, "SKIP", f"needs {', '.join(missing)}"))
            print(f"--- {h.name}: skipped, needs {', '.join(missing)}", flush=True)
            continue
        ok = _run_one(
            root,
            h,
            passthrough if h.runner == "ctest" else [],
            args.build_only,
            junit_dir,
            None,
        )
        outcomes.append(Outcome(h.name, "PASS" if ok else "FAIL"))

    return _summarise(outcomes, strict=args.strict or named)


def _summarise(outcomes: list[Outcome], strict: bool) -> int:
    if len(outcomes) > 1:
        width = max(len(o.name) for o in outcomes)
        print("\n== summary")
        for o in outcomes:
            print(f"   {o.result}  {o.name:<{width}}  {o.note}".rstrip())
    failed = [o for o in outcomes if o.result == "FAIL"]
    skipped = [o for o in outcomes if o.result == "SKIP"]
    passed = len(outcomes) - len(failed) - len(skipped)
    print(f"\n{passed} passed, {len(failed)} failed, {len(skipped)} skipped")
    if skipped and strict:
        print(
            "test: a skip counts as a failure here (--strict, or the test was named)",
            file=sys.stderr,
        )
    return 1 if failed or (skipped and strict) else 0


def cmd_capture(root: Path, entries: list[hr.Harness], label: str, tier: str) -> int:
    """Run every gate above unit level and record what each printed, for comparing two trees."""
    out = root / ".superpowers" / "estate" / label
    out.mkdir(parents=True, exist_ok=True)
    selection = [
        h
        for h in hr.select(entries, [], ["component", "integration"], tier)
        if h.status == "gate" and h.tier != "manual"
    ]
    markers_path = out / "markers.txt"
    summary_path = out / "summary.txt"
    markers_path.write_text("")
    summary_path.write_text("")

    passed = 0
    for h in selection:
        print(f"== {h.name}", flush=True)
        log_path = out / f"{h.name}.log"
        missing = [r for r in h.requires if not hr.probe(r, root)]
        with log_path.open("w") as log:
            if missing:
                log.write(f"skipped: needs {', '.join(missing)}\n")
                rc = 2
            else:
                rc = 0 if _run_one(root, h, [], False, None, log) else 1
        passed += rc == 0
        with summary_path.open("a") as summary:
            summary.write(f"{h.name} rc={rc}\n")
        with markers_path.open("a") as markers:
            for line in log_path.read_text(errors="replace").splitlines():
                if MARKER.match(line):
                    markers.write(f"{h.name}: {line}\n")
            markers.write(f"{h.name}: rc={rc}\n")

    line = f"passed {passed}/{len(selection)}"
    print(line)
    with summary_path.open("a") as summary:
        summary.write(line + "\n")
    return 0 if passed == len(selection) else 1


def cmd_list(
    root: Path, selection: list[hr.Harness], markdown: bool, entries: list[hr.Harness]
) -> int:
    if markdown:
        print(hr.markdown_table(entries), end="")
        return 0
    width = max(len(h.name) for h in selection) if selection else 0
    for h in selection:
        missing = [r for r in h.requires if not hr.probe(r, root)]
        mark = "OK " if not missing else "-- "
        note = "" if not missing else f"  (needs: {', '.join(missing)})"
        print(
            f"{mark} {h.name:<{width}}  {h.level:<11} {h.status:<13} {h.tier:<7} {h.runtime}{note}"
        )
        print(f"    {' ' * width}  {h.gates}")
    return 0


def cmd_doctor(root: Path, entries: list[hr.Harness]) -> int:
    needed = {r for h in entries for r in h.requires}
    missing = sorted(r for r in needed if not hr.probe(r, root))
    if not missing:
        print("All test prerequisites satisfied.")
        return 0
    print("Missing prerequisites:\n")
    for req in missing:
        blocked = [h.name for h in entries if req in h.requires]
        print(f"  {req}")
        print(f"    fix:     {hr.PROBES[req]}")
        print(f"    blocks:  {', '.join(blocked)}\n")
    return 1


def cmd_clean(root: Path) -> int:
    target = root / SHARED_TARGET_SUBDIR
    if target.is_dir():
        shutil.rmtree(target)
        print(f"removed {target}")
    else:
        print(f"nothing to remove at {target}")
    return 0


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    passthrough: list[str] = []
    if "--" in argv:
        cut = argv.index("--")
        argv, passthrough = argv[:cut], argv[cut + 1 :]
    args = argparser().parse_args(argv)
    root = Path(util.get_git_root()).absolute()
    entries = _load(root)

    if args.clean:
        return cmd_clean(root)
    if args.doctor:
        return cmd_doctor(root, entries)
    if args.capture:
        return cmd_capture(
            root, entries, args.capture, "nightly" if args.tier == "pr" else args.tier
        )

    try:
        selection = hr.select(entries, args.names, args.level, args.tier)
    except ValueError as exc:
        print(f"test: {exc}", file=sys.stderr)
        return 1

    if args.ci_matrix:
        print(json.dumps({"test": [h.name for h in selection]}))
        return 0
    if args.list:
        return cmd_list(root, selection, args.markdown, entries)
    return cmd_run(root, selection, args, passthrough, named=bool(args.names))


if __name__ == "__main__":
    sys.exit(main())
