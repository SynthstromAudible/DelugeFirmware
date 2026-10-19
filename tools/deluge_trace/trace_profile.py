#!/usr/bin/env python3
"""A function-level profile of a decoded PTM trace: self and inclusive cycles per function, and the
breakdown under any function matching --focus.

What it can say. Each OpenCSD INSTR_RANGE carries the cycles since the previous waypoint and how the
range ended: a call (`BR b+link`, `iBR b+link`), a return (`iBR ... ret`) or a plain branch. A
shadow call stack built from those gives each range a stack: its cycles count as the innermost
function's self time and, once, towards the inclusive time of every function on the stack down to
the nearest barrier. An exception (an interrupt) is a barrier, named by its number, which unwinds
when execution resumes at its preferred return address, so a handler's cycles are never charged to
what it interrupted. A
loss of sync (TRACE_ON, NO_SYNC; dense loops overflow the trace port, so it is frequent) keeps the
stack: the first range after it unwinds to the innermost frame of its own function, so a
long-running frame survives the gaps; if no frame is its function, it starts behind a barrier,
`[after a gap]`, since its caller is unknown. The cycles lost in a gap are not counted:
times cover what the trace saw.

Limits: a jump into another function that is neither a call nor a return (a tail call) replaces the
innermost frame; code outside every symbol is grouped as `?`. Times are cycles at 400 MHz.

Usage:
    trace_profile.py RUN_DIR --elf ELF [--focus REGEX] [--top 40]

Output: profile.md in RUN_DIR, and the Markdown on stdout.
"""

from __future__ import annotations

import argparse
import bisect
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import symbols

CPU_HZ = 400e6

RANGE_RE = re.compile(
    r"INSTR_RANGE\(exec range=0x([0-9a-fA-F]+):\[0x[0-9a-fA-F]+\] num_i\(\d+\) last_sz\(\d+\) "
    r"\(ISA=\w+\) ([EN]) (\S+)([^\[]*)\[CC=(\d+)\]"
)
EXCEPTION_RE = re.compile(
    r"EXCEPTION\(pref ret addr:0x([0-9a-fA-F]+); excep num \(0x([0-9a-fA-F]+)\)\s*(?:\[CC=(\d+)\])?"
)
UNSYNC_RE = re.compile(r"ELEM_(TRACE_ON|NO_SYNC)")


def short_name(full: str) -> str:
    """`full` (a demangled name) without its trailing parameter list and qualifiers, keeping any
    `(anonymous namespace)` or `decltype(...)` inside it: `ns::(anonymous namespace)::f(int) const`
    is `ns::(anonymous namespace)::f`."""
    text = full.rstrip()
    for suffix in (" const", " volatile", " &&", " &"):
        if text.endswith(suffix):
            text = text[: -len(suffix)].rstrip()
    if not text.endswith(")"):
        return text
    depth = 0
    for i in range(len(text) - 1, -1, -1):
        if text[i] == ")":
            depth += 1
        elif text[i] == "(":
            depth -= 1
            if depth == 0:
                return text[:i] if i > 0 else text
    return text


class Names:
    """Address to function name, by bisection over the symbol table, cached per address."""

    def __init__(self, syms: list[symbols.Symbol]) -> None:
        self.syms = sorted(syms, key=lambda s: s.start)
        self.starts = [s.start for s in self.syms]
        self.cache: dict[int, str] = {}

    def __call__(self, address: int) -> str:
        name = self.cache.get(address)
        if name is None:
            i = bisect.bisect_right(self.starts, address) - 1
            s = self.syms[i] if i >= 0 else None
            name = short_name(s.name) if s and address < s.end else "?"
            self.cache[address] = name
        return name


@dataclass
class Frame:
    name: str
    # The segment the frame belongs to, and that segment's running total when it was pushed: its
    # inclusive cycles are the segment's total at its pop less this.
    segment: int
    since: int
    # A barrier (an exception, or code found after a gap that is on no frame) opens a segment of its
    # own; an exception's barrier also records where the interrupted code resumes.
    barrier: bool = False
    resume: int | None = None


@dataclass
class Segment:
    total: int = 0
    on_stack: Counter = field(default_factory=Counter)


@dataclass
class Profile:
    total: int = 0
    self_cycles: Counter = field(default_factory=Counter)
    inclusive: Counter = field(default_factory=Counter)
    unsynced: int = 0
    focus_total: int = 0
    focus_preempted: int = 0
    focus_children: Counter = field(default_factory=Counter)
    focus_self: Counter = field(default_factory=Counter)


RESYNC = "[after a gap]"


def profile(lines, name_of, focus: re.Pattern | None = None) -> Profile:
    """Walks decoded-trace `lines`, naming addresses with `name_of`.

    Time inside a barrier (an exception, or code found after a gap on no frame of the stack) counts
    for the barrier and its own frames, never for the frames beneath: an interrupt does not inflate
    what it interrupted, and code whose caller the trace lost is not charged to an unrelated frame.
    """
    p = Profile()
    stack: list[Frame] = []
    segments: list[Segment] = [Segment()]

    def push(name: str, barrier: bool = False, resume: int | None = None) -> None:
        seg = len(segments) - 1
        stack.append(Frame(name, seg, segments[seg].total, barrier, resume))
        segments[seg].on_stack[name] += 1
        if barrier:
            segments.append(Segment())

    def pop() -> None:
        f = stack.pop()
        if f.barrier:
            inner = segments.pop()
            p.inclusive[f.name] += inner.total
        seg = segments[f.segment]
        seg.on_stack[f.name] -= 1
        # Recursion: only the outermost frame's span counts, so a cycle is counted once.
        if seg.on_stack[f.name] == 0 and not f.barrier:
            p.inclusive[f.name] += seg.total - f.since

    def unwind_to(depth: int) -> None:
        while len(stack) > depth:
            pop()

    def find(name: str) -> int | None:
        for i in range(len(stack) - 1, -1, -1):
            if stack[i].name == name:
                return i
        return None

    pending = None  # how the previous range ended: "call", "ret" or None
    resynced = False
    for line in lines:
        if "INSTR_RANGE" in line:
            m = RANGE_RE.search(line)
            if not m:
                continue
            address = int(m.group(1), 16)
            executed = m.group(2) == "E"
            kind, notes, cycles = m.group(3), m.group(4), int(m.group(5))
            name = name_of(address)
            # Resuming after an exception unwinds to and past its frame.
            for i in range(len(stack) - 1, -1, -1):
                if stack[i].resume == address:
                    unwind_to(i)
                    break
            if resynced:
                resynced = False
                i = find(name)
                if i is not None:
                    unwind_to(i + 1)
                else:
                    # A run of gaps into unknown code replaces its barrier rather than piling up.
                    if stack and stack[-1].name == RESYNC:
                        unwind_to(len(stack) - 1)
                    elif len(stack) >= 2 and stack[-2].name == RESYNC:
                        unwind_to(len(stack) - 2)
                    push(RESYNC, barrier=True)
                    push(name)
            elif pending == "call" or not stack:
                push(name)
            elif stack[-1].name != name:
                if pending == "ret" and not stack[-1].barrier:
                    pop()
                if not stack or stack[-1].name != name:
                    # A tail call, or a return the trace did not see: the frame becomes this one.
                    if stack and not stack[-1].barrier:
                        pop()
                    push(name)
            p.total += cycles
            segments[-1].total += cycles
            p.self_cycles[name] += cycles
            if focus is not None:
                for i, f in enumerate(stack):
                    if focus.search(f.name):
                        p.focus_total += cycles
                        rest = stack[i + 1 :]
                        if any(g.barrier for g in rest):
                            p.focus_preempted += cycles
                        else:
                            p.focus_children[rest[0].name if rest else "(self)"] += (
                                cycles
                            )
                            p.focus_self[name] += cycles
                        break
            pending = None
            if executed and "b+link" in notes:
                pending = "call"
            elif executed and kind == "iBR" and "ret" in notes:
                pending = "ret"
        elif "EXCEPTION" in line:
            m = EXCEPTION_RE.search(line)
            if m:
                push(
                    f"[exception 0x{int(m.group(2), 16):02x}]",
                    barrier=True,
                    resume=int(m.group(1), 16),
                )
                pending = "call"
        elif UNSYNC_RE.search(line):
            p.unsynced += 1
            resynced = True
            pending = None
    unwind_to(0)
    return p


def ms(cycles: int) -> float:
    return cycles / CPU_HZ * 1000


def render(p: Profile, focus: str | None, top: int) -> str:
    out = [
        f"Traced {ms(p.total):.1f} ms ({p.total} cycles at 400 MHz); the trace lost sync {p.unsynced} times.",
        "",
    ]
    lost = p.inclusive[RESYNC] / p.total if p.total else 0.0
    if lost > 0.2:
        out += [
            (
                f"**{100 * lost:.0f}% of the traced time ran after a gap with its caller unknown.** Self "
                "times are exact for what the trace saw, but inclusive times and the --focus breakdown "
                "undercount: the trace overflowed too often for call stacks to survive (dense loops, "
                "typically audio). Narrow the traced ranges to the code in question."
            ),
            "",
        ]
    out += [
        "| inclusive ms | % | self ms | function |",
        "|---:|---:|---:|---|",
    ]
    for name, c in p.inclusive.most_common(top):
        out.append(
            f"| {ms(c):.2f} | {100 * c / p.total:.1f} | {ms(p.self_cycles[name]):.2f} | `{name}` |"
        )
    out += ["", "| self ms | % | function |", "|---:|---:|---|"]
    for name, c in p.self_cycles.most_common(top):
        out.append(f"| {ms(c):.2f} | {100 * c / p.total:.1f} | `{name}` |")
    if focus is not None:
        out += [
            "",
            (
                f"Under the outermost frame matching `{focus}`: {ms(p.focus_total):.2f} ms, of which "
                f"{ms(p.focus_preempted):.2f} ms preempted (an interrupt, or code after a gap)."
            ),
            "",
        ]
        out += ["| callee ms | % | direct callee |", "|---:|---:|---|"]
        for name, c in p.focus_children.most_common(top):
            out.append(
                f"| {ms(c):.2f} | {100 * c / max(p.focus_total, 1):.1f} | `{name}` |"
            )
        out += ["", "| self ms | % | function, anywhere under it |", "|---:|---:|---|"]
        for name, c in p.focus_self.most_common(top):
            out.append(
                f"| {ms(c):.2f} | {100 * c / max(p.focus_total, 1):.1f} | `{name}` |"
            )
    return "\n".join(out) + "\n"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Function-level profile of a decoded trace."
    )
    ap.add_argument("run", type=Path, help="a run directory holding decode.txt")
    ap.add_argument("--elf", required=True)
    ap.add_argument(
        "--focus", help="break down the time under functions matching this regex"
    )
    ap.add_argument("--top", type=int, default=40)
    args = ap.parse_args(argv)
    name_of = Names(symbols.read_symbols(args.elf))
    focus = re.compile(args.focus) if args.focus else None
    with open(args.run / "decode.txt", errors="replace") as f:
        p = profile(f, name_of, focus)
    md = render(p, args.focus, args.top)
    (args.run / "profile.md").write_text(md)
    print(md, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
