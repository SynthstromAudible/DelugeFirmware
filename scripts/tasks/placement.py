#! /usr/bin/env python3
"""Writes the device linker fragment's placement blocks from scripts/layers.toml's [[placement]] table.

Each entry names a library by its path; path = namespace (deluge::<path components after src/>), so its code and
data are matched by the namespace's mangled form in each function's and object's own section name, which lld keeps
through ThinLTO. Text goes to .sdram_init inside the __sdram_text range, read-only and initialised data to .sdram_init
after it, zero-initialised data to .sdram_bss. The blocks sit between `/* BEGIN placement <kind> */` and
`/* END placement <kind> */` markers in src/platform/device/linker/sdram_sections.x; --check fails when they are stale
(test_placement runs it in the registry-selftest harness).
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import tomllib

ROOT = Path(__file__).resolve().parents[2]
LAYERS = ROOT / "scripts/layers.toml"
FRAGMENT = ROOT / "src/platform/device/linker/sdram_sections.x"
KINDS = ("text", "data", "bss", "asserts")
MEMORIES = ("sdram",)


class PlacementError(Exception):
    """A malformed [[placement]] entry, or a fragment without the markers."""


@dataclass(frozen=True)
class Placement:
    path: str
    memory: str
    reason: str
    min_text: int = 0
    min_data: int = 0
    min_bss: int = 0


def namespace(path: str) -> str:
    parts = Path(path).parts
    if parts[0] != "src":
        raise PlacementError(f"placement: {path} is not under src/")
    return "::".join(("deluge", *parts[1:]))


def mangled(ns: str) -> str:
    return "".join(f"{len(part)}{part}" for part in ns.split("::"))


def load(layers_toml: Path) -> list[Placement]:
    out = []
    for entry in tomllib.loads(layers_toml.read_text()).get("placement", []):
        path, memory, reason = (
            entry.get("path", ""),
            entry.get("memory", ""),
            entry.get("reason", ""),
        )
        if not path.startswith("src/") or path.startswith("src/deluge"):
            raise PlacementError(
                f"placement: {path!r}: only a namespaced library under src/ (not src/deluge) is placed by name"
            )
        if memory not in MEMORIES:
            raise PlacementError(
                f"placement: {path}: memory {memory!r} is not one of {MEMORIES}"
            )
        if not reason:
            raise PlacementError(f"placement: {path}: every entry says why")
        out.append(
            Placement(
                path,
                memory,
                reason,
                int(entry.get("min_text", 0)),
                int(entry.get("min_data", 0)),
                int(entry.get("min_bss", 0)),
            )
        )
    return out


def _tag(p: Placement) -> str:
    return "_".join(Path(p.path).parts[1:])


def blocks(placements: list[Placement]) -> dict[str, str]:
    text, data, bss, asserts = [], [], [], []
    for p in placements:
        ns, m, tag = namespace(p.path), mangled(namespace(p.path)), _tag(p)
        text += [
            f"/* {ns}: {p.reason} */",
            f"__placement_{tag}_text_start = .;",
            f"*(.text.*{m}*)",
            f"__placement_{tag}_text_end = .;",
        ]
        data += [
            f"__placement_{tag}_data_start = .;",
            f"*(.rodata.*{m}* .data.*{m}*)",
            f"__placement_{tag}_data_end = .;",
        ]
        bss += [
            f"__placement_{tag}_bss_start = .;",
            f"*(.bss.*{m}*)",
            f"__placement_{tag}_bss_end = .;",
        ]
        for kind, size, what in (
            ("text", p.min_text, "code"),
            ("data", p.min_data, "data"),
            ("bss", p.min_bss, "zero-initialised data"),
        ):
            if size:
                asserts += [
                    f"ASSERT(__placement_{tag}_{kind}_end - __placement_{tag}_{kind}_start > {size},",
                    f'       "sdram_sections.x: {ns}\'s {what} fell back to SRAM (scripts/layers.toml [[placement]])")',
                ]
    return {
        "text": "\n".join(text),
        "data": "\n".join(data),
        "bss": "\n".join(bss),
        "asserts": "\n".join(asserts),
    }


def render(fragment: str, generated: dict[str, str]) -> str:
    for kind in KINDS:
        rx = re.compile(
            rf"^([ \t]*)/\* BEGIN placement {kind} \*/\n.*?^[ \t]*/\* END placement {kind} \*/\n",
            re.MULTILINE | re.DOTALL,
        )
        m = rx.search(fragment)
        if not m:
            raise PlacementError(
                f"placement: no BEGIN/END placement {kind} markers in the fragment"
            )
        indent = m.group(1)
        body = "".join(f"{indent}{line}\n" for line in generated[kind].splitlines())
        fragment = (
            fragment[: m.start()]
            + (
                f"{indent}/* BEGIN placement {kind} */\n{body}"
                f"{indent}/* END placement {kind} */\n"
            )
            + fragment[m.end() :]
        )
    return fragment


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    args = p.parse_args(argv)
    current = FRAGMENT.read_text()
    wanted = render(current, blocks(load(LAYERS)))
    if args.write:
        FRAGMENT.write_text(wanted)
        return 0
    if wanted != current:
        print(
            "placement: sdram_sections.x is stale; run python3 scripts/tasks/placement.py --write",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
