#!/usr/bin/env python3
"""Map a decoded PTM trace back onto the disassembly: cycles per block, per loop iteration and per
instruction.

What the trace can and cannot say. The PTM reports waypoints (branches), not instructions; with cycle
counting on, each waypoint carries the cycles since the previous one. So the finest unit timed is the
block of instructions between two waypoints, as OpenCSD's INSTR_RANGE elements give them. Within a
block the report gives each instruction its execution count and the block's mean cycles split evenly
over its instructions ("even share"); that split is arithmetic, not measurement. A loop whose body is
one block (no branch inside) is timed per iteration exactly, and the spread of those times (a long
tail, or two modes) is what shows a stall that comes and goes.

Input: a capture directory with decode.txt (trc_pkt_lister -decode output) and meta.json (its ranges),
and the ELF, disassembled with llvm-objdump (or the toolchain's arm-none-eabi-objdump).
Output: report.md and report.json in the capture directory, and the Markdown on stdout.
"""

from __future__ import annotations

import argparse
import itertools
import json
import os
import re
import statistics
import subprocess
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import symbols

REPO = Path(__file__).resolve().parents[2]
OBJDUMP_CANDIDATES = (
    "llvm-objdump",
    str(
        Path(os.environ.get("DBT_TOOLCHAIN_PATH") or REPO)
        / "toolchain/current/arm-none-eabi-gcc/bin/arm-none-eabi-objdump"
    ),
)

RANGE_RE = re.compile(
    r"OCSD_GEN_TRC_ELEM_INSTR_RANGE\(exec range=0x([0-9a-fA-F]+):\[0x([0-9a-fA-F]+)\] "
    r"num_i\((\d+)\) last_sz\((\d+)\) \(ISA=([^)]+)\) ([EN])\b(.*)\)"
)
CC_RE = re.compile(r"\[CC=(\d+)\]")
RET_RE = re.compile(r"\bret\b")
FILTER_REASON = (
    "begin or filter"  # the TRACE_ON reason for the filter re-entering traced code
)
TRACE_ON_RE = re.compile(r"OCSD_GEN_TRC_ELEM_TRACE_ON\( \[([^\]]*)\]")
BREAK_RE = re.compile(r"OCSD_GEN_TRC_ELEM_(NO_SYNC|EO_TRACE|ADDR_NACC|ADDR_UNKNOWN)")
INSN_RE = re.compile(r"^\s*([0-9a-fA-F]+):\s+(.*\S)\s*$")


@dataclass(frozen=True)
class Range:
    """One executed block: instructions [start, end), ending in a waypoint taken (E) or not (N)."""

    start: int
    end: int
    count: int
    last_size: int
    taken: bool
    cycles: int | None
    returns: bool = False  # its waypoint is a return, taken

    @property
    def last(self) -> int:
        return self.end - self.last_size


@dataclass(frozen=True)
class TraceOn:
    """Trace resuming: after the filter left the traced code (a call out, an exception, a return to an
    untraced caller) or at the capture's start. `cycles` is the count the element carries, the time the
    trace was off."""

    reason: str
    cycles: int | None


@dataclass
class Decoded:
    """The trace as a sequence of ranges, broken where trace resumed (TraceOn entries) or where the
    decoder lost track (None entries)."""

    items: list[Range | TraceOn | None] = field(default_factory=list)
    trace_on: Counter = field(default_factory=Counter)


def parse_decode(text: str) -> Decoded:
    """The INSTR_RANGE elements and the breaks between them, from trc_pkt_lister output."""
    out = Decoded()
    for line in text.splitlines():
        if "OCSD_GEN_TRC_ELEM_" not in line:
            continue
        if m := RANGE_RE.search(line):
            cc = CC_RE.search(m.group(7))
            out.items.append(
                Range(
                    start=int(m.group(1), 16),
                    end=int(m.group(2), 16),
                    count=int(m.group(3)),
                    last_size=int(m.group(4)),
                    taken=m.group(6) == "E",
                    cycles=int(cc.group(1)) if cc else None,
                    returns=m.group(6) == "E" and RET_RE.search(m.group(7)) is not None,
                )
            )
        elif m := TRACE_ON_RE.search(line):
            out.trace_on[m.group(1)] += 1
            cc = CC_RE.search(line)
            out.items.append(TraceOn(m.group(1), int(cc.group(1)) if cc else None))
        elif BREAK_RE.search(line):
            out.items.append(None)
    return out


def parse_objdump(text: str) -> dict[int, str]:
    """Instruction address to its disassembly text."""
    out = {}
    for line in text.splitlines():
        if m := INSN_RE.match(line):
            out[int(m.group(1), 16)] = m.group(2)
    return out


def disassemble(elf: Path, start: int, end: int) -> dict[int, str]:
    for tool in OBJDUMP_CANDIDATES:
        try:
            proc = subprocess.run(
                [
                    tool,
                    "-d",
                    "-C",
                    f"--start-address=0x{start:x}",
                    f"--stop-address=0x{end:x}",
                    str(elf),
                ],
                capture_output=True,
                text=True,
                check=True,
            )
        except (OSError, subprocess.CalledProcessError):
            continue
        return parse_objdump(proc.stdout)
    raise RuntimeError("no objdump could disassemble the ELF")


def summarize(values: list[int]) -> dict[str, float | int]:
    s = sorted(values)
    return {
        "n": len(s),
        "mean": round(statistics.fmean(s), 2),
        "min": s[0],
        "p50": s[len(s) // 2],
        "p90": s[min(len(s) - 1, (len(s) * 9) // 10)],
        "max": s[-1],
    }


def histogram(values: list[int], top: int = 6) -> str:
    total = len(values)
    return ", ".join(
        f"{v}:{100 * n / total:.0f}%" for v, n in Counter(values).most_common(top)
    )


def segments(dec: Decoded) -> list[list[Range]]:
    """Runs of ranges with no break between them."""
    runs: list[list[Range]] = [[]]
    for item in dec.items:
        if isinstance(item, Range):
            runs[-1].append(item)
        elif runs[-1]:
            runs.append([])
    return [r for r in runs if r]


def loop_iterations(dec: Decoded, lo: int, hi: int) -> dict[int, list[int]]:
    """Cycles per complete iteration of each loop inside [lo, hi), keyed by the loop's head.

    A loop head is the target of a taken backward branch. An iteration runs from a range that starts
    at the head to the next arrival there, or to the loop's closing branch falling through (the last
    iteration). Iterations that a break interrupts, or with a range missing its cycle count, are left
    out.
    """
    heads: set[int] = set()
    closers: dict[int, int] = {}
    runs = segments(dec)
    for run in runs:
        for a, b in itertools.pairwise(run):
            if (
                a.taken
                and lo <= b.start < hi
                and b.start <= a.last
                and lo <= a.last < hi
            ):
                heads.add(b.start)
                closers[b.start] = a.last
    result: dict[int, list[int]] = defaultdict(list)
    for run in runs:
        current: int | None = None
        total = 0
        ok = True
        for r in run:
            if r.start in heads:
                current, total, ok = r.start, 0, True
            if current is None:
                continue
            if r.cycles is None:
                ok = False
            else:
                total += r.cycles
            if r.last == closers[current]:
                if ok:
                    result[current].append(total)
                current = None
    return dict(result)


def calls(dec: Decoded, lo: int, hi: int) -> list[dict[str, int]]:
    """Cycles per complete call of the function [@p lo, @p hi), with what it calls when that is traced
    too.

    A call runs from a range starting at @p lo to the function's own return (a taken return whose range
    lies in [lo, hi)); what runs after that until the next entry is not its, even where it is traced
    code reached from elsewhere. `own` sums the call's ranges' cycles; `outside` sums the trace-off time
    between them (calls into untraced code, exceptions), from the TraceOn elements; `calls_out` counts
    those gaps. A call the trace lost data in (a None, or trace resuming for any reason but the filter,
    such as an overflow) or missing a cycle count is dropped, as is one the capture cuts short.
    """
    out: list[dict[str, int]] = []
    current: dict[str, int] | None = None
    for item in dec.items:
        if item is None or (isinstance(item, TraceOn) and item.reason != FILTER_REASON):
            current = None
        elif isinstance(item, TraceOn):
            if current is not None:
                if item.cycles is None:
                    current = None
                else:
                    current["outside"] += item.cycles
                    current["calls_out"] += 1
        else:
            if item.start == lo:
                current = {"own": 0, "outside": 0, "calls_out": 0}
            if current is None:
                continue
            if item.cycles is None:
                current = None
                continue
            current["own"] += item.cycles
            if item.returns and lo <= item.last < hi:
                current["total"] = current["own"] + current["outside"]
                out.append(current)
                current = None
    return out


def calls_section(name: str, result: list[dict[str, int]]) -> str:
    """The calls table: every call, and the calls split by how many times they left the traced code."""
    out = [
        f"### Calls of {name}",
        "",
        "A call runs from an entry to the function's return; `own` is the traced code's cycles (the first",
        "function and any other traced one it calls), `outside` the time in untraced callees and",
        "exceptions.",
        "",
        "| calls | calls out | n | own mean | own p50 | own p90 | total mean | total p50 | total p90 |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    groups: dict[str, list[dict[str, int]]] = {"all": result}
    for k in sorted({c["calls_out"] for c in result}):
        groups[str(k)] = [c for c in result if c["calls_out"] == k]
    for label, group in groups.items():
        if not group:
            continue
        own = summarize([c["own"] for c in group])
        total = summarize([c["total"] for c in group])
        out.append(
            f"| {'all' if label == 'all' else 'by gaps'} | {'-' if label == 'all' else label} | "
            f"{own['n']} | {own['mean']} | {own['p50']} | {own['p90']} | {total['mean']} | "
            f"{total['p50']} | {total['p90']} |"
        )
    out.append("")
    return "\n".join(out)


def analyse(dec: Decoded, insns: dict[int, str], lo: int, hi: int) -> dict:
    """Blocks, loops and per-instruction counts for the function [lo, hi)."""
    addresses = sorted(a for a in insns if lo <= a < hi)
    blocks: dict[tuple[int, int], list[int]] = defaultdict(list)
    block_execs: Counter = Counter()
    insn_execs: Counter = Counter()
    for item in dec.items:
        if not isinstance(item, Range) or not (lo <= item.start < hi):
            continue
        key = (item.start, item.end)
        block_execs[key] += 1
        if item.cycles is not None:
            blocks[key].append(item.cycles)
        for a in addresses:
            if item.start <= a < item.end:
                insn_execs[a] += 1
    block_rows = []
    for key, execs in sorted(block_execs.items()):
        n = sum(1 for a in addresses if key[0] <= a < key[1])
        cyc = blocks.get(key, [])
        row = {"start": key[0], "end": key[1], "instructions": n, "executions": execs}
        if cyc:
            row |= {"cycles": summarize(cyc), "histogram": histogram(cyc)}
            row["even_share"] = round(row["cycles"]["mean"] / n, 2) if n else None
        block_rows.append(row)
    loops = [
        {"head": head, "cycles": summarize(v), "histogram": histogram(v)}
        for head, v in sorted(loop_iterations(dec, lo, hi).items())
        if v
    ]
    # Several blocks can cover one instruction (a loop's first iteration runs on from its preheader) or
    # end at one waypoint: weight each block's figures by how often it ran with a cycle count.
    share_sum: dict[int, float] = defaultdict(float)
    share_n: Counter = Counter()
    wp_sum: dict[int, float] = defaultdict(float)
    wp_n: Counter = Counter()
    for row in block_rows:
        if "cycles" not in row:
            continue
        covered = [a for a in addresses if row["start"] <= a < row["end"]]
        if not covered:
            continue
        timed = row["cycles"]["n"]
        for a in covered:
            share_sum[a] += row["even_share"] * timed
            share_n[a] += timed
        wp_sum[covered[-1]] += row["cycles"]["mean"] * timed
        wp_n[covered[-1]] += timed
    share = {a: share_sum[a] / share_n[a] for a in share_n}
    waypoint = {a: wp_sum[a] / wp_n[a] for a in wp_n}
    lines = [
        {
            "address": a,
            "executions": insn_execs.get(a, 0),
            "even_share": share.get(a),
            "waypoint": waypoint.get(a),
            "text": insns[a],
        }
        for a in addresses
    ]
    return {"blocks": block_rows, "loops": loops, "instructions": lines}


def render(name: str, lo: int, hi: int, result: dict, dec: Decoded) -> str:
    out = [f"## {name}", "", f"`0x{lo:08X}`-`0x{hi:08X}`", ""]
    reasons = ", ".join(f"{k}: {v}" for k, v in dec.trace_on.most_common()) or "none"
    out += [
        f"Trace-on events in the whole capture (each a gap in the trace): {reasons}.",
        "",
    ]
    if result["loops"]:
        out += [
            "### Cycles per loop iteration",
            "",
            "| loop head | iterations | mean | min | p50 | p90 | max | most common (cycles:share) |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
        ]
        for loop in result["loops"]:
            c = loop["cycles"]
            out.append(
                f"| `0x{loop['head']:08X}` | {c['n']} | {c['mean']} | {c['min']} | {c['p50']} | "
                f"{c['p90']} | {c['max']} | {loop['histogram']} |"
            )
        out.append("")
    out += [
        "### Blocks (waypoint to waypoint)",
        "",
        "| block | instrs | executions | mean cycles | min | p50 | p90 | max | even share | most common |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for b in result["blocks"]:
        c = b.get("cycles")
        cells = (
            [str(c["mean"]), str(c["min"]), str(c["p50"]), str(c["p90"]), str(c["max"])]
            if c
            else ["-"] * 5
        )
        out.append(
            f"| `0x{b['start']:08X}`-`0x{b['end']:08X}` | {b['instructions']} | {b['executions']} | "
            + " | ".join(cells)
            + f" | {b.get('even_share', '-')} | {b.get('histogram', '')} |"
        )
    out += [
        "",
        "### Instructions",
        "",
        "Executions per instruction; `block cycles` is the mean of the blocks ending at a waypoint (the",
        "branch that ends them), shown there; `even share` splits each block's mean evenly over its",
        "instructions. Where several blocks cover an instruction, both are weighted by executions.",
        "",
        "```",
        f"{'address':>10} {'execs':>8} {'even share':>10} {'block cycles':>12}  instruction",
    ]
    for line in result["instructions"]:
        share = "" if line["even_share"] is None else f"{line['even_share']:.2f}"
        wp = "" if line["waypoint"] is None else f"{line['waypoint']:.2f}"
        out.append(
            f"{line['address']:>10x} {line['executions']:>8} {share:>10} {wp:>12}  {line['text']}"
        )
    out += ["```", ""]
    return "\n".join(out)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Report cycles per block, loop and instruction."
    )
    ap.add_argument("capture", help="the capture directory (decode.txt, meta.json)")
    ap.add_argument("--elf", required=True, help="the unstripped ELF that ran")
    args = ap.parse_args(argv)

    capture = Path(args.capture)
    meta = json.loads((capture / "meta.json").read_text())
    dec = parse_decode((capture / "decode.txt").read_text(errors="replace"))
    try:
        syms = symbols.read_symbols(args.elf)
    except RuntimeError:
        syms = []
    doc = [f"# PTM trace report: {capture.name}", ""]
    data = []
    call_rows: list[dict[str, int]] = []
    for lo_text, hi_text in meta["ranges"]:
        lo, hi = int(lo_text, 16), int(hi_text, 16)
        sym = symbols.at(syms, lo)
        name = sym.name if sym else f"0x{lo:08X}"
        insns = disassemble(Path(args.elf), lo, hi)
        result = analyse(dec, insns, lo, hi)
        doc.append(render(name, lo, hi, result, dec))
        if not data:
            # The first traced function is the root: its calls include the other traced functions
            call_rows = calls(dec, lo, hi)
            doc.append(calls_section(name, call_rows))
        data.append({"name": name, "start": lo, "end": hi, **result})
    text = "\n".join(doc)
    (capture / "report.md").write_text(text)
    (capture / "report.json").write_text(
        json.dumps(
            {"trace_on": dict(dec.trace_on), "ranges": data, "calls": call_rows},
            indent=2,
        )
        + "\n"
    )
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
