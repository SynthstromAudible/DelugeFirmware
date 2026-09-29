#! /usr/bin/env python3
"""Parse tests/registry.toml and probe each test's prerequisites.

Split from task-test.py so the parsing, selection and probing logic is
unit-testable without invoking dbt or spawning a build. See
test_harness_registry.py.
"""

import os
import shutil
from dataclasses import dataclass, field
from pathlib import Path

import test_builds
import tomllib

STATUSES = ("gate", "investigative", "unavailable")
LEVELS = ("unit", "component", "integration")
RUNNERS = ("ctest", "cargo", "scenario", "script")
# Cumulative, in this order: selecting a tier selects every tier before it too.
TIERS = ("pr", "nightly", "manual")

# Requirement key -> how to satisfy it. Every key a registry entry may name must
# appear here; `dbt test --doctor` prints the value when the probe fails.
PROBES = {
    "cargo": "install a Rust toolchain (rustup); rust-toolchain.toml pins the channel",
    "cargo-nextest": "cargo install cargo-nextest (or taiki-e/install-action@nextest in CI)",
    "cargo-fuzz": "cargo install cargo-fuzz, and a nightly toolchain",
    "cmake": "install cmake and ninja",
    "clang-tsan": "install clang (ThreadSanitizer needs clang or a TSan-capable GCC)",
    "mtools": "apt install mtools -- fs_differential's fixtures are built with mformat",
    "qemu-arm": "apt install qemu-user",
    "arm-linux-gnueabihf-gcc": "apt install g++-arm-linux-gnueabihf",
    "song-corpus": (
        "set DELUGE_BACKUP (default ~/Deluge Backup) or DELUGE_SONG_CORPUS to a directory of .XML songs, "
        "or have the golden fixtures already rebuilt in DELUGE_GOLDEN_DIR (default ~/.cache/deluge-golden)"
    ),
    "deluge-sdk-sibling": (
        "clone https://github.com/FirestormAudio/deluge-embassy as a sibling "
        "checkout at ../deluge-sdk -- the Rust BSP and its harnesses carry a "
        "path dependency on it"
    ),
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
    # A JUnit file the run leaves behind, relative to the repo root; `${CARGO_TARGET_DIR}` is expanded.
    junit: str = ""
    # Build products (test_builds.py) the runner makes before this test runs.
    builds: list[str] = field(default_factory=list)
    # runner = "scenario": the GOLDEN_SCENARIO to run ("" for the stem-render arm), the markers each run must print,
    # the whole line that says it passed, and a module (scenario_gate.py) with the gate's own checks.
    scenario: str | None = None
    markers: list[str] = field(default_factory=list)
    result: str = ""
    check: str = ""


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
        ):
            if not item.get(required):
                raise ValueError(f"{where}: missing required field '{required}'")

        for key, allowed in (
            ("status", STATUSES),
            ("level", LEVELS),
            ("runner", RUNNERS),
            ("tier", TIERS),
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
        elif item["runner"] == "scenario" and "scenario" in item:
            if item.get("build") or item.get("run"):
                raise ValueError(
                    f"{where}: a scenario gate is run from 'scenario', not 'build'/'run'"
                )
            if "golden-vt-render" not in item.get("builds", []):
                raise ValueError(
                    f"{where}: a scenario gate runs golden_vt_render, so builds must name golden-vt-render"
                )
        elif not item.get("run"):
            raise ValueError(f"{where}: missing required field 'run'")
        if item.get("check") and not (root / item["check"]).is_file():
            raise ValueError(f"{where}: check '{item['check']}' does not exist")

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
        case "clang-tsan":
            return shutil.which("clang++") is not None
        case "mtools":
            return shutil.which("mformat") is not None
        case "qemu-arm":
            return shutil.which("qemu-arm") is not None
        case "arm-linux-gnueabihf-gcc":
            cxx = os.environ.get("CXX", "arm-linux-gnueabihf-g++")
            return (
                shutil.which(cxx) is not None
                or shutil.which("arm-linux-gnueabihf-g++") is not None
            )
        case "deluge-sdk-sibling":
            # A checkout of another repository beside this one, not a path
            # inside it -- hence `root.parent`.
            return (root.parent / "deluge-sdk").is_dir()
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
        case _:
            return False


def markdown_table(harnesses: list[Harness]) -> str:
    """The generated table for tests/README.md."""
    lines = [
        "| Test | Level | Runner | Kind | Status | Gates | Tier | Runtime |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for h in harnesses:
        # Escape pipes in fields to preserve table structure
        escaped_gates = h.gates.replace("|", "\\|")
        escaped_runtime = h.runtime.replace("|", "\\|")
        lines.append(
            f"| {h.name} | {h.level} | {h.runner} | {h.kind} | {h.status} | {escaped_gates} | {h.tier} "
            f"| {escaped_runtime} |"
        )
    return "\n".join(lines) + "\n"


__all__ = [
    "LEVELS",
    "PROBES",
    "RUNNERS",
    "TIERS",
    "Harness",
    "load_registry",
    "markdown_table",
    "probe",
    "select",
]
