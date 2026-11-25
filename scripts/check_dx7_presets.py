#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""The render presets that hold a DX7, and whether each one's other oscillator can sound.

A DX7 becomes a sound-level engine: a sound that held a DX7 on one oscillator slot loads with its other oscillator
dropped. The manifest (tests/golden/render/dx7_presets.json) lists every render-golden preset holding a DX7, which
slot it is on, and whether the other oscillator can sound — by its volume above the minimum, or by a cable reaching
its volume. Presets whose other oscillator cannot sound must render unchanged once the DX7 is an engine; the others
are the goldens that change with it.

Usage: check_dx7_presets.py [--check | --write] [--root DIR] [--manifest FILE]

Gate dx7-presets:
    every render-golden preset holding a DX7 is in the manifest, on the slot recorded, with the other oscillator's type
    and audibility as recorded: a preset gaining, losing or changing a DX7, or an other oscillator turning audible or
    silent, fails until the manifest is rewritten (--write) and the change explained
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

SILENT = -(
    1 << 31
)  # a source volume's minimum (the word 0x80000000): the floor at which the oscillator cannot sound
VOLUME = {"osc1": "oscAVolume", "osc2": "oscBVolume"}
OTHER = {"osc1": "osc2", "osc2": "osc1"}


def _param(value: object) -> int:
    """A param word as the file writes it (hex text, or a number), as the signed 32-bit value it is."""
    word = (int(value, 16) if isinstance(value, str) else int(value)) & 0xFFFFFFFF
    return word - (1 << 32) if word & (1 << 31) else word


def _sound_from_xml(text: str) -> dict | None:
    """The fields the scan reads, from an XML preset: each slot's type, the source volumes and the cables' targets."""
    root = ElementTree.fromstring(text)
    sound = root if root.tag == "sound" else root.find("sound")
    if sound is None:
        return None
    slots = {
        name: (el.get("type") or "sine")
        for name in VOLUME
        if (el := sound.find(name)) is not None
    }
    params = sound.find("defaultParams")
    volumes: dict[str, int] = {}
    destinations: set[str] = set()
    if params is not None:
        for name in VOLUME.values():
            value = params.get(name)
            if value is None and (child := params.find(name)) is not None:
                value = (child.text or "").strip()
            if value:
                volumes[name] = _param(value)
        for cable in params.iter("patchCable"):
            destination = cable.get("destination")
            if destination is None and (child := cable.find("destination")) is not None:
                destination = (child.text or "").strip()
            if destination:
                destinations.add(destination)
    return {"slots": slots, "volumes": volumes, "destinations": destinations}


def _sound_from_json(text: str) -> dict | None:
    """The same fields from a JSON preset."""
    document = json.loads(text)
    sound = document.get("sound") if isinstance(document, dict) else None
    if not isinstance(sound, dict):
        return None
    slots = {
        name: (sound[name].get("type") or "sine")
        for name in VOLUME
        if isinstance(sound.get(name), dict)
    }
    params = sound.get("defaultParams") or {}
    volumes = {name: _param(params[name]) for name in VOLUME.values() if name in params}
    destinations = set()
    for entry in params.get("patchCables") or []:
        cable = entry.get("patchCable", entry) if isinstance(entry, dict) else {}
        if cable.get("destination"):
            destinations.add(cable["destination"])
    return {"slots": slots, "volumes": volumes, "destinations": destinations}


def scan(root: pathlib.Path) -> list[dict]:
    """Every preset under @p root holding a DX7, sorted by path."""
    found = []
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        suffix = path.suffix.lower()
        if suffix not in (".xml", ".json"):
            continue
        text = path.read_text(errors="replace")
        try:
            sound = (
                _sound_from_xml(text) if suffix == ".xml" else _sound_from_json(text)
            )
        except (ElementTree.ParseError, json.JSONDecodeError):
            continue
        if sound is None:
            continue
        for slot, kind in sound["slots"].items():
            if kind != "dx7":
                continue
            other = OTHER[slot]
            volume = VOLUME[other]
            audible = (
                sound["volumes"].get(volume, SILENT) > SILENT
                or volume in sound["destinations"]
            )
            found.append(
                {
                    "path": path.relative_to(root).as_posix(),
                    "dx7_slot": slot,
                    "other_type": sound["slots"].get(other, "sine"),
                    "other_audible": audible,
                }
            )
            break
    return found


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

    presets = scan(args.root)
    if args.write:
        args.manifest.write_text(json.dumps({"presets": presets}, indent=2) + "\n")
        return 0
    recorded = (
        json.loads(args.manifest.read_text())["presets"]
        if args.manifest.exists()
        else []
    )
    for index in range(max(len(presets), len(recorded))):
        now = presets[index] if index < len(presets) else None
        then = recorded[index] if index < len(recorded) else None
        if now != then:
            print(
                f"DX7 presets differ from {args.manifest}: recorded {then}, scanned {now}"
            )
            return 1
    audible = sum(1 for p in presets if p["other_audible"])
    print(f"OK: {len(presets)} DX7 presets, {audible} with an audible other oscillator")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
