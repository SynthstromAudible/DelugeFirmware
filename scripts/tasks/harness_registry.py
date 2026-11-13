#! /usr/bin/env python3
"""Parse tests/registry.toml and probe each test's prerequisites.

Split from task-test.py so the parsing, selection and probing logic is
unit-testable without invoking dbt or spawning a build. See
test_harness_registry.py.
"""

import dataclasses
import json
import os
import re
import shutil
from dataclasses import dataclass, field
from pathlib import Path

import test_builds
import tomllib

STATUSES = ("gate", "investigative", "unavailable")
LEVELS = ("unit", "component", "integration")
RUNNERS = ("ctest", "cargo", "scenario", "corpus", "script")
# Where a corpus gate's runner lives: `corpus_runner = "<name>"` runs <CORPUS_RUNNERS>/<name>_run.py.
CORPUS_RUNNERS = Path("tests/golden")
# A corpus gate's runner names its scenario and judges its own runs, so these mean nothing on one.
SCENARIO_ONLY = ("scenario", "markers", "result", "check")
# Cumulative, in this order: selecting a tier selects every tier before it too.
TIERS = ("pr", "nightly", "manual")
# The build products that produce golden_vt_render; a scenario gate names one.
SCENARIO_RENDERERS = (
    "golden-vt-render",
    "golden-vt-render-arm",
    "golden-vt-render-asan",
)
# CI runs each group as one job. A group naming a product runs against that product built once by an earlier job
# (`dbt test --prebuilt`), so its tests may build nothing else; a group naming None builds its own.
GROUPS = {
    "render": "golden-vt-render",
    "app": "golden-vt-render",
    "model": "golden-vt-render",
    "storage": None,
    "unit": None,
    # The C++ ctest trees, apart from `unit`: built cold, they and the cargo builds overran one job's time limit.
    # tests/unit alone is most of that compile, so it has a job to itself.
    "unit-cpp": None,
    "unit-cpp-variants": None,
    "tooling": None,
}

# Requirement key -> how to satisfy it. Every key a registry entry may name must
# appear here; `dbt test --doctor` prints the value when the probe fails.
PROBES = {
    "cargo": "install a Rust toolchain (rustup); rust-toolchain.toml pins the channel",
    "cargo-nextest": "cargo install cargo-nextest (or taiki-e/install-action@nextest in CI)",
    "cargo-fuzz": "cargo install cargo-fuzz, and a nightly toolchain",
    "cmake": "install cmake and ninja",
    "clang": "install clang -- the host app is built with it, as the firmware is",
    "clang-tsan": "install clang (ThreadSanitizer needs clang or a TSan-capable GCC)",
    "clang-asan": "install clang with its compiler-rt AddressSanitizer runtime (libclang_rt.asan)",
    "mtools": "apt install mtools -- fs_differential's fixtures are built with mformat",
    "qemu-arm": "apt install qemu-user",
    "llvm-objdump": "install llvm (llvm-objdump disassembles the fixed-point codegen probe)",
    "arm-linux-gnueabihf-gcc": (
        "apt install g++-arm-linux-gnueabihf -- clang builds for arm-linux against its sysroot, /usr/arm-linux-gnueabihf"
    ),
    "arm-none-eabi-gcc": (
        "run ./dbt once (without DBT_NOENV) to fetch the pinned toolchain into toolchain/current, "
        "or point DBT_TOOLCHAIN_PATH at a directory holding one"
    ),
    "song-corpus": (
        "set DELUGE_BACKUP (default ~/Deluge Backup) or DELUGE_SONG_CORPUS to a directory of .XML songs, "
        "or have the golden fixtures already rebuilt in DELUGE_GOLDEN_DIR (default ~/.cache/deluge-golden)"
    ),
    "song-xml-corpus": (
        "set DELUGE_SONG_CORPUS to a directory of .XML songs, or have them in DELUGE_BACKUP's SONGS "
        "(default ~/Deluge Backup/SONGS) -- the songs themselves, not fixtures rebuilt from them"
    ),
    "legacy-corpus": (
        "set DELUGE_LEGACY_CORPUS to the SONGS folder of a card backup as the firmware wrote it (not upgraded), with "
        "that backup's SYNTHS folder beside it -- the upgrade differential upgrades the originals"
    ),
}

# Requirements CI never has: a test that needs one runs only on a developer's machine. `--ci-matrix` leaves such
# tests out, so a CI job never fails on a skip it cannot avoid; `--list` and `--doctor` still show them.
LOCAL_ONLY = frozenset({"song-corpus", "song-xml-corpus", "legacy-corpus"})


def local_only(h: "Harness") -> bool:
    """Does `h` need a requirement only a developer's machine has?"""
    return not LOCAL_ONLY.isdisjoint(h.requires)


# Set to "1" in the environment of a test whose entry requires song-corpus, and absent from every other test's:
# golden_vt_render refuses to boot the corpus fixture without it, so a test that boots it by mistake fails everywhere
# instead of passing where the corpus happens to be.
CORPUS_DECLARED = "DELUGE_CORPUS_DECLARED"
SCENARIO_TABLE = Path("tests/integration/scenarios/scenario_table.cpp")


def declares_corpus(h: "Harness") -> bool:
    """Does `h`'s entry declare that it needs the song corpus?"""
    return "song-corpus" in h.requires


def corpus_fixture_scenarios(root: Path) -> set[str]:
    """The scenario_table.cpp rows whose card is the corpus fixture (`kCorpusFixture`).

    A `fromEnv("VAR|corpus")` row reaches the corpus only when VAR is unset, which only the run knows;
    golden_vt_render's run-time refusal covers those.
    """
    lines = (root / SCENARIO_TABLE).read_text().splitlines()
    text = "\n".join(line for line in lines if not line.strip().startswith("//"))
    start = text.index("kScenarios[] = {")
    body = text[start : text.index("\n};", start)]
    return {
        row.split('"', 1)[0]
        for row in re.split(r'\n\s*\{"', body)[1:]
        if "kCorpusFixture" in row
    }


@dataclass
class Harness:
    name: str
    path: str
    level: str
    runner: str
    kind: str
    status: str
    gates: str
    tier: str
    build: list[str]
    run: list[str]
    requires: list[str]
    runtime: str
    # Set over the caller's environment.
    env: dict[str, str] = field(default_factory=dict)
    # Set only where the caller's environment does not already say.
    env_default: dict[str, str] = field(default_factory=dict)
    # runner = "ctest": the CMake source and build trees, extra configure arguments, extra ctest arguments.
    source: str = ""
    tree: str = ""
    configure: list[str] = field(default_factory=list)
    ctest_args: list[str] = field(default_factory=list)
    group: str = ""
    # A JUnit file the run leaves behind, relative to the repo root; `${CARGO_TARGET_DIR}` is expanded.
    junit: str = ""
    # Build products (test_builds.py) the runner makes before this test runs.
    builds: list[str] = field(default_factory=list)
    # runner = "scenario": the GOLDEN_SCENARIO to run, the markers each run must print,
    # the whole line that says it passed, and a module (scenario_gate.py) with the gate's own checks.
    scenario: str | None = None
    markers: list[str] = field(default_factory=list)
    result: str = ""
    check: str = ""
    # runner = "corpus": the stem of the tests/golden runner it runs (<corpus_runner>_run.py).
    corpus_runner: str = ""
    # Run on its own after the parallel set: the gate measures real time, or loads the machine enough to distort others.
    alone: bool = False
    # The files and directories (repository paths) the test writes under `./dbt test --update`, and only they: the
    # runner snapshots them, restores them when the run fails, and fails a run that writes anywhere else.
    baseline: list[str] = field(default_factory=list)


def _runs_golden_vt_render(where: str, item: dict) -> None:
    """A scenario or corpus gate runs only the golden_vt_render `./dbt test` built for it."""
    if item.get("build") or item.get("run"):
        raise ValueError(
            f"{where}: a {item['runner']} gate is run by the runner, not by 'build'/'run'"
        )
    if not set(SCENARIO_RENDERERS) & set(item.get("builds", [])):
        raise ValueError(
            f"{where}: a {item['runner']} gate runs golden_vt_render, so builds must name one of "
            f"{', '.join(SCENARIO_RENDERERS)}"
        )


def load_registry(root: Path) -> list[Harness]:
    """Parse and validate tests/registry.toml under `root`."""
    raw = tomllib.loads((root / "tests" / "registry.toml").read_text())
    entries: list[Harness] = []
    seen: set[str] = set()

    for i, item in enumerate(raw.get("harness", [])):
        where = item.get("name", f"entry #{i}")

        for required in (
            "name",
            "path",
            "level",
            "runner",
            "kind",
            "status",
            "gates",
            "tier",
            "group",
        ):
            if not item.get(required):
                raise ValueError(f"{where}: missing required field '{required}'")

        for key, allowed in (
            ("status", STATUSES),
            ("level", LEVELS),
            ("runner", RUNNERS),
            ("tier", TIERS),
            ("group", tuple(GROUPS)),
        ):
            if item[key] not in allowed:
                raise ValueError(
                    f"{where}: {key} '{item[key]}' must be one of {allowed}"
                )

        if item["runner"] == "ctest":
            for required in ("source", "tree"):
                if not item.get(required):
                    raise ValueError(f"{where}: a ctest test needs '{required}'")
            if item.get("build") or item.get("run"):
                raise ValueError(
                    f"{where}: a ctest test is built and run from 'source' and 'tree', not 'build'/'run'"
                )
        elif item["runner"] == "scenario":
            if not item.get("scenario"):
                raise ValueError(
                    f"{where}: scenario must name a row of scenario_table.cpp"
                )
            if not item.get("markers") or not item.get("result"):
                raise ValueError(
                    f"{where}: a scenario gate names the markers each run must print and the result line that says "
                    "it passed"
                )
            _runs_golden_vt_render(where, item)
        elif item["runner"] == "corpus":
            runner = item.get("corpus_runner", "")
            if not runner:
                raise ValueError(
                    f"{where}: a corpus gate names its runner in 'corpus_runner'"
                )
            if not (root / CORPUS_RUNNERS / f"{runner}_run.py").is_file():
                raise ValueError(
                    f"{where}: corpus_runner '{runner}': no {CORPUS_RUNNERS / f'{runner}_run.py'}"
                )
            for name in SCENARIO_ONLY:
                if name in item:
                    raise ValueError(
                        f"{where}: a corpus gate's runner judges its own runs, so it takes no '{name}'"
                    )
            _runs_golden_vt_render(where, item)
        elif not item.get("run"):
            raise ValueError(f"{where}: missing required field 'run'")
        if item.get("corpus_runner") and item["runner"] != "corpus":
            raise ValueError(f"{where}: only a corpus gate names a corpus_runner")
        if item.get("check") and not (root / item["check"]).is_file():
            raise ValueError(f"{where}: check '{item['check']}' does not exist")

        prebuilt = GROUPS[item["group"]]
        if prebuilt and item.get("builds", []) != [prebuilt]:
            raise ValueError(
                f"{where}: group '{item['group']}' runs prebuilt, so a test in it builds only {prebuilt}"
            )
        # A script or build step could build at run time, which a prebuilt group must not.
        if prebuilt and item["runner"] not in ("scenario", "corpus"):
            raise ValueError(
                f"{where}: group '{item['group']}' runs prebuilt, so it holds only scenario and corpus gates"
            )

        known_products = test_builds.products(root)
        for product in item.get("builds", []):
            if product not in known_products:
                raise ValueError(
                    f"{where}: unknown build product '{product}'; add it to test_builds.py"
                )

        for req in item.get("requires", []):
            if req not in PROBES:
                raise ValueError(
                    f"{where}: unknown requirement '{req}'; add a probe to PROBES"
                )
        baseline = item.get("baseline", [])
        if not isinstance(baseline, list) or not all(
            isinstance(p, str)
            and p
            and not Path(p).is_absolute()
            and ".." not in Path(p).parts
            for p in baseline
        ):
            raise ValueError(
                f"{where}: baseline lists the files or directories the test re-records, each relative to the "
                "repository root"
            )
        for table in ("env", "env_default"):
            if CORPUS_DECLARED in item.get(table, {}):
                raise ValueError(
                    f'{where}: {table} may not set {CORPUS_DECLARED}; declare requires = ["song-corpus"] instead'
                )

        if item["name"] in seen:
            raise ValueError(f"duplicate test name '{item['name']}'")
        seen.add(item["name"])

        entries.append(
            Harness(
                name=item["name"],
                path=item["path"],
                level=item["level"],
                runner=item["runner"],
                kind=item["kind"],
                status=item["status"],
                gates=item["gates"],
                tier=item["tier"],
                build=item.get("build", []),
                run=item.get("run", []),
                requires=item.get("requires", []),
                runtime=item.get("runtime", "?"),
                env=item.get("env", {}),
                env_default=item.get("env_default", {}),
                source=item.get("source", ""),
                tree=item.get("tree", ""),
                configure=item.get("configure", []),
                ctest_args=item.get("ctest_args", []),
                junit=item.get("junit", ""),
                builds=item.get("builds", []),
                scenario=item.get("scenario"),
                markers=item.get("markers", []),
                result=item.get("result", ""),
                check=item.get("check", ""),
                corpus_runner=item.get("corpus_runner", ""),
                alone=item.get("alone", False),
                baseline=baseline,
                group=item["group"],
            )
        )

    return entries


def select(
    entries: list[Harness],
    names: list[str],
    levels: list[str],
    tier: str,
) -> list[Harness]:
    """The entries a run covers.

    Named entries are taken as asked for, whatever their tier or status. Otherwise every entry at `tier` or an
    earlier one, at one of `levels` (all levels when empty), that is not marked unavailable.
    """
    if names:
        by_name = {h.name: h for h in entries}
        unknown = [n for n in names if n not in by_name]
        if unknown:
            raise ValueError(f"unknown test(s): {', '.join(unknown)}")
        return [by_name[n] for n in names]

    within = TIERS[: TIERS.index(tier) + 1]
    return [
        h
        for h in entries
        if h.tier in within
        and (not levels or h.level in levels)
        and h.status != "unavailable"
    ]


def probe(requirement: str, root: Path) -> bool:
    """Is `requirement` satisfied on this machine?"""
    match requirement:
        case "cargo":
            return shutil.which("cargo") is not None
        case "cargo-nextest":
            return shutil.which("cargo-nextest") is not None
        case "cargo-fuzz":
            return shutil.which("cargo-fuzz") is not None
        case "cmake":
            return (
                shutil.which("cmake") is not None and shutil.which("ninja") is not None
            )
        case "clang" | "clang-tsan":
            return shutil.which("clang++") is not None
        case "clang-asan":
            return shutil.which("clang++") is not None and any(
                Path("/usr/lib").glob("clang/*/lib/*/libclang_rt.asan*")
            )
        case "mtools":
            return shutil.which("mformat") is not None
        case "qemu-arm":
            return shutil.which("qemu-arm") is not None
        case "llvm-objdump":
            return shutil.which("llvm-objdump") is not None
        case "arm-linux-gnueabihf-gcc":
            # What the arm-linux builds use of it: the sysroot's C library and libstdc++, which clang links against.
            return (Path("/usr/arm-linux-gnueabihf/include/stdio.h")).is_file()
        case "arm-none-eabi-gcc":
            # Where deluge_sample_convert's build.rs looks for the device compiler.
            base = Path(os.environ.get("DBT_TOOLCHAIN_PATH") or root)
            return (
                base / "toolchain/current/arm-none-eabi-gcc/bin/arm-none-eabi-g++"
            ).is_file()
        case "song-corpus":
            for var in ("DELUGE_SONG_CORPUS", "DELUGE_BACKUP"):
                value = os.environ.get(var)
                if value and Path(value).is_dir():
                    return True
            if (Path.home() / "Deluge Backup").is_dir():
                return True
            # The fixtures rebuilt from a backup earlier serve without it.
            golden = Path(
                os.environ.get(
                    "DELUGE_GOLDEN_DIR", Path.home() / ".cache/deluge-golden"
                )
            )
            return golden.is_dir() and any(golden.glob("*_proj"))
        case "song-xml-corpus":
            # Where tests/golden/driver.py's default_corpus() looks.
            candidates = [os.environ.get("DELUGE_SONG_CORPUS")]
            if os.environ.get("DELUGE_BACKUP"):
                candidates.append(str(Path(os.environ["DELUGE_BACKUP"]) / "SONGS"))
            candidates.append(str(Path.home() / "Deluge Backup" / "SONGS"))
            return any(c and Path(c).is_dir() for c in candidates)
        case "legacy-corpus":
            # What tests/golden/upgrade_run.py's corpus part reads: the original songs, and the factory presets'
            # originals in the SYNTHS folder beside them.
            value = os.environ.get("DELUGE_LEGACY_CORPUS")
            return (
                bool(value)
                and Path(value).is_dir()
                and (Path(value).parent / "SYNTHS").is_dir()
            )
        case _:
            return False


def ci_matrix(selection: list[Harness]) -> dict:
    """The CI matrix for `selection`: one entry per group that has a selected test, in GROUPS order.

    Local-only tests (LOCAL_ONLY) are left out.
    """
    selection = [h for h in selection if not local_only(h)]
    return {
        "include": [
            {
                "group": group,
                "tests": " ".join(h.name for h in selection if h.group == group),
                "prebuilt": prebuilt or "",
            }
            for group, prebuilt in GROUPS.items()
            if any(h.group == group for h in selection)
        ],
    }


def markdown_table(harnesses: list[Harness]) -> str:
    """The generated table for tests/README.md."""
    lines = [
        "| Test | Level | Runner | Kind | Status | Gates | Tier | Group | Runtime |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for h in harnesses:
        # Escape pipes in fields to preserve table structure
        escaped_gates = h.gates.replace("|", "\\|")
        escaped_runtime = h.runtime.replace("|", "\\|")
        lines.append(
            f"| {h.name} | {h.level} | {h.runner} | {h.kind} | {h.status} | {escaped_gates} | {h.tier} | {h.group} "
            f"| {escaped_runtime} |"
        )
    return "\n".join(lines) + "\n"


TABLE_BEGIN = "<!-- BEGIN GENERATED TABLE -->"
TABLE_END = "<!-- END GENERATED TABLE -->"


def with_table(readme: str, table: str) -> str:
    """`readme` with the lines between the generated-table markers replaced by `table`."""
    begin = readme.find(TABLE_BEGIN)
    end = readme.find(TABLE_END)
    if begin < 0 or end < begin:
        raise ValueError(
            f"tests/README.md needs a '{TABLE_BEGIN}' line before a '{TABLE_END}' line"
        )
    return readme[: begin + len(TABLE_BEGIN)] + "\n" + table + readme[end:]


def dump_expanded(entries: list[Harness]) -> str:
    """Every entry as the runner sees it: a JSON list sorted by name, each entry's keys sorted.

    Two registries whose dumps are equal declare the same tests, so a rewrite of the registry is proved by mapping
    the dump from before it onto the dump after.
    """
    rows = sorted((dataclasses.asdict(h) for h in entries), key=lambda row: row["name"])
    return json.dumps(rows, indent=1, sort_keys=True, ensure_ascii=False) + "\n"


__all__ = [
    "CORPUS_DECLARED",
    "CORPUS_RUNNERS",
    "GROUPS",
    "LEVELS",
    "LOCAL_ONLY",
    "PROBES",
    "RUNNERS",
    "SCENARIO_TABLE",
    "TABLE_BEGIN",
    "TABLE_END",
    "TIERS",
    "Harness",
    "ci_matrix",
    "corpus_fixture_scenarios",
    "declares_corpus",
    "dump_expanded",
    "load_registry",
    "local_only",
    "markdown_table",
    "probe",
    "select",
    "with_table",
]
