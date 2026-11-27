#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""The render presets that play the DX7 engine, and none that holds a DX7 on an oscillator.

The DX7 is a sound-level engine: a DX7 sound is in DX7 mode, its patch in its `dx7` entry. The firmware refuses an
oscillator of type `dx7`, the form released firmware wrote, which the upgrader (tools/deluge-upgrade) converts. The
manifest (tests/golden/render/dx7_presets.json) lists every render-golden preset in DX7 mode.

Usage: check_dx7_presets.py [--check | --write] [--root DIR] [--manifest FILE]

Gate dx7-presets:
    every render-golden preset in DX7 mode is in the manifest, and no preset holds a DX7 on an oscillator: a preset
    gaining or losing the DX7 engine fails until the manifest is rewritten (--write) and the change explained, and an
    oscillator DX7 fails until the upgrader converts it
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from xml.etree import ElementTree

REPO = pathlib.Path(__file__).resolve().parent.parent
ROOT = REPO / "tests" / "golden" / "render" / "presets"
MANIFEST = REPO / "tests" / "golden" / "render" / "dx7_presets.json"
OSCILLATORS = ("osc1", "osc2")


def _field(element: ElementTree.Element, name: str) -> str | None:
    """An attribute, or the text of the child element of that name (both forms are written)."""
    value = element.get(name)
    if value is None and (child := element.find(name)) is not None:
        value = (child.text or "").strip()
    return value


def _sounds_from_xml(text: str) -> list[dict]:
    """Each sound an XML file holds, wherever it is nested: its mode and its oscillators' types."""
    root = ElementTree.fromstring(text)
    sounds = [root] if root.tag == "sound" else list(root.iter("sound"))
    return [
        {
            "mode": _field(sound, "mode") or "subtractive",
            "types": [
                el.get("type") or "sine"
                for name in OSCILLATORS
                if (el := sound.find(name)) is not None
            ],
        }
        for sound in sounds
    ]


def _json_sounds(node: object) -> list[dict]:
    """Every object under a "sound" key, wherever it is nested."""
    found = []
    if isinstance(node, dict):
        for key, value in node.items():
            if key == "sound" and isinstance(value, dict):
                found.append(value)
            found.extend(_json_sounds(value))
    elif isinstance(node, list):
        for value in node:
            found.extend(_json_sounds(value))
    return found


def _sounds_from_json(text: str) -> list[dict]:
    """The same fields from each sound a JSON file holds."""
    return [
        {
            "mode": sound.get("mode") or "subtractive",
            "types": [
                sound[name].get("type") or "sine"
                for name in OSCILLATORS
                if isinstance(sound.get(name), dict)
            ],
        }
        for sound in _json_sounds(json.loads(text))
    ]


def scan(root: pathlib.Path) -> tuple[list[dict], list[str]]:
    """(the sounds under @p root in DX7 mode, sorted by path; the paths holding a DX7 on an oscillator). A file holding
    several sounds names each one's index after a '#'."""
    engine, oscillator = [], []
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        suffix = path.suffix.lower()
        if suffix not in (".xml", ".json"):
            continue
        text = path.read_text(errors="replace")
        try:
            sounds = (
                _sounds_from_xml(text) if suffix == ".xml" else _sounds_from_json(text)
            )
        except (ElementTree.ParseError, json.JSONDecodeError):
            continue
        name = path.relative_to(root).as_posix()
        for index, sound in enumerate(sounds):
            where = name if len(sounds) == 1 else f"{name}#{index}"
            if sound["mode"] == "dx7":
                engine.append({"path": where})
            if "dx7" in sound["types"]:
                oscillator.append(where)
    return engine, oscillator


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--check",
        action="store_true",
        help="compare the scan with the manifest (the default)",
    )
    mode.add_argument(
        "--write", action="store_true", help="record the scan as the manifest"
    )
    parser.add_argument("--root", type=pathlib.Path, default=ROOT)
    parser.add_argument("--manifest", type=pathlib.Path, default=MANIFEST)
    args = parser.parse_args(argv)

    presets, oscillator = scan(args.root)
    for where in oscillator:
        print(
            f"{where}: a DX7 on an oscillator, which the firmware refuses; tools/deluge-upgrade converts it"
        )
    if oscillator:
        return 1
    if args.write:
        args.manifest.write_text(json.dumps({"presets": presets}, indent=2) + "\n")
        return 0
    recorded = (
        json.loads(args.manifest.read_text())["presets"]
        if args.manifest.exists()
        else []
    )
    if presets != recorded:
        missing = [p["path"] for p in recorded if p not in presets]
        added = [p["path"] for p in presets if p not in recorded]
        print(
            f"DX7 presets differ from {args.manifest}: no longer DX7 {missing}, newly DX7 {added}"
        )
        return 1
    print(f"OK: {len(presets)} DX7 presets")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
