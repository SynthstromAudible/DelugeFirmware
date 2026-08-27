#! /usr/bin/env python3
"""Parse harness/registry.toml and probe each harness's prerequisites.

Split from task-harness.py so the parsing and probing logic is unit-testable
without invoking dbt or spawning a build. See test_harness_registry.py.
"""

import os
import shutil
from dataclasses import dataclass, field
from pathlib import Path

import tomllib

STATUSES = ("gate", "investigative", "unavailable")
CI_TIERS = ("pr", "nightly", "never")

# Requirement key -> how to satisfy it. Every key a registry entry may name must
# appear here; `dbt harness doctor` prints the value when the probe fails.
PROBES = {
    "cargo": "install a Rust toolchain (rustup); rust-toolchain.toml pins the channel",
    "clang-tsan": "install clang (ThreadSanitizer needs clang or a TSan-capable GCC)",
    "mtools": "apt install mtools -- fs_differential's fixtures are built with mformat",
    "qemu-arm": "apt install qemu-user",
    "arm-linux-gnueabihf-gcc": "apt install g++-arm-linux-gnueabihf",
    "build-embassy-hostapp": (
        "cmake -B build-embassy-hostapp -S sim -G Ninja -DDELUGE_SIM_X64=ON && "
        "cmake --build build-embassy-hostapp"
    ),
    "song-corpus": "set DELUGE_BACKUP (default ~/Deluge Backup) or DELUGE_SONG_CORPUS to a directory of .XML songs",
    "deluge-sdk-sibling": (
        "clone https://github.com/FirestormAudio/deluge-embassy as a sibling "
        "checkout at ../deluge-sdk -- lens1_vt_sim and golden_vt_render carry a "
        "path dependency on it"
    ),
}


@dataclass
class Harness:
    name: str
    path: str
    kind: str
    status: str
    gates: str
    build: list[str]
    run: list[str]
    requires: list[str]
    ci: str
    runtime: str
    env: dict[str, str] = field(default_factory=dict)


def load_registry(root: Path) -> list[Harness]:
    """Parse and validate harness/registry.toml under `root`."""
    raw = tomllib.loads((root / "harness" / "registry.toml").read_text())
    entries: list[Harness] = []
    seen: set[str] = set()

    for i, item in enumerate(raw.get("harness", [])):
        where = item.get("name", f"entry #{i}")

        for required in ("name", "path", "kind", "status", "gates", "ci"):
            if not item.get(required):
                raise ValueError(f"{where}: missing required field '{required}'")

        if item["status"] not in STATUSES:
            raise ValueError(
                f"{where}: status '{item['status']}' must be one of {STATUSES}"
            )
        if item["ci"] not in CI_TIERS:
            raise ValueError(f"{where}: ci '{item['ci']}' must be one of {CI_TIERS}")

        for req in item.get("requires", []):
            if req not in PROBES:
                raise ValueError(
                    f"{where}: unknown requirement '{req}'; add a probe to PROBES"
                )

        if item["name"] in seen:
            raise ValueError(f"duplicate harness name '{item['name']}'")
        seen.add(item["name"])

        entries.append(
            Harness(
                name=item["name"],
                path=item["path"],
                kind=item["kind"],
                status=item["status"],
                gates=item["gates"],
                build=item.get("build", []),
                run=item.get("run", []),
                requires=item.get("requires", []),
                ci=item["ci"],
                runtime=item.get("runtime", "?"),
                env=item.get("env", {}),
            )
        )

    return entries


def probe(requirement: str, root: Path) -> bool:
    """Is `requirement` satisfied on this machine?"""
    match requirement:
        case "cargo":
            return shutil.which("cargo") is not None
        case "clang-tsan":
            return shutil.which("clang++") is not None
        case "mtools":
            return shutil.which("mformat") is not None
        case "qemu-arm":
            return shutil.which("qemu-arm") is not None
        case "arm-linux-gnueabihf-gcc":
            return shutil.which("arm-linux-gnueabihf-g++") is not None
        case "build-embassy-hostapp":
            return (root / "build-embassy-hostapp").is_dir()
        case "deluge-sdk-sibling":
            # A checkout of another repository beside this one, not a path
            # inside it -- hence `root.parent`.
            return (root.parent / "deluge-sdk").is_dir()
        case "song-corpus":
            for var in ("DELUGE_SONG_CORPUS", "DELUGE_BACKUP"):
                value = os.environ.get(var)
                if value and Path(value).is_dir():
                    return True
            return (Path.home() / "Deluge Backup").is_dir()
        case _:
            return False


def markdown_table(harnesses: list[Harness]) -> str:
    """The generated table for harness/README.md."""
    lines = [
        "| Harness | Kind | Status | Gates | CI | Runtime |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for h in harnesses:
        # Escape pipes in fields to preserve table structure
        escaped_gates = h.gates.replace("|", "\\|")
        escaped_runtime = h.runtime.replace("|", "\\|")
        lines.append(
            f"| {h.name} | {h.kind} | {h.status} | {escaped_gates} | {h.ci} | {escaped_runtime} |"
        )
    return "\n".join(lines) + "\n"


__all__ = ["PROBES", "Harness", "load_registry", "markdown_table", "probe"]
