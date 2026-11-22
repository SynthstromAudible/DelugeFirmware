#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""No per-directory support/ folder under tests/ holds a file named like one in tests/support/.

A spec includes shared support code as "support/<path>", and its own directory comes before tests/ on its include
path, so a file at <spec dir>/support/<path> would be compiled in place of tests/support/<path> without a word. The
check compares each tracked file's path below its support/ folder with the paths below tests/support/.

Usage:
    scripts/check_test_support_names.py    # check the tracked files, exit 1 on a clash

Gate test-support-names:
    no per-directory support/ folder under tests/ holds a path that tests/support/ also holds, so
    "support/<path>" always names the shared file
"""

from __future__ import annotations

import subprocess
import sys
from collections.abc import Iterable
from pathlib import Path, PurePosixPath

REPO = Path(__file__).resolve().parent.parent
SHARED = PurePosixPath("tests/support")
ROOTS = ("tests",)


def clashes(paths: Iterable[str]) -> list[str]:
    """Each file in `paths` (repo-relative, POSIX) below a support/ folder other than tests/support/ whose path below
    that folder is also a path below tests/support/, as "<file> shadows tests/support/<path>"."""
    paths = [PurePosixPath(p) for p in paths]
    shared = {p.relative_to(SHARED) for p in paths if p.is_relative_to(SHARED)}
    found = []
    for path in paths:
        if path.is_relative_to(SHARED) or path.parts[0] not in ROOTS:
            continue
        parts = path.parts
        for i in range(len(parts) - 1):
            if parts[i] == "support" and PurePosixPath(*parts[i + 1 :]) in shared:
                found.append(
                    f"{path} shadows {SHARED / PurePosixPath(*parts[i + 1 :])}"
                )
                break
    return found


def main() -> int:
    tracked = subprocess.run(
        ["git", "ls-files", "--", *ROOTS],
        cwd=REPO,
        capture_output=True,
        text=True,
        check=True,
    ).stdout.split()
    found = clashes(tracked)
    for line in found:
        print(line)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
