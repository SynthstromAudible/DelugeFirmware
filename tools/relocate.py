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
- an OLD under the legacy root (`deluge/x`) is also spelled `x/` in includes and tokens, since src/deluge is an
  include root; that spelling becomes NEW, or NEW without its `deluge/` when NEW stays under the legacy root;
- src/OLD and tests/unit/OLD are moved with git mv.
Each --namespace OLD=NEW respells OLD and every name under it (OLD::...) in every tracked text file, code, strings and
comments alike (read the dry run for fixtures that spell OLD on purpose); an unqualified use that leaned on the old
enclosing namespace is left for the compiler to name. Each --target OLD=NEW renames a CMake target as a whole word,
in every tracked text file the same way.

A move is refused, and nothing is changed, when src/deluge/OLD or src/deluge/NEW exists (the legacy root precedes
src/ on the include path, so the spelling would name two directories) or when src/NEW or tests/unit/NEW already exists, or when two moves name
the same directory or one inside the other (a chain would move on disk differently from how the text reads). The
north-star layout document is never rewritten: its tables name today's paths beside their targets. Nor is any file
under third_party/, toolchain/, lib/ or website/. An include spelled from tests/ (`"unit/OLD/..."`) is not respelled.

--repo-move OLD NEW moves any tracked directory (both relative to the repo root) with git mv, and cannot be combined
with --move. In every tracked text file outside the never-rewritten set, and never in a Cargo.lock, the token OLD is
respelled NEW wherever it starts a path: after the start of a line, whitespace, a quote, a backtick, `(`, `=`, `:`, `,`
or `[`, `|` or `^` (before a `/` only), a variable prefix (`${X}/`, `$X/`), `./` or a `../` chain (never after a `word/`
component), and before `/`, a quote, a backtick, whitespace, `)` or the end. A single-component OLD (`sim`) is respelled
only before a `/` and as the bare directory of a CMake `-S`, `add_subdirectory(` or `${X}/` position; every other bare
mention is listed under "bare mentions, not rewritten". The tool's own tests and fixtures (tools/test_relocate.py,
test_compare_*.py, tools/testdata/) are never rewritten. In every Cargo.toml, each `path = "..."`, `workspace = "..."`
and `members`/`exclude`/`default-members` entry whose target lies in OLD, or whose manifest lies in OLD, is recomputed
as the relative path from the manifest's new directory to the target's new location (the edits are line-level, so
comments and formatting stay). A package that was a workspace member and would no longer sit under its root gets
`workspace = "<path to the root>"` in `[package]`. Each --crate OLD=NEW renames a crate: `name =` in `[package]` and
`[lib]`, dependency keys, dotted ones too (a key that aliases another package, `x = { package = "OLD", ... }`, keeps its
key and gets the new package name), `package = "OLD"`, `[profile.*.package.OLD]` and `dep:OLD` / `OLD/feature` entries
in every Cargo.toml; the identifiers `OLD` in `use`, `extern crate` and `OLD::` position of every .rs file; and
`libOLD.a`, `-p OLD` and `--package OLD` in every text file. A hyphenated OLD also renames its underscore spelling in
`[lib] name`, those .rs positions and `libOLD.a`. A crate name in prose, a comment or a backtick is never rewritten (a
C-ABI function can share it); each is listed under "crate-name mentions, not rewritten" (comments only in C-family and
.rs files). It is refused, before anything is written, when NEW exists or is not a normal repo-relative path, when OLD
and NEW contain one another, or when OLD is not a tracked directory. Depth arithmetic in anything but a manifest, paths
built from variables, and build-script joins are not followed: grep the old spellings after applying.

Usage:
    tools/relocate.py --move OLD NEW [--move OLD NEW ...] [--namespace OLD=NEW ...] [--target OLD=NEW ...] [--dry-run]
    tools/relocate.py --repo-move OLD NEW [--crate OLD=NEW ...] [--dry-run]
"""

from __future__ import annotations

import argparse
import difflib
import fnmatch
import posixpath
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
NEVER = frozenset(
    {
        "docs/dev/code_layout_north_star.md",
        "tools/test_relocate.py",
        "tools/test_compare_strings.py",
        "tools/test_compare_codegen.py",
    }
)
NEVER_UNDER = ("third_party/", "toolchain/", "lib/", "website/", "tools/testdata/")
INCLUDE = re.compile(r'^(\s*#\s*include\s*)([<"])([^">]+)([>"])')
INCLUDE_ROOTS = ("src/deluge", "src")
LEGACY = "deluge/"


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


def spellings(old: str, new: str) -> list[tuple[str, str]]:
    """How an include or a bare token spells OLD, each with its respelling.

    OLD under the legacy root (`deluge/x`) is also spelled `x/`, resolved through src/deluge; that spelling becomes
    NEW's shortest: `y/` for a NEW outside the legacy root, `x_old/` for `deluge/x_old`.
    """
    pairs = [(old, new)]
    if old.startswith(LEGACY):
        pairs.append((old[len(LEGACY) :], new.removeprefix(LEGACY)))
    return sorted(pairs, key=lambda p: -len(p[0]))


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
            if not target.startswith(f"src/{old}/"):
                continue
            for was, now in spellings(old, new):
                if spelled.startswith(f"{was}/"):
                    lines[i] = line.replace(
                        f"{match.group(2)}{spelled}",
                        f"{match.group(2)}{now}/{spelled[len(was) + 1 :]}",
                        1,
                    )
                    break
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
    spelled = sorted(
        (pair for old, new in moves for pair in spellings(old, new)),
        key=lambda p: -len(p[0]),
    )
    spelled_new = dict(spelled)
    word = re.compile(
        rf"(?<![\w/.\-])({'|'.join(re.escape(was) for was, _ in spelled)})/"
    )
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
                return f"{spelled_new[m.group(3)]}/"
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
        if (repo / "src" / "deluge" / new).exists():
            raise Refused(f"src/deluge/{new} exists: {new}/ would name two directories")
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


# ---------------------------------------------------------------------------------------------------------------------
# --repo-move: any tracked directory, Cargo path arithmetic, crate renames

SENT_OPEN, SENT_CLOSE = "\x00", "\x01"
DEP_TABLE = re.compile(
    r"(^|\.)(dependencies|dev-dependencies|build-dependencies)$|^patch\."
)
DEP_BLOCK = re.compile(
    r"^(.*\.)?(dependencies|dev-dependencies|build-dependencies)\.([^.]+)$"
)
HEADER = re.compile(r"^\s*\[\[?\s*([^\]]+?)\s*\]\]?\s*(#.*)?$")
WS_ARRAY = re.compile(r"^\s*(members|exclude|default-members)\s*=\s*\[")


def moved(path: str, old: str, new: str) -> str:
    if path == old or path.startswith(f"{old}/"):
        return new + path[len(old) :]
    return path


def code_strings(line: str) -> tuple[list[tuple[int, int]], int]:
    """Spans (inside the quotes) of the strings in a TOML line, and where its comment starts (len(line) if none)."""
    spans: list[tuple[int, int]] = []
    i, n = 0, len(line)
    while i < n:
        c = line[i]
        if c == "#":
            return spans, i
        if c in "\"'":
            j = i + 1
            while j < n and line[j] != c:
                j += 2 if (c == '"' and line[j] == "\\") else 1
            spans.append((i + 1, min(j, n)))
            i = j + 1
        else:
            i += 1
    return spans, n


def sentinel(value: str) -> str:
    return f"{SENT_OPEN}{value}{SENT_CLOSE}"


def split_sentinels(text: str) -> list[str]:
    return re.split(f"({SENT_OPEN}.*?{SENT_CLOSE})", text, flags=re.DOTALL)


PATH_START = r"""(?:(?P<pre>\$\w+/)|(?<![^\s"'`(=:,\[])|(?<=\}/)|(?<=\./))"""


def rewrite_path_tokens(text: str, old: str, new: str) -> str:
    """Respells OLD as NEW where it starts a path; each respelling is wrapped in sentinels, text already wrapped is kept.

    A path token follows the start of a line, whitespace, a quote, a backtick, `(`, `=`, `:`, `,` or `[`, a variable
    prefix (`${X}/`, `$X/`), `./` or a `../` chain, never a `word/` component. A single-component OLD is respelled only
    before a `/`, and as the bare directory of a CMake `-S`, `add_subdirectory(` or `${X}/` position.
    """
    o = re.escape(old)
    if "/" in old:
        patterns = [
            rf"{PATH_START}{o}(?=[/\"'`\s),\]]|$)",
            rf"(?<=[|^]){o}(?=/)",
        ]
    else:
        patterns = [
            rf"{PATH_START}{o}(?=/)",
            rf"(?<=[|^]){o}(?=/)",
            rf"(?P<pre>-S\s+|add_subdirectory\(\s*|\$\{{[^}}]*\}}/|\$\w+/){o}(?=[\s\"'`)]|$)",
        ]

    def respell(m: re.Match[str]) -> str:
        return f"{m.groupdict().get('pre') or ''}{sentinel(new)}"

    for pattern in patterns:
        text = "".join(
            part if i % 2 else re.sub(pattern, respell, part, flags=re.MULTILINE)
            for i, part in enumerate(split_sentinels(text))
        )
    return text


def strip_sentinels(text: str) -> str:
    return text.replace(SENT_OPEN, "").replace(SENT_CLOSE, "")


def masked(text: str) -> str:
    """The text with each sentinel-wrapped span blanked, so a scan skips what was already rewritten."""
    return re.sub(
        f"{SENT_OPEN}.*?{SENT_CLOSE}",
        lambda m: "\x02" * len(m.group()),
        text,
        flags=re.DOTALL,
    )


def mentions(rel: str, text: str, pattern: str, comments_only: bool) -> list[str]:
    """`rel:LINE: text` for each match of `pattern` outside rewritten spans (in comments only for code files)."""
    hidden = masked(text)
    mask = (
        comment_mask(text) if comments_only and Path(rel).suffix in C_FAMILY else None
    )
    lines = text.splitlines()
    found: dict[int, None] = {}
    for m in re.finditer(pattern, hidden):
        line = hidden.count("\n", 0, m.start())
        if mask is not None and not mask[m.start()]:
            continue
        column = m.start() - (hidden.rfind("\n", 0, m.start()) + 1)
        if (
            comments_only
            and Path(rel).suffix == ".rs"
            and "//" not in lines[line][:column]
        ):
            continue
        found[line] = None
    return [f"{rel}:{n + 1}: {strip_sentinels(lines[n]).strip()}" for n in found]


class Workspaces:
    """Which manifests are explicit members of which workspace roots, read against the layout before the move."""

    def __init__(self, repo: Path, manifests: list[str]):
        self.members: dict[str, list[str]] = {}
        self.pointer: dict[str, str] = {}
        self.globs: list[tuple[str, str]] = []
        dirs = {posixpath.dirname(m) or "." for m in manifests}
        for rel in manifests:
            root = posixpath.dirname(rel) or "."
            lists = workspace_lists(repo / rel)
            if lists is None:
                continue
            excluded = {
                posixpath.normpath(posixpath.join(root, e))
                for e in lists.get("exclude", [])
            }
            for entry in lists.get("members", []):
                if any(ch in entry for ch in "*?["):
                    pattern = posixpath.normpath(posixpath.join(root, entry))
                    self.globs.append((rel, pattern))
                    for d in dirs:
                        if fnmatch.fnmatch(d, pattern) and d not in excluded:
                            self.members.setdefault(d, []).append(root)
                    continue
                target = posixpath.normpath(posixpath.join(root, entry))
                if target not in excluded:
                    self.members.setdefault(target, []).append(root)


def workspace_lists(path: Path) -> dict[str, list[str]] | None:
    """The `[workspace]` members/exclude entries of a manifest, or None when it has no `[workspace]`."""
    lists: dict[str, list[str]] = {}
    section = None
    key = None
    has = False
    for line in path.read_text(encoding="utf-8", errors="surrogateescape").splitlines():
        header = HEADER.match(line)
        if header and key is None:
            section = header.group(1)
            has = has or section == "workspace"
            continue
        if section != "workspace":
            continue
        start = WS_ARRAY.match(line)
        if start and key is None:
            key = start.group(1)
        if key is not None:
            spans, comment = code_strings(line)
            lists.setdefault(key, []).extend(line[a:b] for a, b in spans)
            if "]" in re.sub(r'"[^"]*"|\'[^\']*\'', "", line[:comment]):
                key = None
    return lists if has else None


def insert_workspace_pointer(text: str, pointer: str) -> str:
    """Adds `workspace = "<pointer>"` to `[package]`, after its `name =` line when it has one."""
    lines = text.splitlines(keepends=True)
    in_package = False
    at = None
    for i, line in enumerate(lines):
        header = HEADER.match(line.rstrip("\r\n"))
        if header:
            in_package = header.group(1) == "package"
            if in_package and at is None:
                at = i + 1
        elif in_package and re.match(r'\s*name\s*=\s*"', line):
            at = i + 1
            break
    if at is None:
        return text
    if not lines[at - 1].endswith("\n"):
        lines[at - 1] += "\n"
    lines.insert(at, f'workspace = "{pointer}"\n')
    return "".join(lines)


def rewrite_manifest(rel, text, old, new, crates, ws: Workspaces) -> str:
    """Cargo path arithmetic and crate renames for one Cargo.toml, line by line, every other byte untouched."""
    here = posixpath.dirname(rel) or "."
    here_new = moved(here, old, new)
    lines = text.splitlines(keepends=True)
    sections: list[str | None] = []
    current = None
    alias_blocks: set[str] = set()
    block_name = None
    for line in lines:
        header = HEADER.match(line.rstrip("\r\n"))
        if header:
            current = header.group(1)
            block_name = current if DEP_BLOCK.match(current) else None
        elif block_name and re.search(r'(?<![\w-])package\s*=\s*"', line):
            alias_blocks.add(block_name)
        sections.append(current)

    def relocated(value: str) -> str:
        if posixpath.isabs(value):
            return value
        target = posixpath.normpath(posixpath.join(here, value))
        if target.startswith(".."):
            return value
        new_value = posixpath.relpath(moved(target, old, new), here_new)
        return value if new_value == posixpath.relpath(target, here) else new_value

    out: list[str] = []
    in_array = False
    for line, section in zip(lines, sections):
        body = line.rstrip("\r\n")
        tail = line[len(body) :]
        header = HEADER.match(body)
        comment = code_strings(body)[1]
        if header:
            name = header.group(1)
            block = DEP_BLOCK.match(name)
            for crate_old, crate_new in crates:
                if block and block.group(3) == crate_old and name not in alias_blocks:
                    body = body.replace(f".{crate_old}]", f".{crate_new}]", 1)
                if re.search(
                    rf"(^|\.)profile\.[^.]+\.package\.{re.escape(crate_old)}$", name
                ):
                    body = body.replace(
                        f".package.{crate_old}]", f".package.{crate_new}]", 1
                    )
            out.append(body + tail)
            in_array = False
            continue

        # path / workspace values (their text is protected from the token pass)
        def path_value(m: re.Match[str]) -> str:
            return f'{m.group(1)}"{sentinel(relocated(m.group(2)))}"'

        head, rest = body[:comment], body[comment:]
        if section == "workspace" and (in_array or WS_ARRAY.match(head)):
            in_array = True
            pieces, last = [], 0
            for a, b in code_strings(head)[0]:
                pieces.append(head[last:a] + sentinel(relocated(head[a:b])))
                last = b
            head = "".join(pieces) + head[last:]
            if "]" in re.sub(r'"[^"]*"|\'[^\']*\'', "", head):
                in_array = False
        else:
            head = re.sub(
                r'((?<![\w-])(?:path|workspace)\s*=\s*)"([^"]*)"', path_value, head
            )
        for crate_old, crate_new in crates:
            names = [(crate_old, crate_new)]
            if section == "lib" and "-" in crate_old:
                names.append((crate_old.replace("-", "_"), crate_new.replace("-", "_")))
            for name_old, name_new in names if section in ("package", "lib") else []:
                head = re.sub(
                    rf'^(\s*name\s*=\s*"){re.escape(name_old)}(")',
                    rf"\g<1>{name_new}\2",
                    head,
                )
            if (
                section
                and DEP_TABLE.search(section)
                and not re.search(r"(?<![\w-])package\s*=", head)
            ):
                head = re.sub(
                    rf"^(\s*){re.escape(crate_old)}(\s*[=.])",
                    rf"\g<1>{crate_new}\2",
                    head,
                )
            head = re.sub(
                rf'((?<![\w-])package\s*=\s*"){re.escape(crate_old)}(")',
                rf"\g<1>{crate_new}\2",
                head,
            )
            if section == "features":
                head = re.sub(
                    rf'"(dep:)?{re.escape(crate_old)}(\??/[^"]*|)"',
                    lambda m, n=crate_new: (
                        (f'"{m.group(1) or ""}{n}{m.group(2)}"')
                        if (m.group(1) or m.group(2))
                        else m.group(0)
                    ),
                    head,
                )
        out.append(head + rest + tail)

    result = "".join(out)
    # a package that stops being under its workspace root names it
    if here in ws.members and not re.search(
        r'^\s*workspace\s*=\s*"', result, re.MULTILINE
    ):
        for root in (r for r in ws.members[here] if r != here):
            root_new = moved(root, old, new)
            either_moved = here_new != here or root_new != root
            under_root = root_new == "." or here_new.startswith(f"{root_new}/")
            if either_moved and not under_root:
                pointer = posixpath.relpath(root_new, here_new)
                result = insert_workspace_pointer(result, sentinel(pointer))
                break
    return result


def crate_idents(text: str, crates: list[tuple[str, str]]) -> str:
    for old, new in crates:
        o = re.escape(old)
        text = re.sub(rf"(?<![\w:]){o}(?=\s*::)", new, text)
        text = re.sub(rf"(\buse\s+(?:::)?){o}\b", rf"\g<1>{new}", text)
        text = re.sub(rf"(\bextern\s+crate\s+){o}\b", rf"\g<1>{new}", text)
    return text


def crate_words(
    text: str, crates: list[tuple[str, str]], archives: list[tuple[str, str]]
) -> str:
    """`libOLD.a` for each archive pair, and `-p OLD` / `--package OLD` for each crate: spellings no other name shares."""
    for old, new in archives:
        text = re.sub(rf"\blib{re.escape(old)}\.a\b", f"lib{new}.a", text)
    for old, new in crates:
        text = re.sub(
            rf"(?<![\w-])(-p\s+|--package[\s=]+){re.escape(old)}(?![\w-])",
            rf"\g<1>{new}",
            text,
        )
    return text


def underscore_pairs(crates: list[tuple[str, str]]) -> list[tuple[str, str]]:
    """The Rust spelling of each hyphenated crate: `deluge-bsp-rust` is `deluge_bsp_rust` in code and archive names."""
    return [(o.replace("-", "_"), n.replace("-", "_")) for o, n in crates if "-" in o]


def check_repo_move(repo: Path, old: str, new: str) -> None:
    for name, path in (("OLD", old), ("NEW", new)):
        if not path or posixpath.isabs(path) or posixpath.normpath(path) != path:
            raise Refused(f"{name} {path!r} is not a normal repo-relative path")
        if path == "." or path.startswith("../"):
            raise Refused(f"{name} {path!r} leaves the repo")
    if overlaps(old, new):
        raise Refused(
            f"{old} and {new} contain one another"
            if old != new
            else "OLD and NEW are the same"
        )
    if (repo / new).exists():
        raise Refused(f"{new} already exists")
    if not any(rel.startswith(f"{old}/") for rel in tracked(repo)):
        raise Refused(f"{old} is not a tracked directory")


class RepoPlan:
    """What a repo move would write, and the mentions of OLD it leaves for the implementer to read."""

    def __init__(self) -> None:
        self.changes: dict[str, str] = {}
        self.bare: list[str] = []
        self.crate_mentions: list[str] = []

    def report(self) -> list[str]:
        out = []
        if self.bare:
            out += ["bare mentions, not rewritten:"] + [f"  {m}" for m in self.bare]
        if self.crate_mentions:
            out += ["crate-name mentions, not rewritten:"] + [
                f"  {m}" for m in self.crate_mentions
            ]
        return out


def plan_repo_move(
    repo: Path, old: str, new: str, crates: list[tuple[str, str]]
) -> RepoPlan:
    check_repo_move(repo, old, new)
    files = tracked(repo)
    manifests = [
        rel
        for rel in files
        if Path(rel).name == "Cargo.toml" and (repo / rel).is_file()
    ]
    ws = Workspaces(repo, manifests)
    dirs = {posixpath.dirname(m) or "." for m in manifests}
    for root, pattern in ws.globs:
        if any(
            fnmatch.fnmatch(d, pattern) and (d == old or d.startswith(f"{old}/"))
            for d in dirs
        ):
            raise Refused(
                f"{root} lists members by glob ({pattern}); name the moved crate explicitly first"
            )
    rust_names = crates + underscore_pairs(crates)
    bare_word = rf"(?<![\w.-]){re.escape(old)}(?![\w-])(?!\.\w)"
    crate_word = "|".join(rf"(?<![\w-]){re.escape(o)}(?![\w-])" for o, _ in rust_names)
    result = RepoPlan()
    for rel in files:
        if (
            not is_text(rel)
            or Path(rel).name == "Cargo.lock"
            or not (repo / rel).is_file()
        ):
            continue
        before = (repo / rel).read_text(encoding="utf-8", errors="surrogateescape")
        after = before
        if Path(rel).name == "Cargo.toml":
            after = rewrite_manifest(rel, after, old, new, crates, ws)
        after = rewrite_path_tokens(after, old, new)
        if "/" not in old:
            result.bare += mentions(rel, after, bare_word, comments_only=False)
        if Path(rel).suffix == ".rs":
            after = crate_idents(after, rust_names)
        after = crate_words(after, crates, rust_names)
        if crates:
            result.crate_mentions += mentions(
                rel, after, crate_word, comments_only=Path(rel).name != "Cargo.toml"
            )
        after = strip_sentinels(after)
        if after != before:
            result.changes[rel] = after
    return result


def apply_repo_move(
    repo: Path, old: str, new: str, crates: list[tuple[str, str]]
) -> RepoPlan:
    plan = plan_repo_move(repo, old, new, crates)
    for rel, text in plan.changes.items():
        (repo / rel).write_text(text, encoding="utf-8", errors="surrogateescape")
    (repo / new).parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["git", "mv", old, new], cwd=repo, check=True)
    return plan


def diff_lines(path: Path, after: str) -> list[str]:
    """`-old / +new` for each replaced line, `+new` for each added one."""
    before = path.read_text(encoding="utf-8", errors="surrogateescape").splitlines()
    now = after.splitlines()
    out = []
    for tag, a0, a1, b0, b1 in difflib.SequenceMatcher(
        None, before, now, autojunk=False
    ).get_opcodes():
        if tag == "replace" and a1 - a0 == b1 - b0:
            out += [
                f"-{before[a0 + k].strip()} / +{now[b0 + k].strip()}"
                for k in range(a1 - a0)
            ]
        elif tag != "equal":
            out += [f"-{line.strip()}" for line in before[a0:a1]] + [
                f"+{line.strip()}" for line in now[b0:b1]
            ]
    return out


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
        "--move", nargs=2, action="append", metavar=("OLD", "NEW"), default=[]
    )
    parser.add_argument("--repo-move", nargs=2, metavar=("OLD", "NEW"))
    parser.add_argument("--crate", type=pair, action="append", default=[])
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
    if bool(args.repo_move) == bool(moves):
        parser.error("give exactly one of --move and --repo-move")
    if args.crate and not args.repo_move:
        parser.error("--crate goes with --repo-move")
    try:
        if args.repo_move:
            old, new = args.repo_move
            if args.dry_run:
                moved_plan = plan_repo_move(args.repo, old, new, args.crate)
                print(f"git mv {old} {new}")
                for rel in sorted(moved_plan.changes):
                    print(rel)
                    for line in diff_lines(args.repo / rel, moved_plan.changes[rel]):
                        print(f"{rel}: {line}")
            else:
                moved_plan = apply_repo_move(args.repo, old, new, args.crate)
            print("\n".join(moved_plan.report()))
            return 0
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
