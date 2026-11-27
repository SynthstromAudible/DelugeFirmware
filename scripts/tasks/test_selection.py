"""Which gates a change can reach: tests/registry.toml's areas, each gate's own files, and the changed files.
See tests/README.md, Selecting by change, and test_test_selection.py."""

import functools
import re
from dataclasses import dataclass, field

AREA_ALL = "all"


@dataclass
class Areas:
    """The registry's [area.<name>], [no-gate] and [every-gate] tables: glob lists relative to the repository root."""

    areas: dict[str, list[str]] = field(default_factory=dict)
    no_gate: list[str] = field(default_factory=list)
    every_gate: list[str] = field(default_factory=list)


@dataclass
class Entry:
    """What selection needs of a registry entry: its name, its areas and its own files (globs)."""

    name: str
    areas: list[str]
    own: list[str]


@functools.cache
def glob_regex(pattern: str) -> re.Pattern:
    """`pattern` as a regex over a whole repository path: `**` any number of directories, `*` and `?` within one."""
    out = []
    i = 0
    while i < len(pattern):
        if pattern.startswith("**/", i):
            out.append("(?:.*/)?")
            i += 3
        elif pattern.startswith("**", i):
            out.append(".*")
            i += 2
        elif pattern[i] == "*":
            out.append("[^/]*")
            i += 1
        elif pattern[i] == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(pattern[i]))
            i += 1
    return re.compile("".join(out) + r"\Z")


def matches(pattern: str, path: str) -> bool:
    """Whether repository path `path` matches glob `pattern`."""
    return glob_regex(pattern).match(path) is not None


def any_match(patterns: list[str], path: str) -> str | None:
    """The first of `patterns` that `path` matches, or None."""
    return next((p for p in patterns if matches(p, path)), None)


def consistency_problems(
    entries: list[Entry], areas: Areas, tracked: list[str]
) -> list[str]:
    """What is wrong with the areas: an entry naming no known area, an area no entry uses, a glob matching no tracked
    file."""
    problems = []
    for e in entries:
        problems += [
            f"{e.name}: unknown area '{a}'"
            for a in e.areas
            if a != AREA_ALL and a not in areas.areas
        ]
    used = {a for e in entries for a in e.areas}
    problems += [
        f"area '{a}' is used by no entry" for a in areas.areas if a not in used
    ]
    tables = [(f"area '{a}'", globs) for a, globs in areas.areas.items()]
    tables += [("no-gate", areas.no_gate), ("every-gate", areas.every_gate)]
    for label, globs in tables:
        problems += [
            f"{label}: glob '{g}' matches no tracked file"
            for g in globs
            if not any(matches(g, t) for t in tracked)
        ]
    return problems
