#! /usr/bin/env python3
"""Run the tests declared in tests/registry.toml.

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
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import util

sys.path.insert(0, str(Path(__file__).parent))

import harness_registry as hr
import scenario_gate
import test_builds as tb

SHARED_TARGET_SUBDIR = tb.SHARED_TARGET_SUBDIR

# A marker line: an upper-case tag followed by a space, '=', ':' or a further '-TAG'. What a capture records.
# The runner's own output: the real stdout as it was at startup, which no gate check can swap out from under the
# summary. `main` binds it to a duplicate of file descriptor 1.
OUT = sys.stdout
# GOLDEN_SWEEP_JOBS for a test run alone: the caller's, else one per CPU.
ALONE_SWEEP_JOBS = str(os.cpu_count() or 1)

MARKER = re.compile(r"^[A-Z][A-Z0-9_]*(-[A-Z0-9_]+)*([ =:]|$)")


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="test",
        description="Run the tests declared in tests/registry.toml (default: the pr tier)",
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
        "--clean",
        action="store_true",
        help="Remove every tree the runner builds (build/test-*, build/host-app, build/cargo-harness)",
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
        "-j",
        "--jobs",
        type=int,
        default=0,
        help="Run up to N tests at once (default: one per CPU for the pr tier and --capture, 1 above it). Each "
        "run's card is its own memory disk, so runs share nothing; tests marked `alone` run one at a time after the "
        "rest",
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


# The C and C++ compilers every test builds with unless its entry or the caller's environment names others: clang, as
# the firmware is built with.
COMPILER_DEFAULTS = {"CC": "clang", "CXX": "clang++"}


def _base_environment(root: Path, compilers: bool = True) -> dict[str, str]:
    env = {**(COMPILER_DEFAULTS if compilers else {}), **os.environ}
    env.setdefault("CARGO_TARGET_DIR", str(root / SHARED_TARGET_SUBDIR))
    return env


def _environment(
    root: Path, h: hr.Harness, products: dict[str, tb.Product]
) -> dict[str, str]:
    # The caller's environment wins over the entry's defaults, which win over the compiler defaults.
    env = {
        **COMPILER_DEFAULTS,
        **h.env_default,
        **_base_environment(root, compilers=False),
    }
    for name in tb.order(h.builds, products):
        env.update(products[name].test_env)
    env.update(h.env)
    if h.alone:
        env["GOLDEN_SWEEP_JOBS"] = ALONE_SWEEP_JOBS
    return env


def _missing(root: Path, h: hr.Harness, products: dict[str, tb.Product]) -> list[str]:
    """`h`'s prerequisites and its build products', where this machine lacks them."""
    needed = list(h.requires)
    for name in tb.order(h.builds, products):
        needed += [r for r in products[name].requires if r not in needed]
    return [r for r in needed if not hr.probe(r, root)]


class Builds:
    """The build products a selection needs, each built once, dependencies first."""

    def __init__(self, root: Path, selection: list[hr.Harness], log=None):
        self.root = root
        self.known = tb.products(root)
        self.log = log
        # Product name -> why it is unusable. Absent: built.
        self.broken: dict[str, str] = {}
        wanted = [name for h in selection for name in h.builds]
        for name in tb.order(wanted, self.known):
            self._build(name)

    def _build(self, name: str) -> None:
        product = self.known[name]
        for dep in product.deps:
            if dep in self.broken:
                self.broken[name] = f"needs {dep}, which {self.broken[dep]}"
                return
        missing = [r for r in product.requires if not hr.probe(r, self.root)]
        if missing:
            self.broken[name] = f"could not be built here: needs {', '.join(missing)}"
            return
        env = _base_environment(self.root)
        for step in product.steps(self.root):
            print(f"==> build {name}: {step.what}: {' '.join(step.argv)}", flush=True)
            result = subprocess.run(
                step.argv,
                cwd=step.cwd,
                env=dict(env, **step.env),
                check=False,
                stdout=self.log,
                stderr=subprocess.STDOUT if self.log is not None else None,
            )
            if result.returncode != 0:
                self.broken[name] = (
                    f"failed to build ({step.what}, exit {result.returncode})"
                )
                print(
                    f"test: build product '{name}' {self.broken[name]}", file=sys.stderr
                )
                return

    def blocker(self, h: hr.Harness) -> str | None:
        """Why `h` cannot run for want of a product, or None."""
        for name in tb.order(h.builds, self.known):
            if name in self.broken:
                return f"build product {name} {self.broken[name]}"
        return None


def _steps(
    root: Path, h: hr.Harness, passthrough: list[str], junit_dir: Path | None
) -> list[tuple[str, list[str]]]:
    """The commands that build and run `h`, as (phase, argv) pairs."""
    if h.runner != "ctest":
        steps = [("build", h.build)] if h.build else []
        return steps + [("run", h.run)]

    tree = root / h.tree
    steps = []
    # The C++ compiler this tree is configured with, resolved as its environment is (see _environment).
    cxx = os.environ.get("CXX") or h.env_default.get("CXX") or COMPILER_DEFAULTS["CXX"]
    if tb.needs_configure(tree, root / h.source, Path(cxx).name):
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
    products: dict[str, tb.Product],
) -> bool:
    env = _environment(root, h, products)
    if h.runner == "scenario" and h.scenario is not None:
        if build_only:
            return True
        print(
            f"==> {h.name}: run: GOLDEN_SCENARIO={h.scenario or '(stem render)'}",
            file=log if log is not None else sys.stdout,
            flush=True,
        )
        # A cross-built harness sits under its target triple (golden-vt-render-arm sets it).
        binary = (
            Path(env["CARGO_TARGET_DIR"])
            / env.get("GOLDEN_VT_RENDER_TARGET", "")
            / "release"
            / "golden_vt_render"
        )
        passed = scenario_gate.run_gate(
            root, h, env, binary, log if log is not None else sys.stdout
        )
        if not passed:
            print(f"test: '{h.name}' failed", file=sys.stderr)
        return passed
    workdir = root / h.path
    for phase, argv in _steps(root, h, passthrough, junit_dir):
        if build_only and phase == "run":
            break
        print(
            f"==> {h.name}: {phase}: {' '.join(argv)}",
            file=log if log is not None else sys.stdout,
            flush=True,
        )
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

    builds = Builds(root, selection)
    outcomes: dict[str, Outcome] = {}
    runnable: list[hr.Harness] = []
    for h in selection:
        if h.status == "unavailable":
            print(f"warning: '{h.name}' is marked unavailable -- {h.gates}\n", file=OUT)
        missing = _missing(root, h, builds.known)
        if missing and not args.skip_checks:
            outcomes[h.name] = Outcome(h.name, "SKIP", f"needs {', '.join(missing)}")
            print(
                f"--- {h.name}: skipped, needs {', '.join(missing)}",
                file=OUT,
                flush=True,
            )
            continue
        blocker = builds.blocker(h)
        if blocker is not None:
            outcomes[h.name] = Outcome(h.name, "FAIL", blocker)
            print(f"--- {h.name}: not run, {blocker}", file=OUT, flush=True)
            continue
        runnable.append(h)

    def one(h: hr.Harness, log) -> bool:
        ctest_args = passthrough if h.runner == "ctest" else []
        return _run_one(
            root, h, ctest_args, args.build_only, junit_dir, log, builds.known
        )

    parallel, alone = schedule(runnable)
    if args.jobs <= 1:
        parallel, alone = [], parallel + alone
    else:
        # Each test's output is held and printed whole when it finishes, so parallel runs do not interleave.
        def buffered(h: hr.Harness) -> tuple[bool, str]:
            with tempfile.TemporaryFile("w+", errors="replace") as log:
                ok = one(h, log)
                log.seek(0)
                return ok, log.read()

        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(buffered, h): h for h in parallel}
            for future in concurrent.futures.as_completed(futures):
                h = futures[future]
                ok, text = future.result()
                print(text, end="", file=OUT, flush=True)
                outcomes[h.name] = Outcome(h.name, "PASS" if ok else "FAIL")
    for h in alone:
        outcomes[h.name] = Outcome(h.name, "PASS" if one(h, OUT) else "FAIL")

    return _summarise(
        [outcomes[h.name] for h in selection], strict=args.strict or named, out=OUT
    )


def schedule(runnable: list[hr.Harness]) -> tuple[list[hr.Harness], list[hr.Harness]]:
    """`runnable` split into the tests run in parallel and those run alone after them, each in selection order."""
    return [h for h in runnable if not h.alone], [h for h in runnable if h.alone]


def default_jobs(jobs: int, tier: str, capture: bool) -> int:
    """The tests run at once: `jobs` when given, else one per CPU for a capture or the pr tier, else 1.

    Above the pr tier the corpus sweeps run their songs in parallel themselves (`GOLDEN_SWEEP_JOBS`)."""
    if jobs:
        return jobs
    return (os.cpu_count() or 1) if capture or tier == "pr" else 1


def _summarise(outcomes: list[Outcome], strict: bool, out) -> int:
    if len(outcomes) > 1:
        width = max(len(o.name) for o in outcomes)
        print("\n== summary", file=out)
        for o in outcomes:
            print(f"   {o.result}  {o.name:<{width}}  {o.note}".rstrip(), file=out)
    failed = [o for o in outcomes if o.result == "FAIL"]
    skipped = [o for o in outcomes if o.result == "SKIP"]
    passed = len(outcomes) - len(failed) - len(skipped)
    print(
        f"\n{passed} passed, {len(failed)} failed, {len(skipped)} skipped",
        file=out,
        flush=True,
    )
    if skipped and strict:
        print(
            "test: a skip counts as a failure here (--strict, or the test was named)",
            file=sys.stderr,
        )
    return 1 if failed or (skipped and strict) else 0


def cmd_capture(
    root: Path, entries: list[hr.Harness], label: str, tier: str, jobs: int
) -> int:
    """Run every gate above unit level and record what each printed, for comparing two trees.

    Gates run `jobs` at a time; the capture's files are written in registry order whatever order they finish in.
    """
    out = root / ".superpowers" / "estate" / label
    out.mkdir(parents=True, exist_ok=True)
    selection = [
        h
        for h in hr.select(entries, [], ["component", "integration"], tier)
        if h.status == "gate" and h.tier != "manual"
    ]

    # The products' own output goes to one log of its own: none of it is a gate's.
    with (out / "builds.log").open("w") as build_log:
        builds = Builds(root, selection, build_log)

    def capture(h: hr.Harness) -> int:
        missing = _missing(root, h, builds.known)
        blocker = builds.blocker(h)
        with (out / f"{h.name}.log").open("w") as log:
            if missing:
                log.write(f"skipped: needs {', '.join(missing)}\n")
                return 2
            if blocker is not None:
                log.write(f"not run: {blocker}\n")
                return 1
            return 0 if _run_one(root, h, [], False, None, log, builds.known) else 1

    codes: dict[str, int] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(jobs, 1)) as pool:
        futures = {pool.submit(capture, h): h for h in selection}
        for future in concurrent.futures.as_completed(futures):
            h = futures[future]
            codes[h.name] = future.result()
            print(f"== {h.name} rc={codes[h.name]}", flush=True)

    passed = sum(1 for h in selection if codes[h.name] == 0)
    with (out / "summary.txt").open("w") as summary:
        for h in selection:
            summary.write(f"{h.name} rc={codes[h.name]}\n")
        summary.write(f"passed {passed}/{len(selection)}\n")
    with (out / "markers.txt").open("w") as markers:
        for h in selection:
            for line in (
                (out / f"{h.name}.log").read_text(errors="replace").splitlines()
            ):
                if MARKER.match(line):
                    markers.write(f"{h.name}: {line}\n")
            markers.write(f"{h.name}: rc={codes[h.name]}\n")
    print(f"passed {passed}/{len(selection)}")
    return 0 if passed == len(selection) else 1


def cmd_list(
    root: Path, selection: list[hr.Harness], markdown: bool, entries: list[hr.Harness]
) -> int:
    if markdown:
        print(hr.markdown_table(entries), end="")
        return 0
    known = tb.products(root)
    width = max(len(h.name) for h in selection) if selection else 0
    for h in selection:
        missing = _missing(root, h, known)
        mark = "OK " if not missing else "-- "
        note = "" if not missing else f"  (needs: {', '.join(missing)})"
        print(
            f"{mark} {h.name:<{width}}  {h.level:<11} {h.status:<13} {h.tier:<7} {h.runtime}{note}"
        )
        print(f"    {' ' * width}  {h.gates}")
    return 0


def cmd_doctor(root: Path, entries: list[hr.Harness]) -> int:
    known = tb.products(root)
    needs = {
        h.name: set(h.requires)
        | {r for p in tb.order(h.builds, known) for r in known[p].requires}
        for h in entries
    }
    missing = sorted(r for r in set().union(*needs.values()) if not hr.probe(r, root))
    if not missing:
        print("All test prerequisites satisfied.")
        return 0
    print("Missing prerequisites:\n")
    for req in missing:
        blocked = [h.name for h in entries if req in needs[h.name]]
        print(f"  {req}")
        print(f"    fix:     {hr.PROBES[req]}")
        print(f"    blocks:  {', '.join(blocked)}\n")
    return 1


def cmd_clean(root: Path) -> int:
    """Remove every tree the runner builds: the ctest trees, the host-app objects and the shared cargo dir."""
    trees = sorted((root / "build").glob("test-*")) + [
        root / tb.HOST_APP_TREE,
        root / SHARED_TARGET_SUBDIR,
    ]
    removed = False
    for tree in trees:
        if tree.is_dir():
            shutil.rmtree(tree)
            print(f"removed {tree}")
            removed = True
    if not removed:
        print("nothing to remove")
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

    global OUT, ALONE_SWEEP_JOBS
    OUT = os.fdopen(os.dup(sys.stdout.fileno()), "w", buffering=1, errors="replace")
    args.jobs = default_jobs(args.jobs, args.tier, bool(args.capture))

    # The corpus sweeps run songs in parallel (tests/golden/driver.py): a sweep run alongside other tests shares the
    # CPUs among them, one run alone has them all.
    cpus = os.cpu_count() or 1
    ALONE_SWEEP_JOBS = os.environ.get("GOLDEN_SWEEP_JOBS", str(cpus))
    os.environ.setdefault("GOLDEN_SWEEP_JOBS", str(max(1, cpus // args.jobs)))

    if args.clean:
        return cmd_clean(root)
    if args.doctor:
        return cmd_doctor(root, entries)
    if args.capture:
        return cmd_capture(
            root,
            entries,
            args.capture,
            "nightly" if args.tier == "pr" else args.tier,
            args.jobs,
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
