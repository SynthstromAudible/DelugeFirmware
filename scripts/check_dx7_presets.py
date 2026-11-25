#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""The render presets that hold a DX7, and whether each one's other oscillator can sound.

A DX7 becomes a sound-level engine: a sound that held a DX7 on one oscillator slot loads with its other oscillator
dropped. The manifest (tests/golden/render/dx7_presets.json) lists every render-golden preset holding a DX7, which
slot it is on, and whether the other oscillator can sound — by its volume above the minimum, or by a cable reaching
its volume, or by ring modulation, oscillator sync or automation (each counted audible). Presets whose other
oscillator cannot sound must render unchanged once the DX7 is an engine; the others
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


def _volume(value: object) -> int | None:
    """A volume param as the file writes it, as the signed 32-bit word it is; none for automation (a node string longer
    than one word), which can raise it anywhere."""
    if isinstance(value, str):
        text = value.strip()
        digits = text[2:] if text.lower().startswith("0x") else text
        if len(digits) > 8:
            return None
        word = int(text, 16) & 0xFFFFFFFF
    else:
        word = int(value) & 0xFFFFFFFF
    return word - (1 << 32) if word & (1 << 31) else word


def _field(element: ElementTree.Element, name: str) -> str | None:
    """An attribute, or the text of the child element of that name (both forms are written)."""
    value = element.get(name)
    if value is None and (child := element.find(name)) is not None:
        value = (child.text or "").strip()
    return value


def _sounds_from_xml(text: str) -> list[dict]:
    """The fields the scan reads from each sound an XML file holds, wherever it is nested (a preset, a kit's rows, a
    song's instruments): each slot's type, the mode, oscillator sync, the source volumes and the cables' targets."""
    root = ElementTree.fromstring(text)
    sounds = [root] if root.tag == "sound" else list(root.iter("sound"))
    found = []
    for sound in sounds:
        slots = {
            name: (el.get("type") or "sine")
            for name in VOLUME
            if (el := sound.find(name)) is not None
        }
        params = sound.find("defaultParams")
        volumes: dict[str, int | None] = {}
        destinations: set[str] = set()
        if params is not None:
            for name in VOLUME.values():
                if (value := _field(params, name)) is not None and value != "":
                    volumes[name] = _volume(value)
            for cable in params.iter("patchCable"):
                if destination := _field(cable, "destination"):
                    destinations.add(destination)
        found.append(
            {
                "slots": slots,
                "mode": _field(sound, "mode") or "subtractive",
                "sync": (_field(sound, "oscillatorSync") or "0") not in ("0", ""),
                "volumes": volumes,
                "destinations": destinations,
            }
        )
    return found


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
    found = []
    for sound in _json_sounds(json.loads(text)):
        slots = {
            name: (sound[name].get("type") or "sine")
            for name in VOLUME
            if isinstance(sound.get(name), dict)
        }
        params = sound.get("defaultParams") or {}
        volumes = {
            name: _volume(params[name]) for name in VOLUME.values() if name in params
        }
        destinations = set()
        for entry in params.get("patchCables") or []:
            cable = entry.get("patchCable", entry) if isinstance(entry, dict) else {}
            if cable.get("destination"):
                destinations.add(cable["destination"])
        found.append(
            {
                "slots": slots,
                "mode": sound.get("mode") or "subtractive",
                "sync": str(sound.get("oscillatorSync", 0))
                not in ("0", "", "False", "false"),
                "volumes": volumes,
                "destinations": destinations,
            }
        )
    return found


def _audible(sound: dict, other: str) -> bool:
    """Whether the other oscillator can sound — or can shape the DX7 — as the app decides a source is active: ring
    modulation always plays both sources, oscillator sync runs the first as the second's master, an automated or
    unwritten volume may rise, and a cable to the volume can raise it. Any of these counts it audible."""
    volume = VOLUME[other]
    if sound["mode"] == "ringmod" or sound["sync"] or volume in sound["destinations"]:
        return True
    if volume not in sound["volumes"]:
        return True
    level = sound["volumes"][volume]
    return level is None or level > SILENT


def scan(root: pathlib.Path) -> list[dict]:
    """Every sound under @p root holding a DX7, sorted by path; a file holding several sounds names each one's index
    after a '#'."""
    found = []
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
            for slot, kind in sound["slots"].items():
                if kind != "dx7":
                    continue
                other = OTHER[slot]
                found.append(
                    {
                        "path": name if len(sounds) == 1 else f"{name}#{index}",
                        "dx7_slot": slot,
                        "other_type": sound["slots"].get(other, "sine"),
                        "other_audible": _audible(sound, other),
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
