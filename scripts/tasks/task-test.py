#! /usr/bin/env python3
"""Run the tests declared in tests/registry.toml.

./dbt test                                   # the pr tier: what a pull request runs
./dbt test --level unit                      # one level (repeatable)
./dbt test --tier nightly                    # everything up to and including nightly
./dbt test pattern-load song-save-channel    # by name, whatever their tier
./dbt test unit -- -R mem_guard              # arguments after -- go to ctest
./dbt test --list [--markdown]               # the registry, with prerequisites resolved
./dbt test --list --verbose song-roundtrip   # a test's explanation, from the file that judges it
./dbt test --doctor                          # what is missing, and how to get it
./dbt test --capture <label>                 # run the estate and record its marker lines
./dbt test --ci-matrix --tier pr             # the selection by CI job, as JSON
./dbt test --update-readme                   # regenerate tests/README.md's table
./dbt test --update golden-synth             # re-record the named tests' baselines
./dbt test --clean                           # drop the shared cargo target dir
./dbt test --dump-expanded                   # every entry as the runner sees it (JSON)
"""

import argparse
import concurrent.futures
import dataclasses
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

import util

sys.path.insert(0, str(Path(__file__).parent))

import corpus_gate
import golden_records
import golden_update
import harness_registry as hr
import run_scratch
import scenario_gate
import test_builds as tb
import test_selection as ts

SHARED_TARGET_SUBDIR = tb.SHARED_TARGET_SUBDIR

# The runner's own output: the real stdout as it was at startup, which no gate check can swap out from under the
# summary. `main` binds it to a duplicate of file descriptor 1.
OUT = sys.stdout
# GOLDEN_SWEEP_JOBS for a test run alone: the caller's, else one per CPU.
ALONE_SWEEP_JOBS = str(os.cpu_count() or 1)


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="test",
        description="Run the tests declared in tests/registry.toml (default: the pr tier)",
    )
    parser.group = "Development"
    parser.add_argument(
        "--dump-expanded",
        action="store_true",
        help="Print every registry entry as the runner sees it, as JSON sorted by name (proves a registry rewrite)",
    )
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
        default=None,
        help="Run every test up to and including this tier (default pr)",
    )
    parser.add_argument(
        "--since",
        metavar="REF",
        help="Only the tier's tests the changes since REF's merge base reach, the working tree included (tests named "
        "run whatever changed; tests/README.md, Selecting by change)",
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
        "--verbose",
        action="store_true",
        help="With --list: each test's explanation too, from the file that judges it",
    )
    parser.add_argument(
        "--doctor", action="store_true", help="Report missing prerequisites"
    )
    parser.add_argument(
        "--update-readme",
        action="store_true",
        help="Regenerate the table in tests/README.md from the registry",
    )
    parser.add_argument(
        "--update",
        action="store_true",
        help="Re-record the baselines of the tests named, and only those: build, run each with DELUGE_GOLDEN_UPDATE=1, "
        "print what changed, then run it again as usual",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove every tree the runner builds (build/test-*, build/host-app, build/cargo-harness)",
    )
    parser.add_argument(
        "--ci-matrix",
        action="store_true",
        help="Print the selection as a CI matrix, one row per CI job (JSON)",
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
    # --prebuilt and --build-only contradict each other.
    built = parser.add_mutually_exclusive_group()
    built.add_argument(
        "-n",
        "--build-only",
        action="store_true",
        help="Build the selection without running it",
    )
    built.add_argument(
        "--prebuilt",
        action="store_true",
        help="Run against build products already built (CI's host-build job); a missing one fails its tests",
    )
    parser.add_argument(
        "--list-outputs",
        action="store_true",
        help="Print the files the selection's prebuilt jobs need (--prebuilt), relative to the repository root",
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
    # Only an entry that declares the corpus may boot the corpus fixture, whatever the caller's environment says.
    env.pop(hr.CORPUS_DECLARED, None)
    if hr.declares_corpus(h):
        env[hr.CORPUS_DECLARED] = "1"
    # Only `--update` re-records, and only for the test it is recording: never from the caller's environment.
    for name in (golden_update.SWITCH, golden_update.LOG):
        env.pop(name, None)
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

    def __init__(
        self, root: Path, selection: list[hr.Harness], log=None, prebuilt: bool = False
    ):
        self.root = root
        self.known = tb.products(root)
        self.log = log
        # Product name -> why it is unusable. Absent: built.
        self.broken: dict[str, str] = {}
        wanted = [name for h in selection for name in h.builds]
        if prebuilt:
            # Only what the tests name: a product's dependencies are inside its outputs.
            for name in dict.fromkeys(wanted):
                self._check_prebuilt(name)
            return
        for name in tb.order(wanted, self.known):
            self._build(name)

    def _check_prebuilt(self, name: str) -> None:
        try:
            outputs = tb.prebuilt_outputs([name], self.known, self.root)
        except ValueError as exc:
            self.broken[name] = f"cannot run prebuilt: {exc}"
            return
        absent = [str(p.relative_to(self.root)) for p in outputs if not p.is_file()]
        if absent:
            self.broken[name] = f"is not built (--prebuilt): {', '.join(absent)}"

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
    # The C++ compiler the build directory is configured with, resolved as its environment is (see _environment).
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
        # The specs are independent processes; a suite under qemu takes minutes one at a time.
        "--parallel",
        str(os.cpu_count() or 1),
        *h.ctest_args,
    ]
    if junit_dir is not None:
        run += ["--output-junit", str(junit_dir / f"{h.name}.xml")]
    steps.append(("run", run + passthrough))
    return steps


def _workdir(root: Path, h: hr.Harness) -> Path:
    """Where `h`'s commands run: a ctest tree's source, otherwise its path."""
    return root / (h.source if h.runner == "ctest" else h.path)


def _run_one(
    root: Path,
    h: hr.Harness,
    passthrough: list[str],
    build_only: bool,
    junit_dir: Path | None,
    log,
    products: dict[str, tb.Product],
    extra_env: dict[str, str] | None = None,
) -> bool:
    env = _environment(root, h, products)
    env.update(extra_env or {})
    if h.runner in ("scenario", "corpus"):
        if build_only:
            return True
        out = log if log is not None else sys.stdout
        if h.runner == "scenario":
            what, judge = f"GOLDEN_SCENARIO={h.scenario}", scenario_gate.run_gate
        else:
            what, judge = (
                str(hr.CORPUS_RUNNERS / f"{h.corpus_runner}_run.py"),
                corpus_gate.run_corpus_gate,
            )
        print(f"==> {h.name}: run: {what}", file=out, flush=True)
        # A cross-built harness sits under its target triple (golden-vt-render-arm sets it).
        binary = (
            Path(env["CARGO_TARGET_DIR"])
            / env.get("GOLDEN_VT_RENDER_TARGET", "")
            / tb.HARNESS_PROFILE
            / "golden_vt_render"
        )
        passed = judge(root, h, env, binary, out)
        if not passed:
            print(f"test: '{h.name}' failed", file=sys.stderr)
        return passed
    workdir = _workdir(root, h)
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
                # Only a buffered log needs stderr folded in; written straight through, it keeps its own.
                stderr=None if log is OUT else subprocess.STDOUT,
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

    builds = Builds(root, selection, prebuilt=args.prebuilt)
    outcomes: dict[str, Outcome] = {}
    runnable: list[hr.Harness] = []
    for h in selection:
        if h.status == "unavailable":
            print(f"warning: '{h.name}' is marked unavailable -- {h.gates}\n", file=OUT)
        # Prebuilt, a missing product is a failure whatever this machine lacks: it was to have been handed in.
        blocker = builds.blocker(h) if args.prebuilt else None
        missing = [] if blocker else _missing(root, h, builds.known)
        if missing and not args.skip_checks:
            outcomes[h.name] = Outcome(h.name, "SKIP", f"needs {', '.join(missing)}")
            print(
                f"--- {h.name}: skipped, needs {', '.join(missing)}",
                file=OUT,
                flush=True,
            )
            continue
        blocker = blocker or builds.blocker(h)
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
                sys.stdout.flush()
                print(text, end="", file=OUT, flush=True)
                outcomes[h.name] = Outcome(h.name, "PASS" if ok else "FAIL")
    for h in alone:
        outcomes[h.name] = Outcome(h.name, "PASS" if one(h, OUT) else "FAIL")

    if junit_dir is not None and not args.build_only:
        for h in selection:
            _write_junit(junit_dir / f"{h.name}.xml", outcomes[h.name])
    return _summarise(
        [outcomes[h.name] for h in selection], strict=args.strict or named, out=OUT
    )


def schedule(runnable: list[hr.Harness]) -> tuple[list[hr.Harness], list[hr.Harness]]:
    """`runnable` split into the tests run in parallel and those run alone after them, each in selection order."""
    return [h for h in runnable if not h.alone], [h for h in runnable if h.alone]


def update_refusal(args) -> str | None:
    """Why `--update` cannot run as asked, or None. It re-records exactly the tests named, from what it has just
    built, one at a time."""
    if not args.names:
        return "--update re-records the tests it names and nothing else: ./dbt test --update <name>..."
    if args.tier is not None or args.level:
        return "--update takes test names, never --tier or --level: a tier re-recorded by accident is what it refuses"
    if args.prebuilt or args.build_only:
        return "--update always builds first: a baseline recorded from a stale binary records the stale binary"
    if args.jobs not in (0, 1):
        return "--update runs one test at a time"
    modes = {
        "--list": args.list,
        "--markdown": args.markdown,
        "--doctor": args.doctor,
        "--capture": args.capture,
        "--ci-matrix": args.ci_matrix,
        "--list-outputs": args.list_outputs,
        "--update-readme": args.update_readme,
        "--clean": args.clean,
        "--junit": args.junit,
        "--skip-checks": args.skip_checks,
    }
    clash = [flag for flag, on in modes.items() if on]
    return f"--update does not combine with {', '.join(clash)}" if clash else None


def owners_refusal(selection: list[hr.Harness]) -> str | None:
    """Why `--update` refuses some of `selection`: a variant copy only checks what its base records, and any other
    entry that lists no baseline has nothing to re-record."""
    why = [
        f"{h.name} is a variant of {h.variant_of}: it checks, and ./dbt test --update {h.variant_of} re-records"
        for h in selection
        if h.variant_of
    ]
    if none := [h.name for h in selection if not h.baseline and not h.variant_of]:
        why.append(f"{', '.join(none)} list(s) no baseline in the registry")
    return f"--update: {'; '.join(why)}" if why else None


def mode_refusal(args, passthrough: list[str]) -> str | None:
    """Why the options asked for do not combine, or None: --verbose belongs to --list, and --dump-expanded prints
    every entry whatever else is asked."""
    if args.verbose and not args.list:
        return "--verbose adds each test's explanation to --list: ./dbt test --list --verbose <name>..."
    if not args.dump_expanded:
        return None
    # Every option's flag is its dest spelled with dashes; the positional names are the one without a flag.
    defaults = vars(argparser().parse_args([]))
    clash = [
        "test names" if key == "names" else "--" + key.replace("_", "-")
        for key, value in vars(args).items()
        if key != "dump_expanded" and value != defaults[key]
    ]
    if passthrough:
        clash.append("arguments after --")
    return (
        f"--dump-expanded prints every entry, so it takes no other option or test name: {', '.join(clash)}"
        if clash
        else None
    )


def _under(rel: str, paths: list[str]) -> bool:
    """Is repository path `rel` one of `paths`, or inside one of them?"""
    return any(rel == p or rel.startswith(p.rstrip("/") + "/") for p in paths)


def baseline_snapshot(root: Path, paths: list[str]) -> dict[str, bytes]:
    """Every file under `paths` (files or directories, relative to `root`), with what it holds."""
    found: dict[str, bytes] = {}
    for rel in paths:
        path = root / rel
        files = (
            sorted(p for p in path.rglob("*") if p.is_file())
            if path.is_dir()
            else [path]
            if path.is_file()
            else []
        )
        for f in files:
            if "__pycache__" not in f.parts:
                found[str(f.relative_to(root))] = f.read_bytes()
    return found


def changed_files(before: dict[str, bytes], after: dict[str, bytes]) -> list[str]:
    return sorted(
        rel for rel in set(before) | set(after) if before.get(rel) != after.get(rel)
    )


def restore(root: Path, before: dict[str, bytes], after: dict[str, bytes]) -> list[str]:
    """Put every file that differs between the snapshots back as `before` holds it (removing one it lacks)."""
    reverted = changed_files(before, after)
    for rel in reverted:
        path = root / rel
        if rel in before:
            golden_update.atomic_write(path, before[rel])
        else:
            path.unlink(missing_ok=True)
    return reverted


def worktree_state(root: Path) -> dict[str, str]:
    """Each path `git status` reports (changed or untracked; ignored ones aside), with a digest of what it holds."""
    out = subprocess.run(
        ["git", "status", "--porcelain=v1", "-z", "--untracked-files=all"],
        cwd=root,
        capture_output=True,
        check=True,
    ).stdout.decode(errors="replace")
    fields = out.split("\0")
    state: dict[str, str] = {}
    i = 0
    while i < len(fields):
        entry = fields[i]
        i += 1
        if not entry:
            continue
        if "R" in entry[:2] or "C" in entry[:2]:
            i += 1  # a rename or copy names its source next
        path = root / entry[3:]
        state[entry[3:]] = (
            hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else "-"
        )
    return state


def strays(
    before: dict[str, str], after: dict[str, str], paths: list[str]
) -> list[str]:
    """The worktree paths outside `paths` whose state changed between two worktree_state() calls."""
    return sorted(
        rel
        for rel in set(before) | set(after)
        if before.get(rel) != after.get(rel) and not _under(rel, paths)
    )


def evidence(
    root: Path, lines: list[str], changed: list[str]
) -> tuple[list[str], list[str]]:
    """The log's GOLDEN-UPDATED lines with paths made relative to `root` (`GOLDEN-UPDATED none` when there are none),
    and what is wrong with them: a baseline file that changed with no line naming it, or a line naming one that did
    not change."""
    prefix = f"{root}/"
    tag = golden_update.TAG
    updated = [line.replace(prefix, "") for line in lines if line.startswith(tag + " ")]
    named = {golden_update.item_file(line) for line in updated}
    problems = [
        f"{rel} changed, and no {tag} line says so"
        for rel in changed
        if rel not in named
    ]
    problems += [
        f"a {tag} line names {rel}, which did not change"
        for rel in sorted(named - set(changed))
    ]
    return (updated or [f"{tag} none"]), problems


# What putting a file back can fail with; each is reported, and the rest are still attempted.
RESTORE_ERRORS = (OSError, subprocess.SubprocessError)


def existing_dirs(root: Path) -> set[str]:
    """Every directory of the worktree (`.` for the root), ignored trees and .git aside: a stray is never under one,
    and put_back_strays() removes only the directories missing here."""
    out = subprocess.run(
        [
            "git",
            "ls-files",
            "-z",
            "--others",
            "--ignored",
            "--exclude-standard",
            "--directory",
        ],
        cwd=root,
        capture_output=True,
        check=True,
    ).stdout.decode(errors="replace")
    skip = {root / ".git"} | {
        root / e.rstrip("/") for e in out.split("\0") if e.endswith("/")
    }
    found: set[str] = set()
    for dirpath, dirnames, _ in os.walk(root):
        dirnames[:] = [d for d in dirnames if Path(dirpath, d) not in skip]
        found.add(os.path.relpath(dirpath, root))
    return found


class StrayKeep:
    """Where put_back_strays() copies what a stray holds before putting it back, so a write the run should not have
    made can still be read: one directory per update run under the system temp directory (not the run's own, which goes
    when the run ends), made on first use and never removed."""

    def __init__(self) -> None:
        self.dir: Path | None = None

    def save(self, root: Path, rel: str) -> Path | None:
        """Copy `rel` (relative to `root`) into the kept directory; None when there is no file to copy."""
        src = root / rel
        if not (src.is_file() or src.is_symlink()):
            return None
        if self.dir is None:
            self.dir = Path(
                tempfile.mkdtemp(
                    prefix="dbt-test-update-strays.", dir=run_scratch.kept_root()
                )
            )
        dst = self.dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst, follow_symlinks=False)
        return dst


def put_back_strays(
    root: Path,
    before: dict[str, str],
    after: dict[str, str],
    paths: list[str],
    dirs: set[str],
    keep: StrayKeep,
) -> tuple[
    list[tuple[str, Path | None]],
    list[str],
    list[tuple[str, OSError | subprocess.SubprocessError]],
]:
    """Undo the run's writes outside `paths`, given worktree_state() before and after it and existing_dirs() before
    it: a file that was clean takes its committed content again, a file that did not exist is deleted, with each
    directory the run made for it. What the run left in each is first copied with `keep`; one that cannot be copied
    is not touched. A path git already reported before the run holds the caller's own change, which is never
    touched. Every path is attempted; returns those put back (each with where its copy went, None when the run
    deleted it), those left, and those that could not be."""
    reverted: list[tuple[str, Path | None]] = []
    left: list[str] = []
    failed: list[tuple[str, OSError | subprocess.SubprocessError]] = []
    for rel in strays(before, after, paths):
        if rel in before:
            left.append(rel)
            continue
        try:
            saved = keep.save(root, rel)
            committed = (
                subprocess.run(
                    ["git", "cat-file", "-e", f"HEAD:{rel}"],
                    cwd=root,
                    capture_output=True,
                    check=False,
                ).returncode
                == 0
            )
            if committed:
                subprocess.run(
                    ["git", "restore", "--source=HEAD", "--worktree", "--", rel],
                    cwd=root,
                    check=True,
                )
            else:
                path = root / rel
                path.unlink(missing_ok=True)
                for parent in path.parents:
                    if (
                        os.path.relpath(parent, root) in dirs
                        or not parent.is_dir()
                        or any(parent.iterdir())
                    ):
                        break
                    parent.rmdir()
        except RESTORE_ERRORS as exc:
            failed.append((rel, exc))
            continue
        reverted.append((rel, saved))
    return reverted, left, failed


def restore_each(
    root: Path, before: dict[str, bytes], after: dict[str, bytes]
) -> list[tuple[str, OSError | subprocess.SubprocessError]]:
    """restore(), attempting every file whatever fails; prints each put back, returns those that could not be."""
    failed: list[tuple[str, OSError | subprocess.SubprocessError]] = []
    for rel in changed_files(before, after):
        try:
            restore(
                root,
                {rel: before[rel]} if rel in before else {},
                {rel: after[rel]} if rel in after else {},
            )
        except RESTORE_ERRORS as exc:
            failed.append((rel, exc))
            continue
        print(f"GOLDEN-UPDATE-REVERTED {rel}", file=OUT)
    return failed


def _put_back(
    root: Path,
    h: hr.Harness,
    before: dict[str, bytes],
    tree_before: dict[str, str] | None = None,
    dirs: set[str] | None = None,
    keep: StrayKeep | None = None,
) -> list[tuple[str, OSError | subprocess.SubprocessError]]:
    """Put `h`'s baselines back as `before` holds them and, given the worktree before its run, undo its writes outside
    them (copying each first with `keep`). Attempts everything, says what was done and what could not be, and returns
    what could not be, each with its error."""
    failed: list[tuple[str, OSError | subprocess.SubprocessError]] = []
    try:
        failed += restore_each(root, before, baseline_snapshot(root, h.baseline))
    except RESTORE_ERRORS as exc:
        failed.append((", ".join(h.baseline), exc))
    if tree_before is not None:
        try:
            reverted, left, stray_failed = put_back_strays(
                root,
                tree_before,
                worktree_state(root),
                h.baseline,
                dirs or set(),
                keep or StrayKeep(),
            )
            failed += stray_failed
            for rel, saved in reverted:
                note = f" (saved: {saved})" if saved else ""
                print(f"GOLDEN-UPDATE-REVERTED {rel}{note}", file=OUT)
            for rel in left:
                print(f"GOLDEN-UPDATE-LEFT {rel}", file=OUT)
        except RESTORE_ERRORS as exc:
            failed.append(("the files outside the baseline", exc))
    for rel, exc in failed:
        print(f"GOLDEN-UPDATE-UNRESTORED {h.name}: {rel}: {exc}", file=OUT)
    OUT.flush()
    return failed


def _record(
    root: Path,
    h: hr.Harness,
    passthrough: list[str],
    products: dict[str, tb.Product],
    before: dict[str, bytes],
    tree_before: dict[str, str],
) -> bool:
    """Run `h` re-recording and judge what it wrote; True when every change is kept."""
    scratch = Path(tempfile.mkdtemp(prefix=f"{h.name}.golden-update."))
    log = scratch / "update.log"
    log.touch()
    try:
        extra = {golden_update.SWITCH: "1", golden_update.LOG: str(log)}
        ok = _run_one(root, h, passthrough, False, None, OUT, products, extra_env=extra)
        lines = log.read_text(errors="replace").splitlines()
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    problems = [] if ok else ["the run failed, and a failed run records nothing"]
    if ok:
        foreign = sorted(
            {
                header
                for header, _ in golden_records.parse(lines)
                if not _under(header, h.baseline)
            }
        )
        problems += [
            f"{header} is not among {h.name}'s baselines" for header in foreign
        ]
        if not foreign:
            try:
                lines += golden_records.apply(root, lines)
            except (OSError, ValueError) as exc:
                problems.append(str(exc))
    after = baseline_snapshot(root, h.baseline)
    problems += [
        f"{rel} changed, and it is not among {h.name}'s baselines"
        for rel in strays(tree_before, worktree_state(root), h.baseline)
    ]
    printed, wrong = evidence(root, lines, changed_files(before, after))
    if ok:
        problems += wrong
    if problems:
        # What the run wrote is about to be put back: its lines are not a record of anything kept.
        tag = golden_update.TAG
        printed = [
            f"GOLDEN-UPDATE-DISCARDED {line[len(tag) + 1 :]}"
            for line in printed
            if line != f"{tag} none"
        ]
    for line in printed:
        print(line, file=OUT)
    for problem in problems:
        print(f"GOLDEN-UPDATE-FAIL {h.name}: {problem}", file=OUT)
    OUT.flush()
    return not problems


def _update_one(
    root: Path,
    h: hr.Harness,
    passthrough: list[str],
    products: dict[str, tb.Product],
    keep: StrayKeep | None = None,
) -> tuple[dict[str, bytes] | None, list[str]]:
    """Run `h` once re-recording, print what changed, and keep it only if the run passed and every change is one of
    its baselines and was said; otherwise put back the baselines and whatever else it wrote (each write outside the
    baselines copied first with `keep`). Returns the baselines as they were before (for a later restore), or None when
    nothing was kept, with the files that could not be put back.

    The log, and the locks golden_update keeps beside it, live in a directory of their own outside the repository,
    so recording leaves nothing in the worktree but the baselines. An interrupt (Ctrl-C) mid-write puts everything
    back too, so no baseline is left half written."""
    print(f"==> {h.name}: re-recording {', '.join(h.baseline)}", file=OUT, flush=True)
    before = baseline_snapshot(root, h.baseline)
    tree_before = worktree_state(root)
    dirs = existing_dirs(root)
    keep = keep or StrayKeep()
    try:
        kept = _record(root, h, passthrough, products, before, tree_before)
    except BaseException as exc:
        # The interrupt is what is raised; a restore that failed on the way rides along as its cause.
        errors = _put_back(root, h, before, tree_before, dirs, keep)
        if errors and exc.__cause__ is None:
            raise exc from errors[0][1]
        raise
    if not kept:
        failed = _put_back(root, h, before, tree_before, dirs, keep)
        return None, [rel for rel, _ in failed]
    return before, []


def _unrestored(n: int) -> str:
    """The outcome note for `n` files an update could not put back."""
    return f"{n} file(s) not put back (see GOLDEN-UPDATE-UNRESTORED)"


def _overlap(a: str, b: str) -> bool:
    """Whether baseline paths `a` and `b` are one file or one contains the other."""
    a, b = a.rstrip("/"), b.rstrip("/")
    return a == b or a.startswith(b + "/") or b.startswith(a + "/")


def baseline_groups(selection: list[hr.Harness]) -> list[list[hr.Harness]]:
    """The selection split into groups whose baselines overlap (transitively), each in selection order, the groups in
    the order of their first member. Entries with disjoint baselines are groups of one."""
    groups: list[list[hr.Harness]] = []
    for h in selection:
        hit = [
            g
            for g in groups
            if any(_overlap(p, q) for m in g for p in m.baseline for q in h.baseline)
        ]
        merged = [h]
        for g in hit:
            groups.remove(g)
            merged = g + merged
        groups.append(sorted(merged, key=selection.index))
    return sorted(groups, key=lambda g: selection.index(g[0]))


def cmd_update(root: Path, selection: list[hr.Harness], passthrough: list[str]) -> int:
    """Re-record each test's baselines, one test at a time; then build again and run each as usual against what it
    recorded. Tests whose baselines overlap form a group that is kept all or nothing: the group's files are snapshotted
    once, before its first record run, and any member's failure (record, confirm or interrupt) puts every file of the
    group back and reports every member as not re-recorded. An interrupt anywhere puts back every group not yet
    wholly confirmed."""
    if why := owners_refusal(selection):
        print(f"test: {why}", file=sys.stderr)
        return 2
    builds = Builds(root, selection)
    outcomes: dict[str, Outcome] = {}
    groups = baseline_groups(selection)
    group_of = {h.name: g for g in groups for h in g}

    def union(g: list[hr.Harness]) -> hr.Harness:
        paths = list(dict.fromkeys(p for h in g for p in h.baseline))
        return dataclasses.replace(g[0], baseline=paths)

    snapshots: dict[int, dict[str, bytes]] = {}
    confirmed_groups: set[int] = set()
    rolled_back: dict[int, str] = {}
    unrestored: dict[int, int] = {}
    keep = StrayKeep()

    def roll_back(g: list[hr.Harness], why: str, earlier: int = 0) -> list:
        """Put `g`'s files back; `earlier` counts files outside them a member's own put-back could not restore."""
        key = id(g)
        failed = _put_back(root, union(g), snapshots[key]) if key in snapshots else []
        rolled_back.setdefault(key, why)
        unrestored[key] = unrestored.get(key, 0) + earlier + len(failed)
        return [exc for _, exc in failed]

    def restored(g: list[hr.Harness]) -> str:
        n = unrestored.get(id(g), 0)
        return _unrestored(n) if n else "restored"

    recorded: list[hr.Harness] = []
    try:
        for h in selection:
            g = group_of[h.name]
            if id(g) in rolled_back:
                continue
            missing = _missing(root, h, builds.known)
            why = f"needs {', '.join(missing)}" if missing else builds.blocker(h)
            if why:
                print(f"--- {h.name}: not re-recorded, {why}", file=OUT, flush=True)
                roll_back(g, f"{h.name} could not be re-recorded")
                n = unrestored[id(g)]
                outcomes[h.name] = Outcome(
                    h.name, "FAIL", f"{why}; {_unrestored(n)}" if n else why
                )
                continue
            if id(g) not in snapshots:
                snapshots[id(g)] = baseline_snapshot(root, union(g).baseline)
            before, failed = _update_one(
                root, h, passthrough if h.runner == "ctest" else [], builds.known, keep
            )
            if before is None:
                # The group's put-back retries its baselines; what is left of the member's own is outside them.
                outside = [rel for rel in failed if not _under(rel, union(g).baseline)]
                roll_back(g, f"{h.name} recorded nothing", len(outside))
                n = unrestored[id(g)]
                outcomes[h.name] = Outcome(
                    h.name,
                    "FAIL",
                    f"nothing re-recorded; {_unrestored(n)}"
                    if n
                    else "nothing re-recorded",
                )
            else:
                recorded.append(h)
        recorded = [h for h in recorded if id(group_of[h.name]) not in rolled_back]
        # Built again: a record header is compiled into what checks it.
        confirm = Builds(root, recorded)
        passed_names: set[str] = set()
        for h in recorded:
            g = group_of[h.name]
            if id(g) in rolled_back:
                continue
            print(f"==> {h.name}: the new baseline, run as usual", file=OUT, flush=True)
            passed = confirm.blocker(h) is None and _run_one(
                root,
                h,
                passthrough if h.runner == "ctest" else [],
                False,
                None,
                OUT,
                confirm.known,
            )
            if passed:
                passed_names.add(h.name)
                if all(m.name in passed_names for m in g):
                    confirmed_groups.add(id(g))
            else:
                roll_back(g, f"{h.name} fails a second run")
                outcomes[h.name] = Outcome(
                    h.name,
                    "FAIL",
                    f"the new baseline fails a second run; {restored(g)}",
                )
        for h in selection:
            if h.name in outcomes:
                continue
            why = rolled_back.get(id(group_of[h.name]))
            outcomes[h.name] = (
                Outcome(
                    h.name,
                    "FAIL",
                    f"not re-recorded: {why}; {restored(group_of[h.name])}",
                )
                if why
                else Outcome(h.name, "PASS", "")
            )
    except BaseException as exc:
        errors: list[OSError | subprocess.SubprocessError] = []
        for g in groups:
            if id(g) in snapshots and id(g) not in confirmed_groups:
                errors += roll_back(g, "interrupted")
        if errors and exc.__cause__ is None:
            raise exc from errors[0]
        raise
    return _summarise([outcomes[h.name] for h in selection], strict=True, out=OUT)


def default_jobs(jobs: int, tier: str, capture: bool) -> int:
    """The tests run at once: `jobs` when given, else one per CPU for a capture or the pr tier, else 1.

    Above the pr tier the corpus sweeps run their songs in parallel themselves (`GOLDEN_SWEEP_JOBS`)."""
    if jobs:
        return jobs
    return (os.cpu_count() or 1) if capture or tier == "pr" else 1


def _write_junit(path: Path, outcome: Outcome) -> None:
    """A one-case JUnit file for a test that wrote none of its own, so every test is reported by name."""
    if path.exists():
        return
    suite = ET.Element(
        "testsuite",
        name=outcome.name,
        tests="1",
        failures=str(int(outcome.result == "FAIL")),
        skipped=str(int(outcome.result == "SKIP")),
    )
    case = ET.SubElement(suite, "testcase", classname="dbt test", name=outcome.name)
    if outcome.result == "FAIL":
        ET.SubElement(case, "failure", message=outcome.note or "failed")
    elif outcome.result == "SKIP":
        ET.SubElement(case, "skipped", message=outcome.note)
    ET.ElementTree(suite).write(path, encoding="utf-8", xml_declaration=True)


def _summarise(outcomes: list[Outcome], strict: bool, out) -> int:
    sys.stdout.flush()
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
                if scenario_gate.MARKER.match(line):
                    markers.write(f"{h.name}: {line}\n")
            markers.write(f"{h.name}: rc={codes[h.name]}\n")
    print(f"passed {passed}/{len(selection)}")
    return 0 if passed == len(selection) else 1


def cmd_list(
    root: Path,
    selection: list[hr.Harness],
    markdown: bool,
    entries: list[hr.Harness],
    verbose: bool = False,
    reasons: dict[str, str] | None = None,
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
        if hr.local_only(h):
            note += "  [local-only: not run in CI]"
        print(
            f"{mark} {h.name:<{width}}  {h.level:<11} {h.status:<13} {h.tier:<7} {hr.runtime_text(h.runtime_s)}{note}"
        )
        print(f"    {' ' * width}  {h.gates}")
        if reasons and (why := reasons.get(h.name)):
            print(f"    {' ' * width}  reached by {why}")
        if verbose and (text := hr.explanation(root, h)):
            for line in textwrap.wrap(
                text,
                width=max(40, 116 - width),
                break_on_hyphens=False,
                break_long_words=False,
            ):
                print(f"    {' ' * width}  {line}")
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
        print(
            f"  {req}"
            + ("   (local-only: CI never has it)" if req in hr.LOCAL_ONLY else "")
        )
        print(f"    fix:     {hr.PROBES[req]}")
        print(f"    blocks:  {', '.join(blocked)}\n")
    return 1


def cmd_update_readme(root: Path, entries: list[hr.Harness]) -> int:
    path = root / "tests" / "README.md"
    old = path.read_text()
    new = hr.with_table(old, hr.markdown_table(entries))
    if new != old:
        path.write_text(new)
        print(f"updated {path.relative_to(root)}")
    return 0


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
    # Every temporary file of the run, its children's included, goes in one directory removed when it ends.
    with run_scratch.run_directory():
        return _main(sys.argv[1:] if argv is None else argv)


def _main(argv: list[str]) -> int:
    passthrough: list[str] = []
    if "--" in argv:
        cut = argv.index("--")
        argv, passthrough = argv[:cut], argv[cut + 1 :]
    args = argparser().parse_args(argv)
    for name in (golden_update.SWITCH, golden_update.LOG):
        if os.environ.pop(name, None) is not None:
            print(
                f"test: {name} in the environment is ignored: ./dbt test --update <name> sets it",
                file=sys.stderr,
            )
    if why := mode_refusal(args, passthrough):
        print(f"test: {why}", file=sys.stderr)
        return 2
    if args.update and (why := update_refusal(args)):
        print(f"test: {why}", file=sys.stderr)
        return 2
    tier = args.tier or "pr"
    root = Path(util.get_git_root()).absolute()
    entries = _load(root)
    if args.dump_expanded:
        print(hr.dump_expanded(entries), end="")
        return 0

    global OUT, ALONE_SWEEP_JOBS
    OUT = os.fdopen(os.dup(sys.stdout.fileno()), "w", buffering=1, errors="replace")
    # `--update` runs one test at a time, so a corpus sweep it records has every CPU.
    args.jobs = 1 if args.update else default_jobs(args.jobs, tier, bool(args.capture))

    # The corpus sweeps run songs in parallel (tests/integration/corpus/driver.py): a sweep run alongside other tests
    # shares the CPUs among them, one run alone has them all.
    cpus = os.cpu_count() or 1
    ALONE_SWEEP_JOBS = os.environ.get("GOLDEN_SWEEP_JOBS", str(cpus))
    os.environ.setdefault("GOLDEN_SWEEP_JOBS", str(max(1, cpus // args.jobs)))

    if args.update_readme:
        return cmd_update_readme(root, entries)
    if args.clean:
        return cmd_clean(root)
    if args.doctor:
        return cmd_doctor(root, entries)
    if args.capture:
        return cmd_capture(
            root,
            entries,
            args.capture,
            "nightly" if tier == "pr" else tier,
            args.jobs,
        )

    try:
        selection = hr.select(entries, args.names, args.level, tier)
    except ValueError as exc:
        print(f"test: {exc}", file=sys.stderr)
        return 1
    reasons: dict[str, str] = {}
    if args.since and not args.names:
        try:
            result = ts.since(
                selection,
                hr.load_areas(root),
                ts.changed_files(root, args.since),
                tier,
            )
        except ValueError as exc:
            print(f"test: {exc}", file=sys.stderr)
            return 2
        for line in result.messages:
            print(line, file=sys.stderr)
        selection, reasons = result.selected, result.reasons
        if not selection:
            print(f"--since: no gate is reachable from the changes since {args.since}")
            return 0
    if args.update:
        return cmd_update(root, selection, passthrough)

    if args.list_outputs:
        names = [name for h in selection if hr.JOBS[h.job] for name in h.builds]
        try:
            outputs = tb.prebuilt_outputs(names, tb.products(root), root)
        except ValueError as exc:
            print(f"test: {exc}", file=sys.stderr)
            return 1
        for path in outputs:
            print(path.relative_to(root))
        return 0
    if args.ci_matrix:
        print(json.dumps(hr.ci_matrix(selection)))
        return 0
    if args.list:
        return cmd_list(
            root,
            selection,
            args.markdown,
            entries,
            verbose=args.verbose,
            reasons=reasons,
        )
    return cmd_run(root, selection, args, passthrough, named=bool(args.names))


if __name__ == "__main__":
    sys.exit(main())
