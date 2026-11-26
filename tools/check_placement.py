#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Checks a linked device image's code placement against what must run from on-chip SRAM.

Rules, with roots from tools/placement_roots.toml:
- pre_inflate: no function the boot reaches before it has inflated .sdram_init lies in SDRAM, which holds no code
  until then. On an image that shares the pack's bytes with zero-initialised statics ([__late_bss_start,
  __late_bss_end)), none of those functions may load an address in that range either.
- hot: no function the audio tier, the streaming tier or an interrupt reaches lies in SDRAM, where every fetch
  competes with sample streaming for the one 16-bit bus.
- vtables: a vtable in SRAM with an entry in SDRAM's code is a finding unless its class is allowed. The walk follows
  direct branches and lld's long-branch thunks only, so a virtual call from a hot root is invisible to it; this is
  where such a call shows.

With --report-hot it also prints the size of the code the hot roots reach, by namespace.

Findings listed under `[[known]]` (each with its reason) are printed but do not fail the gate; the list only
shrinks: an entry that matches no finding fails it, so a fixed finding's entry is deleted.

Exit status: 0 clean, 1 findings or stale known entries, 2 a configuration error (a root or `until` that matches nothing, a missing tool).
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from collections import Counter, deque
from dataclasses import dataclass, field
from pathlib import Path

import tomllib

SDRAM = (0x0C000000, 0x10000000)
THUNK = re.compile(r"^__(?:Thumbv7|ARMv7|Thumbv7PI|ARMV7PI)\w*?LongThunk_(.+)$")
HEADER = re.compile(r"^([0-9a-f]{8}) <(.+)>:$")
ADDRESS = re.compile(r"^\s*([0-9a-f]+):")
BRANCH = re.compile(
    r"^\s*[0-9a-f]+:\s+(?:bl|blx|b|b\.w|b(?:eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)(?:\.w)?)"
    r"\s+0x[0-9a-f]+ <([^>]+)>"
)
WORD = re.compile(r"^\s*[0-9a-f]+:\s+\.word\s+0x([0-9a-f]+)")
MOVW = re.compile(r"^\s*[0-9a-f]+:\s+movw\s+(\w+), #0x([0-9a-f]+)")
MOVT = re.compile(r"^\s*[0-9a-f]+:\s+movt\s+(\w+), #0x([0-9a-f]+)")
DISAMBIGUATOR = re.compile(r"\[[0-9a-f]+\]")


class ConfigError(Exception):
    """A root pattern or `until` matches nothing, or the configuration is malformed."""


@dataclass
class Function:
    name: str
    address: int
    calls_at: list[tuple[int, str]] = field(default_factory=list)
    constants_at: list[tuple[int, int]] = field(default_factory=list)
    insns_at: list[tuple[int, str]] = field(default_factory=list)

    @property
    def calls(self) -> set[str]:
        return {t for _, t in self.calls_at}

    @property
    def constants(self) -> set[int]:
        return {v for _, v in self.constants_at}


@dataclass
class Violation:
    rule: str
    function: str
    chain: list[str]


def parse_functions(text: str) -> dict[str, Function]:
    """Every function in @p text (llvm-objdump -d output), with its direct branch targets and its constants, in order."""
    fns: dict[str, Function] = {}
    cur: Function | None = None
    low: dict[str, int] = {}
    for line in text.splitlines():
        if m := HEADER.match(line):
            cur = fns.setdefault(m.group(2), Function(m.group(2), int(m.group(1), 16)))
            low = {}
            continue
        if cur is None or not (a := ADDRESS.match(line)):
            continue
        at = int(a.group(1), 16)
        cur.insns_at.append((at, line[a.end() :].strip()))
        if m := BRANCH.match(line):
            target = m.group(1)
            if "+0x" not in target and target != cur.name:
                cur.calls_at.append((at, target))
        elif m := WORD.match(line):
            cur.constants_at.append((at, int(m.group(1), 16)))
        elif m := MOVW.match(line):
            low[m.group(1)] = int(m.group(2), 16)
        elif m := MOVT.match(line):
            reg = m.group(1)
            if reg in low:
                cur.constants_at.append(
                    (at, (int(m.group(2), 16) << 16) | low.pop(reg))
                )
    return fns


def call_graph(fns: dict[str, Function]) -> dict[str, set[str]]:
    """Each function's callees; a long-branch thunk's callee is the function it is named after."""
    graph = {name: set(f.calls) for name, f in fns.items()}
    for name in fns:
        if m := THUNK.match(name):
            graph[name] = {m.group(1)} if m.group(1) in fns else set()
    return graph


def reach(graph: dict[str, set[str]], roots: list[str]) -> dict[str, str | None]:
    """Breadth-first: every function reachable from @p roots, mapped to the caller it was first reached from."""
    parents: dict[str, str | None] = {r: None for r in roots}
    queue = deque(roots)
    while queue:
        node = queue.popleft()
        for callee in sorted(graph.get(node, ())):
            if callee not in parents:
                parents[callee] = node
                queue.append(callee)
    return parents


def path(parents: dict[str, str | None], node: str) -> list[str]:
    chain = [node]
    while parents[chain[-1]] is not None:
        chain.append(parents[chain[-1]])
    return chain[::-1]


def sdram_reached(fns, graph, roots, lo, hi, rule="hot") -> list[Violation]:
    parents = reach(graph, roots)
    return [
        Violation(rule, name, path(parents, name))
        for name in sorted(parents)
        if name in fns and lo <= fns[name].address < hi and not THUNK.match(name)
    ]


def prefix(
    fn: Function,
    demangled: dict[str, str],
    until: str | None = None,
    until_insn: str | None = None,
) -> tuple[set[str], set[int]]:
    """@p fn's callees and constants before a cut: its first call to a function whose demangled name matches
    @p until, or its first instruction matching @p until_insn (for a step the optimiser inlined, such as the inflate's
    final cache maintenance)."""
    if (until is None) == (until_insn is None):
        raise ConfigError(
            f"check_placement: {fn.name}'s prefix needs exactly one of until and until_insn"
        )
    if until is not None:
        rx = re.compile(until)
        cut = next(
            (
                at
                for at, t in fn.calls_at
                if rx.search(DISAMBIGUATOR.sub("", demangled.get(t, t)))
            ),
            None,
        )
    else:
        rx = re.compile(until_insn)
        cut = next((at for at, insn in fn.insns_at if rx.search(insn)), None)
    if cut is None:
        raise ConfigError(
            f"check_placement: {fn.name} has no call or instruction matching {until or until_insn!r}"
        )
    return (
        {t for at, t in fn.calls_at if at < cut},
        {v for at, v in fn.constants_at if at < cut},
    )


def late_loads(fn: Function, lo: int, hi: int) -> list[int]:
    return sorted(c for c in fn.constants if lo <= c < hi)


def vtable_hits(
    words_by_vtable: dict[str, list[int]], lo: int, hi: int
) -> dict[str, list[int]]:
    hits = {}
    for cls, words in words_by_vtable.items():
        inside = [w for w in words if lo <= (w & ~1) < hi]
        if inside:
            hits[cls] = inside
    return hits


def apply_known(
    findings: list[Violation], known: list[dict], demangled: dict[str, str]
) -> tuple[list[Violation], list[Violation], list[dict]]:
    """Splits @p findings into those no `[[known]]` entry covers and those one does, and returns the entries that cover
    nothing (stale: the finding was fixed, so the entry must go)."""
    used = [False] * len(known)
    remaining, covered = [], []
    for v in findings:
        name = DISAMBIGUATOR.sub("", demangled.get(v.function, v.function))
        hit = next(
            (
                i
                for i, k in enumerate(known)
                if k["rule"] == v.rule and re.search(k["function"], name)
            ),
            None,
        )
        if hit is None:
            remaining.append(v)
        else:
            used[hit] = True
            covered.append(v)
    return remaining, covered, [k for k, u in zip(known, used) if not u]


def match_roots(patterns: list[str], demangled: dict[str, str]) -> list[str]:
    """The mangled names whose demangled form (disambiguators stripped) matches any of @p patterns."""
    found = []
    for pattern in patterns:
        rx = re.compile(pattern)
        hit = [m for m, d in demangled.items() if rx.search(DISAMBIGUATOR.sub("", d))]
        if not hit:
            raise ConfigError(
                f"check_placement: root pattern {pattern!r} matches no function"
            )
        found += hit
    return sorted(set(found))


def hot_report(
    names: list[str], sizes: dict[str, int], demangled: dict[str, str]
) -> list[tuple[str, int]]:
    """The reachable code size by library: `deluge::<library>` for namespaced code, else the first name component
    (a legacy class, a Rust crate, a C function)."""
    totals: Counter[str] = Counter()
    for name in names:
        parts = (
            DISAMBIGUATOR.sub("", demangled.get(name, name))
            .split("(")[0]
            .split("<")[0]
            .split("::")
        )
        key = (
            "::".join(parts[:2])
            if parts[0] == "deluge" and len(parts) > 2
            else parts[0]
        )
        totals[key] += sizes.get(name, 0)
    return sorted(totals.items(), key=lambda kv: (-kv[1], kv[0]))


def _run(tool: list[str], stdin: str | None = None) -> str:
    return subprocess.run(
        tool, input=stdin, check=True, capture_output=True, text=True
    ).stdout


def _symbols(nm_text: str) -> dict[str, tuple[int, int]]:
    out = {}
    for line in nm_text.splitlines():
        f = line.split()
        if len(f) == 4:
            out[f[3]] = (int(f[0], 16), int(f[1], 16))
        elif len(f) == 3:
            out[f[2]] = (int(f[0], 16), 0)
    return out


def _vtable_words(
    objcopy: str, readelf: str, elf: str, syms, demangled
) -> dict[str, list[int]]:
    """The words of every vtable linked in SRAM, by class name."""
    sections = []
    for line in _run([readelf, "-SW", elf]).splitlines():
        f = line.replace("[ ", "[").split()
        if len(f) > 5 and f[0].startswith("[") and f[1] in (".rodata", ".data"):
            sections.append((f[1], int(f[3], 16)))
    blobs = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, addr in sections:
            out = Path(tmp) / name.strip(".")
            _run([objcopy, "-O", "binary", f"--only-section={name}", elf, str(out)])
            blobs.append((addr, out.read_bytes()))
    words = {}
    for sym, (addr, size) in syms.items():
        if not sym.startswith("_ZTV") or addr < 0x20000000:
            continue
        for base, data in blobs:
            if base <= addr < base + len(data):
                raw = data[addr - base : addr - base + size]
                words[demangled.get(sym, sym).removeprefix("vtable for ")] = [
                    int.from_bytes(raw[i : i + 4], "little")
                    for i in range(0, len(raw) - 3, 4)
                ]
    return words


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("elf")
    p.add_argument(
        "--roots", default=str(Path(__file__).with_name("placement_roots.toml"))
    )
    p.add_argument("--report-hot", action="store_true")
    for tool in ("objdump", "nm", "objcopy", "readelf", "cxxfilt"):
        p.add_argument(f"--{tool}", default=f"llvm-{tool}")
    args = p.parse_args(argv)

    sys.path.insert(0, str(Path(__file__).parent))
    import compare_codegen  # its disassemble() also covers the SDRAM-resident code

    try:
        config = tomllib.loads(Path(args.roots).read_text())
        nm_text = _run([args.nm, "-S", "-n", "--defined-only", args.elf])
        fns = parse_functions(
            compare_codegen.disassemble(args.objdump, args.elf, nm_text)
        )
        syms = _symbols(nm_text)
        names = sorted(set(fns) | {s for s in syms if s.startswith("_ZTV")})
        demangled = dict(
            zip(names, _run([args.cxxfilt], "\n".join(names) + "\n").splitlines())
        )
        fn_names = {n: demangled[n] for n in fns}
        graph = call_graph(fns)
        pre_cfg = config["pre_inflate"]
        pre = match_roots(pre_cfg["roots"], fn_names)
        code_only = match_roots(pre_cfg["code_only"], fn_names)
        hot = match_roots(config["hot"]["roots"], fn_names)
        # A prefix function becomes a pseudo-root whose callees and constants are those before its `until` call.
        prefix_constants: dict[str, set[int]] = {}
        prefix_fns: list[str] = []
        for entry in pre_cfg["prefix"]:
            for name in match_roots([entry["function"]], fn_names):
                calls, constants = prefix(
                    fns[name], demangled, entry.get("until"), entry.get("until_insn")
                )
                root = (
                    f"{name} (before {entry.get('until') or entry.get('until_insn')})"
                )
                graph[root] = calls
                prefix_constants[root] = constants
                pre.append(root)
                prefix_fns.append(name)
    except (
        ConfigError,
        KeyError,
        subprocess.CalledProcessError,
        FileNotFoundError,
    ) as e:
        print(f"check_placement: {e}", file=sys.stderr)
        return 2

    findings = sdram_reached(fns, graph, pre + code_only, *SDRAM, rule="pre_inflate")
    # A prefix function's own body runs before the inflate too.
    findings += [
        Violation("pre_inflate", n, [n])
        for n in prefix_fns
        if SDRAM[0] <= fns[n].address < SDRAM[1]
    ]
    findings += sdram_reached(fns, graph, hot, *SDRAM, rule="hot")

    if "__late_bss_start" in syms and "__late_bss_end" in syms:
        lo, hi = syms["__late_bss_start"][0], syms["__late_bss_end"][0]
        for name in sorted(reach(graph, pre)):
            if name in code_only:
                continue
            constants = prefix_constants.get(
                name, fns[name].constants if name in fns else set()
            )
            if loads := sorted(c for c in constants if lo <= c < hi):
                findings.append(
                    Violation(
                        "late_bss", name, [f"loads {', '.join(hex(a) for a in loads)}"]
                    )
                )
    else:
        print(
            "check_placement: no __late_bss_start/__late_bss_end in this image; the late-statics rule is skipped"
        )

    lo, hi = syms["__sdram_text_start"][0], syms["__sdram_text_end"][0]
    allow = [re.compile(a) for a in config.get("vtables", {}).get("allow", [])]
    words = _vtable_words(args.objcopy, args.readelf, args.elf, syms, demangled)
    for cls, hits in sorted(vtable_hits(words, lo, hi).items()):
        if not any(a.search(cls) for a in allow):
            findings.append(
                Violation("vtable", cls, [f"{len(hits)} entries in SDRAM code"])
            )

    known = config.get("known", [])
    for k in known:
        if not k.get("reason"):
            print(
                f"check_placement: known entry {k.get('function')!r} gives no reason",
                file=sys.stderr,
            )
            return 2
    findings, covered, stale = apply_known(findings, known, demangled)
    for k in stale:
        print(
            f"known: {k['rule']} {k['function']!r} matches no finding any more: delete it from placement_roots.toml"
        )
    for v in covered:
        print(f"known {v.rule}: {demangled.get(v.function, v.function)}")
    for v in findings:
        chain = " -> ".join(demangled.get(n, n) for n in v.chain)
        print(f"{v.rule}: {demangled.get(v.function, v.function)}\n    {chain}")

    hot_set = [n for n in reach(graph, hot) if n in fns]
    if args.report_hot:
        sizes = {n: s for n, (_, s) in syms.items()}
        for group, size in hot_report(hot_set, sizes, demangled):
            print(f"{size:9d}  {group}")
    print(
        f"check_placement: {len(findings)} finding(s), {len(covered)} known, {len(stale)} stale known; "
        f"{len(reach(graph, pre))} pre-inflate and {len(hot_set)} hot functions walked"
    )
    return 1 if findings or stale else 0


if __name__ == "__main__":
    sys.exit(main())
