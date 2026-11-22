#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Moves a library directory under src/ and rewrites every reference to it.

For each --move OLD NEW (both relative to src/, e.g. --move scenes ui/scenes):
- an #include in a tracked C-family file that the compiler resolves to a file under src/OLD (beside the including
  file, then under src/deluge, then under src) is respelled with NEW for OLD; one spelled relative to its own file is
  left, since it moves with that file;
- in every tracked text file, src/OLD and tests/unit/OLD become src/NEW and tests/unit/NEW, and the include-style
  token OLD/ becomes NEW/ where nothing path-like precedes it (a comment's header, a CMake subdirectory, the layer
  table's roots, prefixes and patterns); a CMake variable's closing brace
  counts as no path, so ${REPO_ROOT}/src/OLD is rewritten;
- src/OLD and tests/unit/OLD are moved with git mv.
Each --namespace OLD=NEW respells the qualified name wherever it is written (a namespace declaration, a qualified use,
a comment); an unqualified use that leaned on the old enclosing namespace is left for the compiler to name. Each
--target OLD=NEW renames a CMake target as a whole word.

A move is refused, and nothing is changed, when src/deluge/OLD exists (the legacy root precedes src/ on the include
path, so OLD/ would name two directories) or when src/NEW already exists. The north-star layout document is never
rewritten: its tables name today's paths beside their targets.

Usage:
    tools/relocate.py --move OLD NEW [--move OLD NEW ...] [--namespace OLD=NEW ...] [--target OLD=NEW ...] [--dry-run]
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
C_FAMILY = frozenset(
    {
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hh",
        ".hpp",
        ".hxx",
        ".inc",
        ".inl",
        ".ipp",
        ".tcc",
        ".s",
        ".S",
    }
)
TEXT = C_FAMILY | {
    ".txt",
    ".cmake",
    ".py",
    ".toml",
    ".yml",
    ".yaml",
    ".md",
    ".json",
    ".sh",
    ".in",
    ".rs",
    ".ld",
}
NEVER = frozenset({"docs/dev/code_layout_north_star.md"})
NEVER_UNDER = ("third_party/", "toolchain/", "src/gsl/", "lib/")
INCLUDE = re.compile(r'^(\s*#\s*include\s*)([<"])([^">]+)([>"])')
INCLUDE_ROOTS = ("src/deluge", "src")


class Refused(Exception):
    """A move that would change the meaning of an include, or overwrite a directory."""


def tracked(repo: Path) -> list[str]:
    out = subprocess.run(
        ["git", "ls-files", "-z"], cwd=repo, check=True, capture_output=True
    ).stdout
    return [p for p in out.decode().split("\0") if p]


def is_text(rel: str) -> bool:
    name = Path(rel).name
    return (
        rel not in NEVER
        and not rel.startswith(NEVER_UNDER)
        and (Path(rel).suffix in TEXT or name == "CMakeLists.txt")
    )


def resolve(repo: Path, including: str, spelled: str) -> str | None:
    """The repo-relative file an include names, as the build finds it, or None."""
    for base in (str(Path(including).parent), *INCLUDE_ROOTS):
        candidate = (repo / base / spelled).resolve()
        if candidate.is_file() and candidate.is_relative_to(repo.resolve()):
            return candidate.relative_to(repo.resolve()).as_posix()
    return None


def respell_includes(
    repo: Path, rel: str, text: str, moves: list[tuple[str, str]]
) -> str:
    lines = text.splitlines(keepends=True)
    for i, line in enumerate(lines):
        match = INCLUDE.match(line)
        if not match:
            continue
        spelled = match.group(3)
        target = resolve(repo, rel, spelled)
        if target is None:
            continue
        for old, new in moves:
            if target.startswith(f"src/{old}/") and spelled.startswith(f"{old}/"):
                lines[i] = line.replace(
                    f"{match.group(2)}{spelled}",
                    f"{match.group(2)}{new}/{spelled[len(old) + 1 :]}",
                    1,
                )
                break
    return "".join(lines)


def path_tokens(text: str, moves: list[tuple[str, str]], skip_includes: bool) -> str:
    for old, new in sorted(moves, key=lambda m: -len(m[0])):
        qualified = re.compile(
            rf"(?:(?<![\w/.\-])|(?<=\}}/))(src|tests/unit)/{re.escape(old)}(?![\w\-.])"
        )
        bare = re.compile(rf"(?<![\w/.\-]){re.escape(old)}/")
        out = []
        for line in text.splitlines(keepends=True):
            if skip_includes and INCLUDE.match(line):
                out.append(line)
                continue
            line = qualified.sub(lambda m, new=new: f"{m.group(1)}/{new}", line)
            line = bare.sub(f"{new}/", line)
            out.append(line)
        text = "".join(out)
    return text


def rename_words(
    text: str, namespaces: list[tuple[str, str]], targets: list[tuple[str, str]]
) -> str:
    for old, new in namespaces:
        text = re.sub(
            rf"(?<![\w:])(::)?{re.escape(old)}(?!\w)",
            lambda m, new=new: f"{m.group(1) or ''}{new}",
            text,
        )
    for old, new in targets:
        text = re.sub(rf"\b{re.escape(old)}\b", new, text)
    return text


def check(repo: Path, moves: list[tuple[str, str]]) -> None:
    for old, new in moves:
        if (repo / "src" / "deluge" / old).exists():
            raise Refused(f"src/deluge/{old} exists: {old}/ would name two directories")
        if (repo / "src" / new).exists():
            raise Refused(f"src/{new} already exists")
        if not (repo / "src" / old).is_dir():
            raise Refused(f"src/{old} is not a directory")


def plan(repo: Path, moves, namespaces, targets) -> dict[str, str]:
    check(repo, moves)
    changes = {}
    for rel in tracked(repo):
        if not is_text(rel) or not (repo / rel).is_file():
            continue
        before = (repo / rel).read_text(encoding="utf-8", errors="surrogateescape")
        c_family = Path(rel).suffix in C_FAMILY
        after = respell_includes(repo, rel, before, moves) if c_family else before
        after = path_tokens(after, moves, skip_includes=c_family)
        after = rename_words(after, namespaces, targets)
        if after != before:
            changes[rel] = after
    return changes


def apply(repo: Path, moves, namespaces, targets) -> None:
    changes = plan(repo, moves, namespaces, targets)
    for rel, text in changes.items():
        (repo / rel).write_text(text, encoding="utf-8", errors="surrogateescape")
    for old, new in moves:
        for root in ("src", "tests/unit"):
            source = repo / root / old
            if source.is_dir():
                (repo / root / new).parent.mkdir(parents=True, exist_ok=True)
                subprocess.run(
                    ["git", "mv", f"{root}/{old}", f"{root}/{new}"],
                    cwd=repo,
                    check=True,
                )


def pair(text: str) -> tuple[str, str]:
    old, sep, new = text.partition("=")
    if not sep or not old or not new:
        raise argparse.ArgumentTypeError(f"expected OLD=NEW, got {text!r}")
    return old, new


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--move", nargs=2, action="append", metavar=("OLD", "NEW"), required=True
    )
    parser.add_argument("--namespace", type=pair, action="append", default=[])
    parser.add_argument("--target", type=pair, action="append", default=[])
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="list the files a move would change, and change none",
    )
    parser.add_argument("--repo", type=Path, default=REPO, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    moves = [tuple(m) for m in args.move]
    try:
        if args.dry_run:
            for rel in sorted(plan(args.repo, moves, args.namespace, args.target)):
                print(rel)
            return 0
        apply(args.repo, moves, args.namespace, args.target)
    except Refused as refusal:
        print(f"relocate: refused: {refusal}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
