#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Boundary check for src/engine-api/types/, the data-only types of the host–engine contract.

A file there may include only standard-library headers (<name>, no path) and other types-layer headers
("engine-api/types/..."). There is no allowlist: the layer is clean by construction, and a type that still needs
another header is not ready to move here.

Usage:
    scripts/check_engine_types_boundary.py              # check src/engine-api/types, exit 1 on a violation
    scripts/check_engine_types_boundary.py --root DIR   # check DIR instead (used to test the check itself)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')


def violations(root: Path) -> list[str]:
    found = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in {".h", ".hpp", ".cpp"}:
            continue
        for number, line in enumerate(path.read_text().splitlines(), start=1):
            match = INCLUDE.match(line)
            if match is None:
                continue
            bracket, name = match.groups()
            standard = bracket == "<" and "/" not in name
            own = bracket == '"' and name.startswith("engine-api/types/")
            if not (standard or own):
                found.append(
                    f"{path.relative_to(root.parent)}:{number}: {line.strip()}"
                )
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--root", type=Path, default=REPO / "src" / "engine-api" / "types"
    )
    args = parser.parse_args()
    found = violations(args.root)
    for line in found:
        print(line)
    if found:
        print(f"FAIL: {len(found)} include(s) outside the engine-api types boundary")
        return 1
    print("OK: engine-api types include only the standard library and themselves")
    return 0


if __name__ == "__main__":
    sys.exit(main())
