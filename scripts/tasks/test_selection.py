"""Which gates a change can reach: tests/registry.toml's areas, each gate's own files, and the changed files.
See tests/README.md, Selecting by change, and test_test_selection.py."""

import functools
import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

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


def changed_files(root: Path, ref: str) -> list[str]:
    """The repository paths changed since the merge base of `ref` and HEAD, the working tree included (staged,
    unstaged, untracked and not ignored); a rename gives both paths. Sorted, each once."""

    def git(*args: str) -> str:
        done = subprocess.run(
            ["git", *args], cwd=root, capture_output=True, text=True, check=False
        )
        if done.returncode != 0:
            raise ValueError(
                f"--since {ref}: {done.stderr.strip() or 'git ' + ' '.join(args) + ' failed'}"
            )
        return done.stdout

    base = git("merge-base", ref, "HEAD").strip()
    diffed = git("diff", "--name-only", "--no-renames", base).split("\n")
    untracked = git("ls-files", "--others", "--exclude-standard").split("\n")
    return sorted({p for p in diffed + untracked if p})


def own_files(h) -> list[str]:
    """The globs of `h`'s own files: its directory, the files its commands and check name, its scenario's driver,
    its corpus runner and its baselines. A change to any of them reaches `h` whatever its areas."""
    own = []
    if h.path not in (".", ""):
        own.append(f"{h.path.rstrip('/')}/**")
    for arg in [*h.build, *h.run]:
        if "/" in arg or arg.endswith((".py", ".sh")):
            rel = arg.removeprefix("./")
            own.append(
                rel
                if h.path in (".", "") or rel.startswith(h.path)
                else f"{h.path}/{rel}"
            )
    if h.check:
        own.append(h.check)
    if h.scenario:
        own.append(f"tests/integration/scenarios/**/{h.scenario}*")
    if h.corpus_runner:
        own.append(f"tests/integration/corpus/{h.corpus_runner}_run.py")
    for b in h.baseline:
        own += [b, f"{b.rstrip('/')}/**"]
    return own


@dataclass
class SinceResult:
    """The gates selected, what reached each (or "widened"), and the widening messages."""

    selected: list
    reasons: dict[str, str]
    messages: list[str]


def since(
    selection: list,
    areas: Areas,
    changed: list[str],
    tier: str = "",
    registry: list | None = None,
) -> SinceResult:
    """The gates of `selection` the `changed` files reach, each with the first area or own file that reached it. An
    every-gate file, or a file in no area and in the own files of no gate of `registry` (default: `selection`), selects
    the whole `selection`, with a message: a gate outside the selection owning the file means it reaches nothing here."""
    owned = {h.name: own_files(h) for h in selection}
    anyone = (
        [own_files(h) for h in registry]
        if registry is not None
        else list(owned.values())
    )
    messages = []
    for path in changed:
        if any_match(areas.every_gate, path):
            messages.append(f"--since: {path} reaches every gate")
        elif not (
            any_match(areas.no_gate, path)
            or any(any_match(globs, path) for globs in areas.areas.values())
            or any(any_match(globs, path) for globs in anyone)
        ):
            messages.append(
                f"--since: {path} is in no area: running the whole {tier + ' ' if tier else ''}selection"
            )
    if messages:
        return SinceResult(
            list(selection), {h.name: "widened" for h in selection}, messages
        )
    gated = [p for p in changed if not any_match(areas.no_gate, p)]
    selected, reasons = [], {}
    for h in selection:
        why = None
        for path in gated:
            if AREA_ALL in h.areas:
                why = f"all: {path}"
            elif hit := next(
                (a for a in h.areas if any_match(areas.areas.get(a, []), path)), None
            ):
                why = f"area {hit}: {path}"
            elif any_match(owned[h.name], path):
                why = f"own file: {path}"
            if why:
                break
        if why:
            selected.append(h)
            reasons[h.name] = why
    return SinceResult(selected, reasons, messages)


# The trees whose every tracked file must be in a table or a gate's own files; outside them (docs, the website, CI
# configuration) an unlisted file widens a `--since` run like any other, but is not reported.
CHECKED_TREES = ("src/", "include/", "tests/", "tools/", "scripts/")


def completeness_problems(
    entries: list[Entry], areas: Areas, tracked: list[str]
) -> list[str]:
    """The tracked files under CHECKED_TREES that no table and no gate's own files name."""
    globs = (
        [g for gs in areas.areas.values() for g in gs]
        + areas.no_gate
        + areas.every_gate
    )
    globs += [g for e in entries for g in e.own]
    return [
        f"{p} is in no area, no-gate or every-gate table and no gate's own files"
        for p in tracked
        if p.startswith(CHECKED_TREES) and not any_match(globs, p)
    ]
