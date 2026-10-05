#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""The model names no engine's state: no file under src/deluge-project may name a sound record, one of the Deluge
instrument's words, one of the runtime's param collections or its cable words. A song holds an engine slot, which the
model stores without understanding, and addresses params (cable depths included) by their engine's id; the words belong
to the engine (src/engines/deluge) and the runtime.

Usage:
    scripts/check_model_words.py               # check src/deluge-project, exit 1 on a match
    scripts/check_model_words.py --root DIR    # check DIR
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

#: The engine's words the model must not name.
ENGINE_WORDS = (
    "DocSound",
    "DocSource",
    "DocModControllable",
    "SoundState",
    "SourceState",
    "SynthMode",
    "OscType",
    "LFOType",
    "PolyphonyMode",
    "SampleRepeatMode",
    # The runtime's param collections: the model addresses params by their engine's id.
    "ParamKind",
    "PATCHED",
    "UNPATCHED_SOUND",
    "UNPATCHED_GLOBAL",
    "patched",
    "unpatched",
    # The runtime's cable words: the model names a cable by its depth's id.
    "DocPatchCable",
    "patch_cable_param_id",
    "PATCH_CABLE",
    "PatchSource",
    "ParamDescriptor",
)
PATTERN = re.compile(r"\b(" + "|".join(ENGINE_WORDS) + r")\b")
SUFFIXES = {".h", ".hpp", ".cpp", ".c", ".cc", ".inc"}


def matches(root: Path) -> list[str]:
    """Every `path:line: word` under root naming one of ENGINE_WORDS."""
    found = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            for match in PATTERN.finditer(line):
                found.append(f"{path}:{number}: {match.group(1)}")
    return found


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO / "src" / "deluge-project")
    args = parser.parse_args(argv)
    found = matches(args.root)
    for line in found:
        print(line)
    if found:
        print(f"FAIL: the model names an engine's state ({len(found)} place(s))")
        return 1
    print("OK: the model names no engine's state")
    return 0


if __name__ == "__main__":
    sys.exit(main())
