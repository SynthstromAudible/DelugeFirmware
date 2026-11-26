#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""The include-layer checker: every include in the tree against scripts/layers.toml.

The table holds four kinds of entry, and this script checks each:

- a library ([[library]]) may include only the C++ standard library, the system headers it lists, its own headers
  and the dependencies it declares. A file belongs to the deepest library whose root holds it. A quoted include is
  read as the compiler finds it: beside the including file first, then as written. Any #include that is not a plain
  <name> or "name" is a violation, so a macro or a comment cannot slip a header past the check, and so is any path
  with "..";
- a rule ([[rule]]) counts the includes matching its pattern in the files its globs cover. With transitive = true it
  follows every quoted include through the include roots and counts each forbidden header the file reaches. A count
  may not rise above scripts/layers_baseline.txt; a rule with no baseline row is held at zero;
- the tree ([tree] and [[directory]]): no directory under src/ that the table does not list, no file at the src/
  root it does not list, and no file under src/ at a path the legacy tree also has, because the legacy root precedes
  src/ on the include path and one of the two would be shadowed;
- the legacy tree's source-file count, which may only fall.

Usage:
    scripts/check_layers.py            # check, exit 1 on any violation
    scripts/check_layers.py --list     # print every count as file<TAB>rule<TAB>count and exit 0
    scripts/check_layers.py --update   # rewrite the baseline; refuses to raise a shrink-only count
"""

from __future__ import annotations

import argparse
import functools
import re
import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass
from fnmatch import fnmatch
from pathlib import Path

import tomllib

REPO = Path(__file__).resolve().parent.parent
SUFFIXES = frozenset(
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
DIRECTIVE = re.compile(r"^\s*#\s*include\b")
PLAIN_INCLUDE = re.compile(
    r'^\s*#\s*include\s*(?:<([^<>"]+)>|"([^<>"]+)")\s*(?://.*)?$'
)
ANY_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[>"]')
QUOTED_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')
LEGACY_RULE = "legacy-shrinks"

# The C++23 standard library headers, including the C compatibility headers (<cstdint>, not <stdint.h>).
STANDARD_HEADERS = frozenset(
    [
        "algorithm",
        "any",
        "array",
        "atomic",
        "barrier",
        "bit",
        "bitset",
        "charconv",
        "chrono",
        "codecvt",
        "compare",
        "complex",
        "concepts",
        "condition_variable",
        "coroutine",
        "deque",
        "exception",
        "execution",
        "expected",
        "filesystem",
        "flat_map",
        "flat_set",
        "format",
        "forward_list",
        "fstream",
        "functional",
        "future",
        "generator",
        "initializer_list",
        "iomanip",
        "ios",
        "iosfwd",
        "iostream",
        "istream",
        "iterator",
        "latch",
        "limits",
        "list",
        "locale",
        "map",
        "mdspan",
        "memory",
        "memory_resource",
        "mutex",
        "new",
        "numbers",
        "numeric",
        "optional",
        "ostream",
        "print",
        "queue",
        "random",
        "ranges",
        "ratio",
        "regex",
        "scoped_allocator",
        "semaphore",
        "set",
        "shared_mutex",
        "source_location",
        "span",
        "spanstream",
        "sstream",
        "stack",
        "stacktrace",
        "stdexcept",
        "stdfloat",
        "stop_token",
        "streambuf",
        "string",
        "string_view",
        "strstream",
        "syncstream",
        "system_error",
        "thread",
        "tuple",
        "type_traits",
        "typeindex",
        "typeinfo",
        "unordered_map",
        "unordered_set",
        "utility",
        "valarray",
        "variant",
        "vector",
        "version",
        "cassert",
        "cctype",
        "cerrno",
        "cfenv",
        "cfloat",
        "cinttypes",
        "climits",
        "clocale",
        "cmath",
        "csetjmp",
        "csignal",
        "cstdarg",
        "cstddef",
        "cstdint",
        "cstdio",
        "cstdlib",
        "cstring",
        "ctime",
        "cuchar",
        "cwchar",
        "cwctype",
    ]
)


@dataclass(frozen=True)
class Library:
    """A declared-dependency library: where it lives, its own include prefix, and what else it may include.

    A may_include entry ending in "/" admits every header under it; any other entry admits that one header. system
    lists the non-C++ headers (C headers, intrinsics) it may include in angle brackets.
    """

    name: str
    root: str
    prefix: str
    may_include: tuple[str, ...] = ()
    system: tuple[str, ...] = ()


@dataclass(frozen=True)
class Rule:
    """A forbidden include pattern, counted per file over the files its globs cover."""

    name: str
    files: tuple[str, ...]
    pattern: str
    exclude: tuple[str, ...] = ()
    allow: tuple[str, ...] = ()
    transitive: bool = False
    shrink_only: bool = False
    root_level: bool = False
    reason: str = ""


@dataclass(frozen=True)
class Tree:
    include_roots: tuple[str, ...]
    legacy: str
    scan_roots: tuple[str, ...]
    directories: tuple[str, ...]
    root_files: tuple[str, ...]


@dataclass(frozen=True)
class Config:
    tree: Tree
    libraries: tuple[Library, ...]
    rules: tuple[Rule, ...]


def load_config(path: Path) -> Config:
    raw = tomllib.loads(path.read_text())
    t = raw.get("tree", {})
    tree = Tree(
        include_roots=tuple(t.get("include_roots", ("src/deluge", "src"))),
        legacy=t.get("legacy", "src/deluge"),
        scan_roots=tuple(t.get("scan_roots", ("src", "tests"))),
        directories=tuple(d["path"] for d in raw.get("directory", [])),
        root_files=tuple(t.get("root_files", ())),
    )
    libraries = tuple(
        Library(
            name=entry["name"],
            root=entry["root"],
            prefix=entry["prefix"],
            may_include=tuple(entry.get("may_include", ())),
            system=tuple(entry.get("system", ())),
        )
        for entry in raw.get("library", [])
    )
    rules = tuple(
        Rule(
            name=entry["name"],
            files=tuple(entry["files"]),
            pattern=entry["pattern"],
            exclude=tuple(entry.get("exclude", ())),
            allow=tuple(entry.get("allow", ())),
            transitive=entry.get("transitive", False),
            shrink_only=entry.get("shrink_only", False),
            root_level=entry.get("root_level", False),
            reason=entry.get("reason", ""),
        )
        for entry in raw.get("rule", [])
    )
    names = [entry.name for entry in libraries] + [entry.name for entry in rules]
    repeated = sorted({name for name in names if names.count(name) > 1})
    if repeated:
        raise ValueError(f"{path}: names used more than once: {', '.join(repeated)}")
    for rule in rules:
        re.compile(rule.pattern)
    return Config(tree, libraries, rules)


def fetched_vendor(repo: Path) -> Callable[[Path], bool]:
    """A predicate for untracked files under a `third_party/` directory: a cache a build fetched, not project code."""
    result = subprocess.run(
        ["git", "ls-files", "-z"], cwd=repo, capture_output=True, check=False
    )
    tracked = (
        set(result.stdout.decode().split("\0")) if result.returncode == 0 else set()
    )

    def skip(path: Path) -> bool:
        rel = path.relative_to(repo)
        return "third_party" in rel.parts and rel.as_posix() not in tracked

    return skip


def source_files(repo: Path, root: str) -> list[Path]:
    base = repo / root
    if not base.is_dir():
        return []
    vendored = fetched_vendor(repo)
    return sorted(
        p
        for p in base.rglob("*")
        if p.is_file() and p.suffix in SUFFIXES and not vendored(p)
    )


@functools.cache
def read_lines(path: Path) -> tuple[str, ...]:
    return tuple(path.read_text(encoding="utf-8", errors="replace").splitlines())


def relative(repo: Path, path: Path) -> str:
    return path.relative_to(repo).as_posix()


def owner(config: Config, rel_path: str) -> Library | None:
    """The deepest library whose root holds @p rel_path."""
    best = None
    for library in config.libraries:
        if rel_path.startswith(library.root.rstrip("/") + "/") and (
            best is None or len(library.root) > len(best.root)
        ):
            best = library
    return best


def logical_of(repo: Path, config: Config, resolved: Path) -> str | None:
    """@p resolved relative to the first include root that holds it, or None outside them all."""
    for root in config.tree.include_roots:
        base = (repo / root).resolve()
        if resolved.is_relative_to(base):
            return resolved.relative_to(base).as_posix()
    return None


def logical_path(repo: Path, config: Config, including: Path, quoted: str) -> str:
    """A quoted include as the compiler finds it: the file beside @p including if there is one, else as written."""
    beside = including.parent / quoted
    if beside.is_file():
        logical = logical_of(repo, config, beside.resolve())
        if logical is not None:
            return logical
    return quoted


def library_allows(library: Library, logical: str) -> bool:
    if logical.startswith(library.prefix):
        return True
    return any(
        logical.startswith(dep) if dep.endswith("/") else logical == dep
        for dep in library.may_include
    )


def library_violations(config: Config, repo: Path) -> list[str]:
    found = []
    for library in config.libraries:
        for path in source_files(repo, library.root):
            rel_path = relative(repo, path)
            if owner(config, rel_path) is not library:
                continue
            for number, line in enumerate(read_lines(path), start=1):
                if not DIRECTIVE.match(line):
                    continue
                match = PLAIN_INCLUDE.match(line)
                ok = False
                if match is not None:
                    angle, quoted = match.groups()
                    if angle is not None:
                        ok = angle in STANDARD_HEADERS or angle in library.system
                    elif ".." not in Path(quoted).parts:
                        ok = library_allows(
                            library, logical_path(repo, config, path, quoted)
                        )
                if not ok:
                    found.append(f"{library.name}: {rel_path}:{number}: {line.strip()}")
    return found


def glob_match(rel_path: str, pattern: str) -> bool:
    """fnmatch, after expanding each {a,b} alternative. fnmatch's * crosses "/", so "dir/**" is everything under dir."""
    match = re.search(r"\{([^}]*)\}", pattern)
    if match:
        return any(
            glob_match(
                rel_path,
                pattern[: match.start()] + alternative + pattern[match.end() :],
            )
            for alternative in match.group(1).split(",")
        )
    return fnmatch(rel_path, pattern)


def covers(rule: Rule, rel_path: str) -> bool:
    return any(glob_match(rel_path, glob) for glob in rule.files) and not any(
        glob_match(rel_path, glob) for glob in rule.exclude
    )


def direct_count(rule: Rule, pattern: re.Pattern[str], path: Path) -> int:
    """The includes in @p path, as written, that match the rule's pattern and are not allowed."""
    count = 0
    for line in read_lines(path):
        match = ANY_INCLUDE.match(line)
        if (
            match
            and pattern.search(match.group(1))
            and match.group(1) not in rule.allow
        ):
            count += 1
    return count


def quoted_includes(path: Path) -> list[str]:
    return [
        match.group(1)
        for line in read_lines(path)
        if (match := QUOTED_INCLUDE.match(line))
    ]


def resolve(repo: Path, config: Config, including: Path, quoted: str) -> Path | None:
    """The file a quoted include names: beside @p including first, then under each include root in order."""
    for candidate in (
        including.parent / quoted,
        *(repo / root / quoted for root in config.tree.include_roots),
    ):
        if candidate.is_file():
            return candidate.resolve()
    return None


def reaches(
    config: Config, repo: Path, rule: Rule, start: Path
) -> dict[str, list[str]]:
    """Each forbidden header @p start reaches through quoted includes, with the chain of includes that reached it.

    A forbidden header is recorded and not followed. An include that resolves to no file is judged as written. A file
    outside every include root (a vendor header on its own -I) is not followed.
    """
    pattern = re.compile(rule.pattern)

    def forbidden(logical: str) -> bool:
        return bool(pattern.search(logical)) and logical not in rule.allow

    found: dict[str, list[str]] = {}
    visited = {start.resolve()}
    queue: list[tuple[Path, list[str]]] = [(start, [])]
    while queue:
        current, chain = queue.pop()
        for quoted in quoted_includes(current):
            resolved = resolve(repo, config, current, quoted)
            if resolved is None:
                if forbidden(quoted):
                    found.setdefault(quoted, [*chain, quoted])
                continue
            logical = logical_of(repo, config, resolved)
            if logical is None:
                continue
            if forbidden(logical):
                found.setdefault(logical, [*chain, logical])
                continue
            if resolved not in visited:
                visited.add(resolved)
                queue.append((resolved, [*chain, logical]))
    return found


def root_level_count(
    config: Config, repo: Path, rule: Rule, pattern: re.Pattern[str], path: Path
) -> int:
    """The quoted includes in @p path that name a file at the top of an include root, a root-level header.

    The include checker sees only quoted includes, so `<cstdint>` is never one; a slash-less name that is not a file at
    the top of a root (a header beside the including file) is not one either.
    """
    return sum(
        1
        for quoted in quoted_includes(path)
        if "/" not in quoted
        and pattern.search(quoted)
        and quoted not in rule.allow
        and any((repo / root / quoted).is_file() for root in config.tree.include_roots)
    )


def rule_count(
    config: Config, repo: Path, rule: Rule, pattern: re.Pattern[str], path: Path
) -> int:
    if rule.root_level:
        return root_level_count(config, repo, rule, pattern, path)
    if rule.transitive:
        return len(reaches(config, repo, rule, path))
    return direct_count(rule, pattern, path)


def explain(config: Config, repo: Path, file: str, rule_name: str) -> list[str]:
    """Extra lines for a count above its baseline: a transitive rule names each header reached and how."""
    rule = next((rule for rule in config.rules if rule.name == rule_name), None)
    if rule is None or not rule.transitive:
        return []
    return [
        f"{header}   (via {' -> '.join(chain)})"
        for header, chain in sorted(reaches(config, repo, rule, repo / file).items())
    ]


def rule_counts(config: Config, repo: Path) -> dict[tuple[str, str], int]:
    files = [
        path for root in config.tree.scan_roots for path in source_files(repo, root)
    ]
    counts: dict[tuple[str, str], int] = {}
    for rule in config.rules:
        pattern = re.compile(rule.pattern)
        for path in files:
            rel_path = relative(repo, path)
            if not covers(rule, rel_path):
                continue
            count = rule_count(config, repo, rule, pattern, path)
            if count:
                counts[(rel_path, rule.name)] = count
    return counts


def tree_violations(config: Config, repo: Path) -> list[str]:
    found = []
    src = repo / "src"
    for entry in sorted(src.iterdir()):
        if entry.is_dir() and entry.name not in config.tree.directories:
            found.append(
                f"tree: src/{entry.name}/ is a directory scripts/layers.toml does not list"
            )
        elif entry.is_file() and entry.name not in config.tree.root_files:
            found.append(
                f"tree: src/{entry.name} is a file at the src/ root that scripts/layers.toml does not list"
            )
    legacy = repo / config.tree.legacy
    # Source files only: an include names a source file, and src/deluge/CMakeLists.txt beside src/CMakeLists.txt
    # shadows nothing.
    for path in source_files(repo, config.tree.legacy):
        inner = path.relative_to(legacy).as_posix()
        if (src / inner).is_file():
            found.append(
                f"tree: src/{inner} and {relative(repo, path)} are both reached by an include of {inner}"
            )
    return found


def legacy_count(config: Config, repo: Path) -> int:
    return len(source_files(repo, config.tree.legacy))


def all_counts(config: Config, repo: Path) -> dict[tuple[str, str], int]:
    counts = rule_counts(config, repo)
    counts[(config.tree.legacy, LEGACY_RULE)] = legacy_count(config, repo)
    return counts


def is_shrink_only(config: Config, rule_name: str) -> bool:
    return rule_name == LEGACY_RULE or any(
        rule.name == rule_name and rule.shrink_only for rule in config.rules
    )


def reason_of(config: Config, rule_name: str) -> str:
    if rule_name == LEGACY_RULE:
        return "the legacy tree only shrinks: code leaves it by rewrite, and nothing is added"
    return next((rule.reason for rule in config.rules if rule.name == rule_name), "")


def load_baseline(path: Path) -> dict[tuple[str, str], int]:
    counts: dict[tuple[str, str], int] = {}
    if path.exists():
        for line in path.read_text().splitlines():
            if line and not line.startswith("#"):
                file, rule, count = line.split("\t")
                counts[(file, rule)] = int(count)
    return counts


def write_baseline(path: Path, counts: dict[tuple[str, str], int]) -> None:
    path.write_text(
        "# Include-layer baseline: file<TAB>rule<TAB>count. A count may only fall.\n"
        + "".join(
            f"{file}\t{rule}\t{count}\n"
            for (file, rule), count in sorted(counts.items())
        )
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--list",
        action="store_true",
        help="print every count as file<TAB>rule<TAB>count and exit 0",
    )
    parser.add_argument(
        "--update", action="store_true", help="rewrite the baseline from the tree"
    )
    parser.add_argument("--repo", type=Path, default=REPO, help=argparse.SUPPRESS)
    parser.add_argument("--config", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--baseline", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    repo = args.repo
    config = load_config(args.config or repo / "scripts" / "layers.toml")
    baseline_path = args.baseline or repo / "scripts" / "layers_baseline.txt"
    counts = all_counts(config, repo)

    if args.list:
        for (file, rule), count in sorted(counts.items()):
            print(f"{file}\t{rule}\t{count}")
        return 0

    baseline = load_baseline(baseline_path)
    if args.update:
        if baseline_path.exists():
            grown = sorted(
                key
                for key, count in counts.items()
                if is_shrink_only(config, key[1]) and count > baseline.get(key, 0)
            )
            for file, rule in grown:
                print(
                    f"{file}: rule '{rule}' count {counts[(file, rule)]} > baseline {baseline.get((file, rule), 0)}, "
                    "which --update may not raise"
                )
            if grown:
                print(
                    f"{len(grown)} shrink-only count(s) grew; baseline not written.",
                    file=sys.stderr,
                )
                return 1
        write_baseline(baseline_path, counts)
        print(f"wrote {len(counts)} entries to {baseline_path}")
        return 0

    problems = library_violations(config, repo) + tree_violations(config, repo)
    for problem in problems:
        print(problem)
    grown = 0
    for (file, rule), count in sorted(counts.items()):
        allowed = baseline.get((file, rule), 0)
        if count > allowed:
            print(
                f"{file}: rule '{rule}' count {count} > baseline {allowed} ({reason_of(config, rule)})"
            )
            for line in explain(config, repo, file, rule):
                print(f"    {line}")
            grown += 1
    if problems or grown:
        print(
            f"FAIL: {len(problems)} include or tree violation(s), {grown} count(s) above the baseline. Remove the "
            "include, or for an agreed move of a count that is not shrink-only, run --update.",
            file=sys.stderr,
        )
        return 1
    print("OK: every include is where scripts/layers.toml allows it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
