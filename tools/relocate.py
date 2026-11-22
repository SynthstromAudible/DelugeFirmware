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
- in every tracked text file, src/OLD and tests/unit/OLD (also after $VAR/, ${VAR}/ or ../) become src/NEW and
  tests/unit/NEW; all moves are read against the layout before any move, in one pass;
- the include-style token OLD/ becomes NEW/ where nothing path-like precedes it, and only where it names a path or
  layer: prose, CMake files, the layer table, its baselines and rules, and comments in C-family files. A string
  literal in code, a script and a shell file keep their OLD/;
- src/OLD and tests/unit/OLD are moved with git mv.
Each --namespace OLD=NEW respells OLD and every name under it (OLD::...) in every tracked text file, code, strings and
comments alike (read the dry run for fixtures that spell OLD on purpose); an unqualified use that leaned on the old
enclosing namespace is left for the compiler to name. Each --target OLD=NEW renames a CMake target as a whole word,
in every tracked text file the same way.

A move is refused, and nothing is changed, when src/deluge/OLD exists (the legacy root precedes src/ on the include
path, so OLD/ would name two directories) or when src/NEW or tests/unit/NEW already exists, or when two moves name
the same directory or one inside the other (a chain would move on disk differently from how the text reads). The
north-star layout document is never rewritten: its tables name today's paths beside their targets. Nor is any file
under third_party/, toolchain/, lib/ or website/. An include spelled from tests/ (`"unit/OLD/..."`) is not respelled.

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
NEVER_UNDER = ("third_party/", "toolchain/", "lib/", "website/")
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
        for old, new in sorted(moves, key=lambda m: -len(m[0])):
            if target.startswith(f"src/{old}/") and spelled.startswith(f"{old}/"):
                lines[i] = line.replace(
                    f"{match.group(2)}{spelled}",
                    f"{match.group(2)}{new}/{spelled[len(old) + 1 :]}",
                    1,
                )
                break
    return "".join(lines)


def raw_prefix(text: str, i: int) -> bool:
    """Whether the `R` at text[i] starts a raw string: alone, or after an encoding prefix (u8, u, U, L)."""
    start = i
    while start > 0 and (text[start - 1].isalnum() or text[start - 1] == "_"):
        start -= 1
    return text[start:i] in ("", "u8", "u", "U", "L")


def comment_mask(text: str) -> list[bool]:
    """Which characters of C-family source lie inside a comment (string, char and raw-string literals are code)."""
    mask = [False] * len(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            end = i
            while True:
                end = text.find("\n", end)
                if end < 0:
                    end = n
                    break
                if text[end - 1] != "\\":
                    break
                end += 1
            mask[i:end] = [True] * (end - i)
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            mask[i:end] = [True] * (end - i)
            i = end
        elif c == "R" and text.startswith('R"', i) and raw_prefix(text, i):
            paren = text.find("(", i)
            delim = text[i + 2 : paren] if paren > 0 else ""
            end = text.find(f'){delim}"', paren) if paren > 0 else -1
            i = n if end < 0 else end + len(delim) + 2
        elif c == '"' or (c == "'" and (i == 0 or not text[i - 1].isalnum())):
            i += 1
            while i < n and text[i] != c and text[i] != "\n":
                i += 2 if text[i] == "\\" else 1
            i += 1
        else:
            i += 1
    return mask


def bare_spelling(rel: str) -> str:
    """Where `OLD/` names a path or layer in a file.

    Returns "all" (prose, CMake, checker data), "comments" (C-family) or "none".
    """
    path = Path(rel)
    if path.suffix in C_FAMILY:
        return "comments"
    if rel.startswith("scripts/") and (
        path.name in ("layers.toml", "check_layer_rules.py") or "baseline" in path.name
    ):
        return "all"
    if path.name == "CMakeLists.txt" or path.suffix in (
        ".cmake",
        ".md",
        ".toml",
        ".txt",
        ".yml",
        ".yaml",
    ):
        return "all"
    return "none"


def path_tokens(
    text: str, moves: list[tuple[str, str]], bare: str, skip_includes: bool
) -> str:
    """One simultaneous pass: every OLD is read against the layout before any move."""
    ordered = sorted(moves, key=lambda m: -len(m[0]))
    new_of = dict(ordered)
    alternation = "|".join(re.escape(old) for old, _ in ordered)
    qualified = re.compile(
        rf"(?<![\w/.\-])((?:\$\{{[^}}]*\}}/|\$\w+/|(?:\.\./)+)?(?:src|tests/unit))/({alternation})(?![\w\-]|\.\w)"
    )
    word = re.compile(rf"(?<![\w/.\-])({alternation})/")
    mask = comment_mask(text) if bare == "comments" else None
    both = re.compile(f"{qualified.pattern}|{word.pattern}")
    out = []
    offset = 0
    for line in text.splitlines(keepends=True):
        start = offset
        offset += len(line)
        if skip_includes and INCLUDE.match(line):
            out.append(line)
            continue

        def respell(m: re.Match[str], start: int = start) -> str:
            if m.group(1) is not None:
                return f"{m.group(1)}/{new_of[m.group(2)]}"
            if bare == "all" or (mask is not None and mask[start + m.start()]):
                return f"{new_of[m.group(3)]}/"
            return m.group(0)

        out.append(both.sub(respell, line))
    return "".join(out)


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
        text = re.sub(rf"(?<![\w-]){re.escape(old)}(?![\w-])", new, text)
    return text


def overlaps(a: str, b: str) -> bool:
    return a == b or a.startswith(f"{b}/") or b.startswith(f"{a}/")


def check(repo: Path, moves: list[tuple[str, str]]) -> None:
    for i, first in enumerate(moves):
        for second in moves[i + 1 :]:
            for x in first:
                for y in second:
                    if overlaps(x, y):
                        raise Refused(
                            f"moves {first} and {second} overlap at {x} and {y}: run them separately"
                        )
    for old, new in moves:
        if (repo / "src" / "deluge" / old).exists():
            raise Refused(f"src/deluge/{old} exists: {old}/ would name two directories")
        if (repo / "src" / new).exists():
            raise Refused(f"src/{new} already exists")
        if (repo / "tests" / "unit" / new).exists():
            raise Refused(f"tests/unit/{new} already exists")
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
        after = path_tokens(after, moves, bare_spelling(rel), skip_includes=c_family)
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


def changed_lines(path: Path, after: str) -> list[str]:
    """`-old / +new` for each rewritten line that is not an include line (those follow the compiler, not the eye)."""
    before = path.read_text(encoding="utf-8", errors="surrogateescape")
    return [
        f"-{old.strip()} / +{new.strip()}"
        for old, new in zip(before.splitlines(), after.splitlines())
        if old != new and not INCLUDE.match(old)
    ]


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
        help="list the files a move would change and each rewritten line, and change none",
    )
    parser.add_argument("--repo", type=Path, default=REPO, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    moves = [tuple(m) for m in args.move]
    try:
        if args.dry_run:
            changes = plan(args.repo, moves, args.namespace, args.target)
            for rel in sorted(changes):
                print(rel)
                for line in changed_lines(args.repo / rel, changes[rel]):
                    print(f"{rel}: {line}")
            return 0
        apply(args.repo, moves, args.namespace, args.target)
    except Refused as refusal:
        print(f"relocate: refused: {refusal}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
