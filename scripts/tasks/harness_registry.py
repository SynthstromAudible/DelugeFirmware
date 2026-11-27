#! /usr/bin/env python3
"""Parse tests/registry.toml and probe each test's prerequisites.

Split from task-test.py so the parsing, selection and probing logic is
unit-testable without invoking dbt or spawning a build. See
test_harness_registry.py.
"""

import ast
import dataclasses
import importlib.util
import json
import os
import re
import shutil
from dataclasses import dataclass, field
from pathlib import Path

import test_builds
import test_selection as ts
import tomllib

STATUSES = ("gate", "investigative", "unavailable")
LEVELS = ("unit", "component", "integration")
RUNNERS = ("ctest", "cargo", "scenario", "corpus", "script")
# Where a corpus gate's runner lives: `corpus_runner = "<name>"` runs <CORPUS_RUNNERS>/<name>_run.py.
CORPUS_RUNNERS = Path("tests/integration/corpus")
# Every baseline lives under GOLDEN_ROOT, in the directory of what reads it (tests/golden/README.md lists them).
GOLDEN_ROOT = Path("tests/golden")
# A record header is compiled into the spec that checks it, so it stays beside that spec. The loader accepts one by
# its name alone: any tests/**/*_records.h.
RECORD_HEADER_SUFFIX = "_records.h"
# The menu tree is a website build input; the ui-menu-dump gate keeps it equal to the app's.
BASELINES_ELSEWHERE = ("website/src/data/generated/menu-tree.json",)
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
# CI runs each job as one GitHub job. A job naming a product runs against that product built once by an earlier job
# (`dbt test --prebuilt`), so its tests may build nothing else; a job naming None builds its own.
JOBS = {
    "render": "golden-vt-render",
    "app": "golden-vt-render",
    "model": "golden-vt-render",
    "storage": None,
    "unit": None,
    # The C++ ctest trees, apart from `unit`: built cold, they and the cargo builds overran one job's time limit.
    # The host tree (`unit`) alone is most of that compile, so it has a job to itself.
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
    "llvm-cxxfilt": "install llvm (llvm-cxxfilt demangles the symbols the codegen comparison keys by)",
    "llvm-objcopy": "install llvm (llvm-objcopy reads the loaded data the codegen and string comparisons compare)",
    "llvm-readelf": "install llvm (llvm-readelf lists the sections and segments the image comparisons read)",
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
    "delugemu": "build delugemu (scripts/build.sh) in DELUGEMU_ROOT (default ~/GitHub/delugemu)",
    "parity-ref": (
        "build main with ./dbt build RelWithDebInfo and put its deluge.elf at DELUGE_PARITY_REF (default "
        ".superpowers/estate/emu/main.elf)"
    ),
    "content-card": (
        "put the content cards (directories and FAT images of cards with presets, kits, samples and songs) in "
        "DELUGE_CONTENT_CARDS (default .superpowers/emu-card); a script's `# card: content/<name>` names one"
    ),
    "rig": "set DELUGE_PROBE_HOST to the probe rig's ssh destination (docs/dev/device_benchmarking.md)",
    "deluge-usb": "plug a Deluge into this machine's USB, and pip install -r tests/device/usbmidi/requirements.txt",
}

# Requirements CI never has: a test that needs one runs only on a developer's machine. `--ci-matrix` leaves such
# tests out, so a CI job never fails on a skip it cannot avoid; `--list` and `--doctor` still show them.
LOCAL_ONLY = frozenset(
    {
        "song-corpus",
        "song-xml-corpus",
        "legacy-corpus",
        "delugemu",
        "parity-ref",
        "content-card",
        "rig",
        "deluge-usb",
    }
)


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
    level: str
    runner: str
    kind: str
    status: str
    gates: str
    tier: str
    job: str
    runtime_s: int
    # runner = "cargo" | "script": the directory `build` and `run` run in, relative to the repository root.
    path: str = "."
    build: list[str] = field(default_factory=list)
    run: list[str] = field(default_factory=list)
    requires: list[str] = field(default_factory=list)
    # Set over the caller's environment.
    env: dict[str, str] = field(default_factory=dict)
    # Set only where the caller's environment does not already say.
    env_default: dict[str, str] = field(default_factory=dict)
    # runner = "ctest": the CMake source and build trees, extra configure arguments, extra ctest arguments.
    source: str = ""
    tree: str = ""
    configure: list[str] = field(default_factory=list)
    ctest_args: list[str] = field(default_factory=list)
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
    # runner = "corpus": the stem of the runner it runs, CORPUS_RUNNERS/<corpus_runner>_run.py.
    corpus_runner: str = ""
    # Run on its own after the parallel set: the gate measures real time, or loads the machine enough to distort others.
    alone: bool = False
    # The files and directories (repository paths) the test writes under `./dbt test --update`, and only they: the
    # runner snapshots them, restores them when the run fails, and fails a run that writes anywhere else.
    baseline: list[str] = field(default_factory=list)
    # The areas ([area.<name>] tables) the test exercises, or ["all"]: what `--since` selects it by (test_selection.py).
    areas: list[str] = field(default_factory=list)
    # A generated copy (a variant): the name of the entry it copies. Never written in the registry.
    variant_of: str = ""


# How each test judges what it gates. unit: one function or module; property: an invariant over generated or real
# inputs; differential: two implementations agree; regression: output equals a committed record; sanitizer: a
# sanitizer finds nothing; simulation: a modelled run whose output is read, not compared; benchmark: timing only.
KINDS = (
    "unit",
    "property",
    "differential",
    "regression",
    "sanitizer",
    "simulation",
    "benchmark",
)
# Runners that run the entry's own `build` and `run` commands; they run them in `path`.
PATH_RUNNERS = ("cargo", "script")
# The fields an entry may set: everything the runner reads.
ENTRY_FIELDS = frozenset(f.name for f in dataclasses.fields(Harness)) - {"variant_of"}


# How an entry is written rather than what it is: the loader consumes these before validating the entry.
SOURCE_FIELDS = frozenset({"template", "override", "variants", "variant_fields"})
# A template supplies fields entries share; never an entry's name or its claim, nor another template.
TEMPLATE_EXCLUDED = frozenset({"name", "gates"}) | SOURCE_FIELDS


def _templates(raw: dict) -> dict[str, dict]:
    templates = raw.get("template", {})
    # A registry mistake is a ValueError, the one exception the runner reports, so TRY004 is waived here and below.
    if not isinstance(templates, dict):
        raise ValueError("'template' is a table of [template.<name>] tables")  # noqa: TRY004
    for name, shared in templates.items():
        if not isinstance(shared, dict):
            raise ValueError(f"template '{name}' is a table of fields")  # noqa: TRY004
        if "template" in shared:
            raise ValueError(f"template '{name}': templates do not nest")
        if unknown := sorted(set(shared) - ENTRY_FIELDS - SOURCE_FIELDS):
            raise ValueError(
                f"template '{name}': unknown field(s) {', '.join(unknown)}"
            )
        if excluded := sorted(set(shared) & TEMPLATE_EXCLUDED):
            raise ValueError(
                f"template '{name}': a template cannot set {', '.join(excluded)}"
            )
    return templates


def _with_template(where: str, item: dict, templates: dict[str, dict]) -> dict:
    """`item` over its template's fields. A field both set must be listed in the entry's `override`."""
    declared = item.get("override", [])
    if not isinstance(declared, list) or not all(isinstance(k, str) for k in declared):
        raise ValueError(f"{where}: override is a list of field names")
    if "template" not in item:
        if declared:
            raise ValueError(
                f"{where}: 'override' names template fields, but the entry has no template"
            )
        return dict(item)
    name = item["template"]
    if not isinstance(name, str):
        raise ValueError(f"{where}: template is the name of a [template.<name>] table")  # noqa: TRY004
    if name not in templates:
        raise ValueError(f"{where}: unknown template '{name}'")
    shared = templates[name]
    own = {k: v for k, v in item.items() if k not in ("template", "override")}
    both = sorted(set(own) & set(shared))
    if undeclared := [k for k in both if k not in declared]:
        raise ValueError(
            f"{where}: sets {', '.join(undeclared)}, which template '{name}' sets too; list it in 'override' if the "
            "difference is meant"
        )
    if stale := [k for k in declared if k not in both]:
        raise ValueError(
            f"{where}: 'override' lists {', '.join(stale)}, which it does not set over template '{name}'"
        )
    if same := [k for k in both if own[k] == shared[k]]:
        raise ValueError(
            f"{where}: sets {', '.join(same)} to the value template '{name}' already sets"
        )
    return {**shared, **own}


# What a [variant.<name>] table may set: the copy's name and gates (each with "{base}" for the base's name), and what
# the copy runs differently. requires and env_default add to the base's; every other field replaces it.
VARIANT_FIELDS = frozenset(
    {
        "name",
        "gates",
        "kind",
        "builds",
        "requires",
        "env_default",
        "tier",
        "job",
        "runtime_s",
    }
)
# What one base may set for its own copy: variant_fields = { <variant> = { runtime_s = N } }.
VARIANT_INSTANCE_FIELDS = frozenset({"runtime_s"})
# A copy checks; it never owns what its base re-records.
_NOT_COPIED = frozenset({"variants", "variant_fields", "baseline"})


def _variant_tables(raw: dict) -> dict[str, dict]:
    variants = raw.get("variant", {})
    if not isinstance(variants, dict):
        raise ValueError("'variant' is a table of [variant.<name>] tables")  # noqa: TRY004
    for name, table in variants.items():
        if not isinstance(table, dict):
            raise ValueError(f"variant '{name}' is a table of fields")  # noqa: TRY004
        if unknown := sorted(set(table) - VARIANT_FIELDS):
            raise ValueError(f"variant '{name}': unknown field(s) {', '.join(unknown)}")
        for key in ("name", "gates"):
            if not isinstance(table.get(key, ""), str):
                raise ValueError(  # noqa: TRY004
                    f"variant '{name}': {key} is a string containing {{base}}"
                )
            if "{base}" not in table.get(key, ""):
                raise ValueError(f"variant '{name}': {key} must contain {{base}}")
    return variants


def _copies(items: list[dict], variants: dict[str, dict]) -> list[dict]:
    """Every variant copy the entries ask for, in entry order."""
    copies = []
    for i, item in enumerate(items):
        where = item.get("name", f"entry #{i}")
        wanted = item.get("variants", [])
        per_base = item.get("variant_fields", {})
        if not isinstance(wanted, list) or not all(isinstance(v, str) for v in wanted):
            raise ValueError(f"{where}: variants is a list of variant names")
        if not isinstance(per_base, dict):
            raise ValueError(  # noqa: TRY004
                f"{where}: variant_fields is a table of [variant_fields.<variant>] tables"
            )
        if wanted and "name" not in item:
            raise ValueError(f"{where}: missing required field 'name'")
        if wanted and (runner := item.get("runner")) != "scenario":
            raise ValueError(
                f"{where}: variant '{wanted[0]}' swaps the render build, so it applies to scenario entries; this is a "
                f"{runner} entry"
            )
        if stray := sorted(set(per_base) - set(wanted)):
            raise ValueError(
                f"{where}: variant_fields names {', '.join(stray)}, which 'variants' does not list"
            )
        for name in wanted:
            if name not in variants:
                raise ValueError(f"{where}: unknown variant '{name}'")
            own = per_base.get(name, {})
            if not isinstance(own, dict):
                raise ValueError(f"{where}: variant_fields.{name} is a table")  # noqa: TRY004
            if bad := sorted(set(own) - VARIANT_INSTANCE_FIELDS):
                raise ValueError(
                    f"{where}: variant_fields.{name} may set only {', '.join(sorted(VARIANT_INSTANCE_FIELDS))}, "
                    f"not {', '.join(bad)}"
                )
            table = variants[name]
            copy = {k: v for k, v in item.items() if k not in _NOT_COPIED}
            copy.update(
                {k: v for k, v in table.items() if k not in ("requires", "env_default")}
            )
            copy["name"] = table["name"].replace("{base}", item["name"])
            copy["gates"] = table["gates"].replace("{base}", item["name"])
            copy["requires"] = list(
                dict.fromkeys([*item.get("requires", []), *table.get("requires", [])])
            )
            copy["env_default"] = {
                **item.get("env_default", {}),
                **table.get("env_default", {}),
            }
            copy.update(own)
            copy["variant_of"] = item["name"]
            copies.append(copy)
    return copies


# The tables test_selection.py reads (load_areas); expand passes over them.
AREA_TABLES = ("area", "no-gate", "every-gate")


def expand(raw: dict) -> list[dict]:
    """The registry's entries as the runner sees them: each written entry over its template, then every variant
    copy."""
    if stray := sorted(set(raw) - {"harness", "template", "variant", *AREA_TABLES}):
        raise ValueError(f"unknown table(s) {', '.join(stray)}")
    templates = _templates(raw)
    variants = _variant_tables(raw)
    written = raw.get("harness", [])
    if not isinstance(written, list) or not all(
        isinstance(item, dict) for item in written
    ):
        raise ValueError("'harness' is a list of [[harness]] tables")
    items = []
    for i, item in enumerate(written):
        if not isinstance(item.get("name", ""), str):
            raise ValueError(f"entry #{i}: name is a string")  # noqa: TRY004
        where = item.get("name", f"entry #{i}")
        if unknown := sorted(set(item) - ENTRY_FIELDS - SOURCE_FIELDS):
            raise ValueError(f"{where}: unknown field(s) {', '.join(unknown)}")
        items.append(_with_template(where, item, templates))
    return items + _copies(items, variants)


# The longest `gates` line: one sentence a reader takes in from the README table. What it leaves out is the test's
# explanation, a `Gate <name>:` block in the file that judges the test or a `## <name>` section in its notes
# (explanation_source).
GATES_MAX = 160
_SENTENCE_BREAK = re.compile(r"[.!?;][\"')\]]*\s")


def gates_problem(gates: str) -> str | None:
    """Why `gates` is not one short sentence, or None."""
    if "\n" in gates:
        return "runs over more than one line"
    if len(gates) > GATES_MAX:
        return f"is {len(gates)} characters, more than {GATES_MAX}"
    if found := _SENTENCE_BREAK.search(gates):
        return f"has a sentence break ({found.group().strip()!r})"
    if gates.rstrip().endswith("."):
        return "ends with a full stop"
    return None


def baseline_problem(path: str) -> str | None:
    """Why `path` cannot be a registry `baseline`, or None when it can: it is under GOLDEN_ROOT, any file named
    tests/**/*_records.h, or one of BASELINES_ELSEWHERE.

    Any path below GOLDEN_ROOT is accepted, a direct child included: a consumer's whole directory is a baseline
    (tests/golden/param_record), and the loader reads no files, so it cannot tell that from a file directly in the
    root. The tree's layout (tests/golden/README.md) keeps every file in a consumer's directory."""
    parts = Path(path).parts
    root = GOLDEN_ROOT.parts
    if parts[: len(root)] == root and len(parts) > len(root):
        return None
    if parts[:1] == ("tests",) and path.endswith(RECORD_HEADER_SUFFIX):
        return None
    if path in BASELINES_ELSEWHERE:
        return None
    return (
        f"'{path}' is outside {GOLDEN_ROOT}/<consumer>/; only a record header (tests/…/*{RECORD_HEADER_SUFFIX}) "
        f"or {', '.join(BASELINES_ELSEWHERE)} lives elsewhere"
    )


# Where a scenario without a check is written: its stepped scenario, or its driver.
SCENARIO_SOURCES = (
    "tests/integration/scenarios/{scenario}_scenario.cpp",
    "tests/integration/scenarios/drivers/{scenario}.cpp",
)
# A check whose entries' explanations would swamp its docstring keeps them in a notes file beside it, one
# `## <name>` section per entry.
CHECK_NOTES = {
    "tests/integration/checks/render_song.py": "tests/integration/checks/render_song_cases.md",
}


def _script_source(h: Harness) -> Path:
    """The file a script entry's command runs, which holds its explanation: the program, the script after python3,
    the module after `python3 -m`, for `python3 -m pytest` the first positional argument naming a .py file (less any
    `::` node id), or for `python3 -m unittest` the first module it names (with none named, or `discover`, the module
    named after `path`'s last directory). A pytest option whose .py value is a separate argument, such as
    `-c conf.py`, reads as a test file: give such a value with `=` (`--config-file=conf.py`)."""
    run = h.run
    if run[0] != "python3":
        return Path(h.path) / run[0]
    if len(run) < 2 or (run[1].startswith("-") and run[1] != "-m"):
        raise ValueError(f"{h.name}: run names no script after python3")
    if run[1] != "-m":
        return Path(h.path) / run[1]
    if len(run) < 3:
        raise ValueError(f"{h.name}: run names no module after python3 -m")
    if run[2] == "pytest":
        files = [
            arg.split("::", 1)[0]
            for arg in run[3:]
            if not arg.startswith("-") and arg.split("::", 1)[0].endswith(".py")
        ]
        if not files:
            raise ValueError(
                f"{h.name}: run names no test file after python3 -m pytest; name a .py file (not a directory), "
                "since the explanation is read from the first"
            )
        return Path(h.path) / files[0]
    if run[2] != "unittest":
        return Path(h.path) / f"{run[2]}.py"
    if len(run) == 3 or run[3] == "discover":
        return Path(h.path) / f"{Path(h.path).name}.py"
    if run[3].startswith("-"):
        raise ValueError(
            f"{h.name}: run names an option before the first module after -m unittest; name the modules first, since "
            "the explanation is read from the first"
        )
    return Path(h.path) / f"{run[3]}.py"


def explanation_source(root: Path, h: Harness) -> Path | None:
    """The file that holds `h`'s explanation: the code that judges it, or that file's notes (CHECK_NOTES), or the
    file its command runs."""
    match h.runner:
        case "scenario":
            if h.check:
                return Path(CHECK_NOTES.get(h.check, h.check))
            for pattern in SCENARIO_SOURCES:
                candidate = Path(pattern.format(scenario=h.scenario))
                if (root / candidate).is_file():
                    return candidate
            return None
        case "corpus":
            return CORPUS_RUNNERS / f"{h.corpus_runner}_run.py"
        case "cargo":
            return Path(h.path) / "Cargo.toml"
        case "ctest":
            return Path(h.source) / "CMakeLists.txt"
    return _script_source(h)


_BLOCK_HEAD = re.compile(r"^Gate (?P<name>[a-z0-9][a-z0-9-]*):$")
_SECTION_HEAD = re.compile(r"^## (?P<name>[a-z0-9][a-z0-9-]*)$")
_HEADING = re.compile(r"^#{1,6} ")
_COMMENT = re.compile(r"^\s*(?:///|//|#)(?: |$)")


def _notes_sections(path: Path) -> dict[str, str]:
    """The `## <name>` sections of a Markdown notes file, each one's lines joined by single spaces."""
    sections: dict[str, list[str]] = {}
    current = None
    for line in path.read_text().splitlines():
        if head := _SECTION_HEAD.match(line):
            current = head["name"]
            if current in sections:
                raise ValueError(f"{path}: two '## {current}' sections")
            sections[current] = []
        elif _HEADING.match(line):
            current = None
        elif current is not None and line.strip():
            sections[current].append(line.strip())
    for name, body in sections.items():
        if not body:
            raise ValueError(f"{path}: '## {name}' is empty")
    return {name: " ".join(body) for name, body in sections.items()}


def explanation_blocks(path: Path) -> dict[str, str]:
    """The `Gate <name>:` blocks in `path`: in a Python file its module docstring, elsewhere its `///`, `//` or `#`
    comments. A block is the header and the lines indented under it, up to the first blank or unindented line; its
    text is those lines joined by single spaces. A Markdown notes file (CHECK_NOTES) holds `## <name>` sections."""
    if path.suffix == ".md":
        return _notes_sections(path)
    text = path.read_text()
    if path.suffix == ".py":
        lines = (
            ast.get_docstring(ast.parse(text, filename=str(path)), clean=True) or ""
        ).splitlines()
    else:
        lines = [
            _COMMENT.sub("", line, count=1) if _COMMENT.match(line) else ""
            for line in text.splitlines()
        ]
    blocks: dict[str, str] = {}
    i = 0
    while i < len(lines):
        head = _BLOCK_HEAD.match(lines[i])
        i += 1
        if not head:
            continue
        body = []
        while i < len(lines) and lines[i].startswith("    ") and lines[i].strip():
            body.append(lines[i].strip())
            i += 1
        name = head["name"]
        if not body:
            raise ValueError(f"{path}: 'Gate {name}:' is empty")
        if name in blocks:
            raise ValueError(f"{path}: two 'Gate {name}:' blocks")
        blocks[name] = " ".join(body)
    return blocks


def explanation(root: Path, h: Harness) -> str:
    """`h`'s explanation, or "" when its gates line says everything."""
    source = explanation_source(root, h)
    if source is None or not (root / source).is_file():
        return ""
    return explanation_blocks(root / source).get(h.name, "")


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


def load_areas(root: Path) -> ts.Areas:
    """The registry's [area.<name>], [no-gate] and [every-gate] tables under `root`."""
    raw = tomllib.loads((root / "tests" / "registry.toml").read_text())
    tables = raw.get("area", {})
    if not isinstance(tables, dict) or not all(
        isinstance(t, dict) and isinstance(t.get("paths"), list)
        for t in tables.values()
    ):
        raise ValueError(
            "'area' is a table of [area.<name>] tables, each with a 'paths' list"
        )
    flat = {}
    for key in ("no-gate", "every-gate"):
        table = raw.get(key, {})
        if not isinstance(table, dict) or not isinstance(table.get("paths", []), list):
            raise ValueError(f"'{key}' is a table with a 'paths' list")  # noqa: TRY004
        flat[key] = list(table.get("paths", []))
    return ts.Areas(
        areas={name: list(t["paths"]) for name, t in tables.items()},
        no_gate=flat["no-gate"],
        every_gate=flat["every-gate"],
    )


def load_registry(root: Path) -> list[Harness]:
    """Parse and validate tests/registry.toml under `root`."""
    raw = tomllib.loads((root / "tests" / "registry.toml").read_text())
    seen: set[str] = set()
    return [
        _validate(root, item, item.get("name", f"entry #{i}"), seen)
        for i, item in enumerate(expand(raw))
    ]


def _validate(root: Path, item: dict, where: str, seen: set[str]) -> Harness:
    """`item`, an expanded entry, as a Harness; `seen` holds the names validated before it."""
    for required in (
        "name",
        "level",
        "runner",
        "kind",
        "status",
        "gates",
        "tier",
        "job",
        "runtime_s",
        "areas",
    ):
        if required not in item or item[required] in ("", [], None):
            raise ValueError(f"{where}: missing required field '{required}'")
    if not isinstance(item["areas"], list) or not all(
        isinstance(a, str) and a for a in item["areas"]
    ):
        raise ValueError(f"{where}: areas is a list of area names")
    if not isinstance(item["gates"], str):
        raise ValueError(f"{where}: gates is a string")  # noqa: TRY004
    for command in ("build", "run"):
        given = item.get(command, [])
        if not isinstance(given, list) or not all(isinstance(a, str) for a in given):
            raise ValueError(f"{where}: {command} is a list of strings")
    if problem := gates_problem(item["gates"]):
        raise ValueError(
            f"{where}: gates {problem}: state what the test gates in one sentence, and put the rest in its explanation, "
            f"a 'Gate {item['name']}:' block in the file that judges it or a '## {item['name']}' section in that "
            "file's notes (tests/README.md, Writing an entry)"
        )

    for key, allowed in (
        ("status", STATUSES),
        ("level", LEVELS),
        ("runner", RUNNERS),
        ("kind", KINDS),
        ("tier", TIERS),
        ("job", tuple(JOBS)),
    ):
        if item[key] not in allowed:
            raise ValueError(f"{where}: {key} '{item[key]}' must be one of {allowed}")

    if item["runner"] in PATH_RUNNERS and not item.get("path"):
        raise ValueError(
            f"{where}: a {item['runner']} test runs its commands in 'path', so it needs a non-empty 'path'"
        )
    if item["runner"] not in PATH_RUNNERS and "path" in item:
        raise ValueError(
            f"{where}: only {' and '.join(PATH_RUNNERS)} tests run commands in a 'path'; a {item['runner']} test "
            "takes none"
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
            raise ValueError(f"{where}: scenario must name a row of scenario_table.cpp")
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

    seconds = item["runtime_s"]
    if not isinstance(seconds, int) or isinstance(seconds, bool) or seconds <= 0:
        raise ValueError(
            f"{where}: runtime_s is a whole number of seconds above zero, not {seconds!r}"
        )

    prebuilt = JOBS[item["job"]]
    if prebuilt and item.get("builds", []) != [prebuilt]:
        raise ValueError(
            f"{where}: job '{item['job']}' runs prebuilt, so a test in it builds only {prebuilt}"
        )
    # A script or build step could build at run time, which a prebuilt job must not.
    if prebuilt and item["runner"] not in ("scenario", "corpus"):
        raise ValueError(
            f"{where}: job '{item['job']}' runs prebuilt, so it holds only scenario and corpus gates"
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
    for p in baseline:
        if problem := baseline_problem(p):
            raise ValueError(f"{where}: baseline {problem}")
    for table in ("env", "env_default"):
        if CORPUS_DECLARED in item.get(table, {}):
            raise ValueError(
                f'{where}: {table} may not set {CORPUS_DECLARED}; declare requires = ["song-corpus"] instead'
            )

    if item["name"] in seen:
        raise ValueError(f"duplicate test name '{item['name']}'")
    seen.add(item["name"])

    h = Harness(
        name=item["name"],
        path=item.get("path", "."),
        level=item["level"],
        runner=item["runner"],
        kind=item["kind"],
        status=item["status"],
        gates=item["gates"],
        tier=item["tier"],
        build=item.get("build", []),
        run=item.get("run", []),
        requires=item.get("requires", []),
        runtime_s=item["runtime_s"],
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
        areas=list(item["areas"]),
        job=item["job"],
        variant_of=item.get("variant_of", ""),
    )
    if h.runner == "script":
        # Refuses a command whose explanation file it cannot name.
        _script_source(h)
    return h


def select(
    entries: list[Harness],
    names: list[str],
    levels: list[str],
    tier: str,
    only: bool = False,
) -> list[Harness]:
    """The entries a run covers.

    Named entries are taken as asked for, whatever their tier or status. Otherwise every entry at `tier` (`only`) or
    at `tier` or an earlier one, at one of `levels` (all levels when empty), that is not marked unavailable.
    """
    if names:
        by_name = {h.name: h for h in entries}
        unknown = [n for n in names if n not in by_name]
        if unknown:
            raise ValueError(f"unknown test(s): {', '.join(unknown)}")
        return [by_name[n] for n in names]

    within = (tier,) if only else TIERS[: TIERS.index(tier) + 1]
    return [
        h
        for h in entries
        if h.tier in within
        and (not levels or h.level in levels)
        and h.status != "unavailable"
    ]


# Where the kernel lists USB devices, and the Deluge's vendor and product ids.
USB_DEVICES = Path("/sys/bus/usb/devices")
DELUGE_USB_ID = ("16d0", "0ce2")


def usb_device_id(device: Path) -> tuple[str, str] | None:
    """`device`'s vendor and product ids, or None when it has none or goes away while they are read (an unplug, or a
    hub enumerating again)."""
    try:
        return tuple(
            (device / f).read_text().strip() for f in ("idVendor", "idProduct")
        )
    except OSError:
        return None


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
        case "llvm-cxxfilt":
            return shutil.which("llvm-cxxfilt") is not None
        case "llvm-objcopy":
            return shutil.which("llvm-objcopy") is not None
        case "llvm-readelf":
            return shutil.which("llvm-readelf") is not None
        case "arm-linux-gnueabihf-gcc":
            # What the arm-linux builds use of it: the sysroot's C library and libstdc++, which clang links against.
            return (Path("/usr/arm-linux-gnueabihf/include/stdio.h")).is_file()
        case "arm-none-eabi-gcc":
            # Where deluge_storage_sample_convert's build.rs looks for the device compiler.
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
            # Where tests/integration/corpus/driver.py's default_corpus() looks.
            candidates = [os.environ.get("DELUGE_SONG_CORPUS")]
            if os.environ.get("DELUGE_BACKUP"):
                candidates.append(str(Path(os.environ["DELUGE_BACKUP"]) / "SONGS"))
            candidates.append(str(Path.home() / "Deluge Backup" / "SONGS"))
            return any(c and Path(c).is_dir() for c in candidates)
        case "legacy-corpus":
            # What tests/integration/corpus/upgrade_run.py's corpus part reads: the original songs, and the factory
            # presets' originals in the SYNTHS folder beside them.
            value = os.environ.get("DELUGE_LEGACY_CORPUS")
            return (
                bool(value)
                and Path(value).is_dir()
                and (Path(value).parent / "SYNTHS").is_dir()
            )
        case "delugemu":
            emu = Path(
                os.environ.get("DELUGEMU_ROOT") or Path.home() / "GitHub/delugemu"
            )
            return all(
                p.is_file()
                for p in (emu / "scripts/run.sh", emu / "qemu/build/qemu-system-arm")
            )
        case "parity-ref":
            # Where tools/emu_parity.py's --ref looks.
            return Path(
                os.environ.get("DELUGE_PARITY_REF")
                or root / ".superpowers/estate/emu/main.elf"
            ).is_file()
        case "content-card":
            return Path(
                os.environ.get("DELUGE_CONTENT_CARDS") or root / ".superpowers/emu-card"
            ).is_dir()
        case "rig":
            return bool(os.environ.get("DELUGE_PROBE_HOST"))
        case "deluge-usb":
            plugged = any(
                usb_device_id(d) == DELUGE_USB_ID for d in USB_DEVICES.glob("*")
            )
            return plugged and all(
                importlib.util.find_spec(m) for m in ("pytest", "rtmidi", "usb")
            )
        case _:
            return False


def ci_matrix(selection: list[Harness]) -> dict:
    """The CI matrix for `selection`: one row per job that has a selected test, in JOBS order.

    Local-only tests (LOCAL_ONLY) are left out.
    """
    selection = [h for h in selection if not local_only(h)]
    return {
        "include": [
            {
                "job": job,
                "tests": " ".join(h.name for h in selection if h.job == job),
                "prebuilt": prebuilt or "",
            }
            for job, prebuilt in JOBS.items()
            if any(h.job == job for h in selection)
        ],
    }


def runtime_text(seconds: int) -> str:
    """`seconds` as the README and --list print it: whole minutes as ~Nm, anything else as ~Ns."""
    return (
        f"~{seconds // 60}m" if seconds >= 60 and seconds % 60 == 0 else f"~{seconds}s"
    )


def markdown_table(harnesses: list[Harness]) -> str:
    """The generated table for tests/README.md."""
    lines = [
        "| Test | Level | Runner | Kind | Status | Gates | Areas | Tier | Job | Runtime |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for h in harnesses:
        # Escape pipes in fields to preserve table structure
        escaped_gates = h.gates.replace("|", "\\|")
        lines.append(
            f"| {h.name} | {h.level} | {h.runner} | {h.kind} | {h.status} | {escaped_gates} | {', '.join(h.areas)} "
            f"| {h.tier} | {h.job} "
            f"| {runtime_text(h.runtime_s)} |"
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
    "AREA_TABLES",
    "BASELINES_ELSEWHERE",
    "CHECK_NOTES",
    "CORPUS_DECLARED",
    "CORPUS_RUNNERS",
    "ENTRY_FIELDS",
    "GATES_MAX",
    "GOLDEN_ROOT",
    "JOBS",
    "KINDS",
    "LEVELS",
    "LOCAL_ONLY",
    "PATH_RUNNERS",
    "PROBES",
    "RECORD_HEADER_SUFFIX",
    "RUNNERS",
    "SCENARIO_SOURCES",
    "SCENARIO_TABLE",
    "SOURCE_FIELDS",
    "TABLE_BEGIN",
    "TABLE_END",
    "TIERS",
    "VARIANT_FIELDS",
    "VARIANT_INSTANCE_FIELDS",
    "Harness",
    "baseline_problem",
    "ci_matrix",
    "corpus_fixture_scenarios",
    "declares_corpus",
    "dump_expanded",
    "expand",
    "explanation",
    "explanation_blocks",
    "explanation_source",
    "gates_problem",
    "load_areas",
    "load_registry",
    "local_only",
    "markdown_table",
    "probe",
    "select",
    "usb_device_id",
    "with_table",
]
