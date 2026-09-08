#!/usr/bin/env python3
r"""Did a commit move the line a citation points at, without moving the citation?

THE CLASS THIS EXISTS FOR, AND WHY THE OTHER GATE CANNOT SEE IT
--------------------------------------------------------------
`scripts/check_citations.py --strict` asks whether a citation lands somewhere structurally
impossible -- past EOF, inside a licence header, off its named symbol's brace-matched body. A
citation that slides three lines *inside a doc comment* satisfies all of that perfectly. The trap
fired **seven times in plan F1 alone** and `--strict` exited 0 before and after every single one.
F1 task 8's sweep is the sharpest example: 68 citations had moved, and the gate said 0 both sides.

The mechanism that does see it needs no symbol predicate at all, and it is what four of this
stack's tasks ended up doing by hand: **for every citation into a file the commit modified,
compare the TEXT of the cited line before the commit against the text of the (possibly rewritten)
cited line after it.** Content, not arithmetic -- a shift map is a hypothesis and the file is the
fact.

    scripts/check_citation_shift.py                # HEAD^..HEAD
    scripts/check_citation_shift.py A..B           # any range
    scripts/check_citation_shift.py --strict A..B  # exit 1 on any difference
    scripts/check_citation_shift.py --per-commit A..B   # one report per commit in the range

It is deliberately NOT a CI gate. It answers a question about a *diff*, so it has no fixed point
to be green at, and its unpaired-citation report (below) is advice rather than a verdict.

WHAT IT COMPARES
----------------
For each file the range touched, and each citation into that file from a scanned citing file:
the citing file's citations are paired **positionally per cited path** -- the k-th citation into
`view.cpp` in the old version with the k-th in the new -- and then both endpoints of each range
are compared as text. Both endpoints, because the single most common ad-hoc rewriter in this stack
matches `` :\d+ `` and so rewrites `scene.h:500-552` into `510-552`, leaving the second endpoint
behind; comparing only the start would agree with that bug.

FOUR THINGS IT STRUCTURALLY CANNOT DO. Each was found by a different mechanism in F1, and none of
them is a bug to be fixed later -- they are properties of comparing content across a diff.

  1. **It preserves pre-existing wrongness by the correct offset.** A shift map keeps a citation
     pointing at whatever it pointed at, right or wrong, and this reports 0 either way.
     `src/scenes/browser/listing.h` cited `menu_scene.h:525` for `take_action` while
     `take_action()` was declared at `:611`; it was already wrong before F1 task 9, task 9's sweep
     faithfully moved it to a new wrong line, and a content comparison approves of that. Detecting
     a citation that is *wrong* is `check_citations.py`'s symbol-extent predicate, a different
     tool. **This one detects only citations THIS COMMIT moved.**
  2. **It skips a citation whose basename is ambiguous.** `midi_learn.h` is both
     `src/scenes/services/` and `src/deluge/gui/menu_item/song/`; the resolver borrowed from
     `check_citations.py` picks one, and if it picks the other file the pair is compared against
     the wrong content -- so ambiguous basenames are reported as SKIPPED rather than judged. F1
     task 10's hand sweep silently dropped three files for exactly this collision. A citation the
     sweep never *looked* at is a different failure from one that drifted, and the count below
     separates them.
  3. **It can launder a claim the same commit falsified.** Updating the number is not checking the
     sentence. F1 task 10 put two arms above `MidiLearn`'s root gate, then swept
     `src/shell/scene.h`: the citation's number was bumped correctly and "gates its **entire**
     `handle()`" was left standing. That is *worse* than a stale citation, because an accurate
     line number reads as evidence the text was read. This tool reports 0 on that commit. The only
     mechanism that catches it has no automated form: list every citation into each file you
     changed and read the sentence each one sits in.
  4. **A citation the same commit authored is invisible by construction.** It was written against
     the post-edit tree, so its "old" number was never a number in `HEAD`, and the comparison
     succeeds on the wrong pair. Sweep first, author second.

A FIFTH PROPERTY, which is not a limitation so much as a thing to read the output with: it cannot
tell "the cited line moved out from under the citation" from "the commit rewrote the text ON that
line in place". Both print the same two-line before/after, and both are reasons to go and look;
only one is drift. The count below is therefore a count of citations to RE-READ, not of defects.

MEASURED BEFORE IT WAS KEPT, the way `check_citations.py`'s two widenings were (2026-09-07, plan
F1 task 12). Run `--per-commit` over F1's own history, `f9d00c951..32f4d4551`, **39 commits**:

  * it reports on **11** of the 39, naming **1124** moved endpoints out of **4921** compared, and
    skips **336** citations it refuses to judge -- **266** on 110 `SKIP` lines for an ambiguous
    basename and **70** on 19 for a pairing it could not make. Same units on both sides of that
    sentence, deliberately: the earlier phrasing counted citations on one side and `SKIP` *lines* on
    the other, which read as 336 being roughly 129. **The skip rate is 336 of 5257, or 6.4%** --
    not the 23% a first estimate put it at, which divided by the *moved* count as though that were
    the universe;
  * the two commits whose drift this plan actually had to go back and fix by hand are both in it,
    and on the smaller of the two the match is EXACT. F1 task 7's `280352058` inserted 13 lines
    into two doc comments; the tool names 5 endpoints, and those 5 are precisely the four
    citations that task's fix-round hand sweep found and the code review had not --
    `shell/shell.h:311` (`Shell::lowering`), `shell/scene.h:561` and
    `scenes/audio_clip/audio_clip_scene.h:474` (both `timer_repaints`, cited twice each, hence 5
    sites for 4 citations). Nothing else, no false positive on that commit;
  * F1 task 9's `a4b6fce59` gated eight entry points and the tool reports **966** endpoints; the
    very next commit is that task's 568-citation sweep, `f8c49128e`, on which the tool reports
    **0 moved over 478 compared**. So it both names the work and confirms the work was complete --
    which is what the sweep needed and did not have;
  * the OTHER gate is not a substitute, and the ratio is recorded in `check_citations.py`'s own
    sweeping section: at `a4b6fce59`, `--strict` went from 0 to **134** while **568** citations
    had moved. It saw about a quarter. On `280352058` it saw none of the 5, because a citation
    sliding inside a doc comment resolves perfectly.

**And what it does on its own task**, because a tool that is not run against the commits that ship
it has not been run: `--quiet 32f4d4551..7efd7bd79` reports **0 moved, 39 skipped, 2 compared**. Two
judged endpoints and thirty-nine refusals is the cleanest illustration of limitation 2 this tool will
ever produce -- almost every citation into a file that range touched names `deluge.cpp`, which is
`src/deluge/deluge.cpp` *and* `src/shell/profiles/deluge.cpp`, or `README.md`, which is twenty-six
files. Both citations that task actually broke were in the 39, and both were found by hand.

Kept on that: naming four citations a code review missed, and turning "which of ~4200 citations
did my commit move?" into a printed list, is worth a script nobody has to run in CI. Re-derive the
numbers with `--per-commit f9d00c951..32f4d4551`; they are of that exact range and will move as
the branch does.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_citations as cc

REPO = cc.REPO

# The citing files this tool reads. `check_citations.SCAN_ROOTS` plus the two layer READMEs and
# the design specs, and `.md` alongside the code suffixes.
#
# The gate deliberately leaves `.md` out (its own docstring says widening `SCAN_SUFFIXES` is a
# separate measured decision, because prose citations there cite across two functions or a file
# with no line at all, which its range predicates cannot judge). None of that applies here: this
# tool renders no verdict about a range's *contents*, only about whether the text under it moved,
# and the densest citation sites in the repo are exactly those files. Measured over the F1 range
# rather than assumed: including `.md` takes the report from 1014 endpoints to 1124 and the skips
# from 281 to 336, so roughly a tenth of everything this tool finds is in prose the gate does not
# read at all -- including both `.md` sites of F1 task 7's five (`ui-migration-design.md`).
CITING_ROOTS = (*cc.SCAN_ROOTS, "docs/superpowers/specs")
CITING_SUFFIXES = cc.SCAN_SUFFIXES | {".md"}

_BLOB_CACHE: dict[tuple[str, str], list[str] | None] = {}


def git(*args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(REPO), *args], capture_output=True, text=True, check=True
    ).stdout


def blob_lines(rev: str, rel: str) -> list[str] | None:
    """The file's lines at `rev`, or None if it did not exist there."""
    key = (rev, rel)
    if key not in _BLOB_CACHE:
        r = subprocess.run(
            ["git", "-C", str(REPO), "show", f"{rev}:{rel}"],
            capture_output=True,
            text=True,
            check=False,
        )
        _BLOB_CACHE[key] = r.stdout.splitlines() if r.returncode == 0 else None
    return _BLOB_CACHE[key]


def citations_in(
    text: str, index: dict[str, list[str]]
) -> list[tuple[str, int, int, int]]:
    """Every citation in `text`, as (resolved path, start, end, line number in the citing file).

    Bare `` `:NNN` `` continuations inherit the path from the previous citation ON THE SAME LINE,
    which is the same rule `check_citations.scan` applies and the same one the house convention
    means. A continuation with nothing before it on its line resolves to nothing and is dropped --
    that is the bare-continuation hole, recorded rather than guessed at.
    """
    out: list[tuple[str, int, int, int]] = []
    recent_dirs: list[str] = []
    for lineno, line in enumerate(text.splitlines(), 1):
        last: str | None = None
        for m in re.finditer(
            f"(?:{cc.CITATION_RE.pattern})|(?:{cc.CONTINUATION_RE.pattern})", line
        ):
            token, s1, e1, s2, e2 = m.groups()
            if token is not None:
                rel = cc.resolve(token, index, recent_dirs, int(s1))
                if rel is None:
                    continue
                last = rel
                recent_dirs.append(rel.rsplit("/", 1)[0])
                start, end = int(s1), int(e1 or s1)
            else:
                if last is None:
                    continue
                rel, start, end = last, int(s2), int(e2 or s2)
            out.append((rel, start, end, lineno))
    return out


def ambiguous_basenames(index: dict[str, list[str]]) -> set[str]:
    return {tok for tok, hits in index.items() if len(hits) > 1}


def report_range(old: str, new: str, strict: bool, quiet: bool) -> tuple[int, int, int]:
    """Compare every citation into a file the range touched. Returns (moved, skipped, pairs)."""
    index = cc.cited_index(cc.CITED_ROOT, *cc.PORT_CITED_ROOTS)
    ambiguous = ambiguous_basenames(index)
    touched = {
        p
        for p in git("diff", "--name-only", f"{old}..{new}").split()
        if Path(p).suffix in cc.CITED_SUFFIXES
    }
    if not touched:
        return 0, 0, 0

    citing = [
        p
        for p in git("ls-tree", "-r", "--name-only", new).splitlines()
        if Path(p).suffix in CITING_SUFFIXES
        and any(p == r or p.startswith(r + "/") for r in CITING_ROOTS)
    ]

    moved = skipped = pairs = 0
    for cf in citing:
        new_text = blob_lines(new, cf)
        old_text = blob_lines(old, cf)
        if new_text is None or old_text is None:
            continue  # added or deleted in the range: nothing to pair against
        new_cites = [
            c for c in citations_in("\n".join(new_text), index) if c[0] in touched
        ]
        old_cites = [
            c for c in citations_in("\n".join(old_text), index) if c[0] in touched
        ]
        by_new: dict[str, list] = {}
        by_old: dict[str, list] = {}
        for c in new_cites:
            by_new.setdefault(c[0], []).append(c)
        for c in old_cites:
            by_old.setdefault(c[0], []).append(c)
        for rel, ncs in by_new.items():
            ocs = by_old.get(rel, [])
            if Path(rel).name in ambiguous:
                skipped += len(ncs)
                if not quiet:
                    print(
                        f"  SKIP {cf}: {len(ncs)} citation(s) into {rel} -- "
                        f"'{Path(rel).name}' is an ambiguous basename, resolve by hand"
                    )
                continue
            if len(ncs) != len(ocs):
                skipped += len(ncs)
                if not quiet:
                    print(
                        f"  SKIP {cf}: {len(ocs)} -> {len(ncs)} citation(s) into {rel} -- "
                        f"cannot pair; this commit added or removed some, so read them"
                    )
                continue
            before, after = blob_lines(old, rel), blob_lines(new, rel)
            if before is None or after is None:
                continue
            for (_, ns, ne, nline), (_, os_, oe, _) in zip(ncs, ocs):
                # Both endpoints of a range, separately. `` `scene.h:500-552` `` swept by a
                # `` :\d+ `` regex becomes `510-552`, and comparing only the start agrees with it.
                endpoints = [("start", os_, ns)]
                if (oe, ne) != (os_, ns):
                    endpoints.append(("end", oe, ne))
                for label, o, n in endpoints:
                    ot = before[o - 1].strip() if 0 < o <= len(before) else "<past EOF>"
                    nt = after[n - 1].strip() if 0 < n <= len(after) else "<past EOF>"
                    pairs += 1
                    if ot != nt:
                        moved += 1
                        print(
                            f"  {cf}:{nline}: {rel}:{o} -> :{n} ({label}) no longer the same line"
                        )
                        print(f"      was  {ot[:100]}")
                        print(f"      now  {nt[:100]}")
    return moved, skipped, pairs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("range", nargs="?", default="HEAD^..HEAD")
    ap.add_argument("--strict", action="store_true", help="exit 1 on any difference")
    ap.add_argument(
        "--per-commit",
        action="store_true",
        help="report each commit in the range separately",
    )
    ap.add_argument(
        "--quiet", action="store_true", help="differences only, no SKIP lines"
    )
    a = ap.parse_args()

    if ".." in a.range:
        old, new = a.range.split("..", 1)
    else:
        old, new = a.range + "^", a.range

    if a.per_commit:
        revs = git("rev-list", "--reverse", f"{old}..{new}").split()
        total_moved = total_skipped = total_compared = flagged = 0
        for rev in revs:
            m, s, p = report_range(rev + "^", rev, a.strict, a.quiet)
            subject = git("log", "-1", "--format=%h %s", rev).strip()
            if m or (s and not a.quiet):
                print(f"{subject}: {m} moved, {s} unpaired/ambiguous, {p} compared")
            if m:
                flagged += 1
            total_moved += m
            total_skipped += s
            total_compared += p
        # `compared` is summed over EVERY commit, including the ones whose per-commit line was not
        # printed -- a commit with nothing to report still compared endpoints. Reconstructing this
        # by adding up the printed lines undercounts, and by a different amount with `--quiet` than
        # without it (3582 and 2416 against a true 4921, on this tool's own first measurement
        # range), which is why the total is printed here rather than left to be summed by hand.
        universe = total_compared + total_skipped
        rate = (100.0 * total_skipped / universe) if universe else 0.0
        print(
            f"check_citation_shift: {len(revs)} commit(s), {flagged} with citations whose cited "
            f"line moved, {total_moved} moved of {total_compared} endpoint(s) compared, "
            f"{total_skipped} citation(s) skipped ({rate:.1f}% of {universe})"
        )
        return 1 if (a.strict and total_moved) else 0

    moved, skipped, pairs = report_range(old, new, a.strict, a.quiet)
    print(
        f"check_citation_shift: {moved} citation endpoint(s) whose cited line moved, "
        f"{skipped} unpaired/ambiguous, {pairs} compared over {old}..{new}"
    )
    return 1 if (a.strict and moved) else 0


if __name__ == "__main__":
    sys.exit(main())
