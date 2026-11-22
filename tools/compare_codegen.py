#!/usr/bin/env python3
# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Diffs two ELFs' machine code function by function, with what the layout sets masked.

When any function changes size, the code and data after it move, and every address that names them changes with
them. This compares what an address names instead of its value, so that what remains is a change in the instructions:
- a call or branch to another function keeps only that function's name; a branch inside the function becomes the
  index of the instruction it lands on;
- an address in a literal-pool word, or built by a `movw`/`movt` pair, becomes ADDR<symbol> when it falls inside a
  sized symbol, and ADDR when it falls in a loaded segment but in no symbol (merged string literals);
- a pc-relative load or adr loses its offset (its pool word is compared where it sits);
- the linker's trap fill between functions is dropped.
A value outside every loaded segment is a constant and compares as is, as does every other immediate.

Functions and call targets are named by their demangled name without its parameter list (or a template's return
type), so a function whose signature changes type but whose code doesn't, and every call to it, compare alike.
Overloads that share a name are compared as a set of bodies. ThinLTO's promoted-local suffixes (`.llvm.<hash>`) hash
the module's source, so they are dropped too.

A name that wraps a mangled one (`__const._ZN…sources`, `__dtor__ZZN…`, a linker veneer `__Thumbv7ABSLongThunk__ZN…`)
is read through its `_Z…` part, so it follows a renamed namespace and keeps its prefix and suffix. A veneer is named
by its target's canonical name; the other wrapped names keep the whole demangled text, because cutting the parameter
list would merge distinct locals and anonymous-namespace data. A mangled name the demangler cannot read (some lambda
names) has the renamed namespaces applied to its mangled text, on component boundaries, and its substitution
references (`S0_`) reduced to `S_`, because their indexes shift with the names. If two such names of one image end up
equal that way, both keep their raw names. A reference the text rules do not recognise compares as a difference.

The linker folds identical functions into one body that several symbols name. Each such body is registered under every
one of its names, so two images that print different names for one folded body compare alike while a function that
moves to another group is a change; a call to it names it by the least of its names.

The device image runs part of its code from SDRAM (the symbols __sdram_text_start and __sdram_text_end bound it, in
.sdram_init). Once the image is packed that section is no longer flagged executable, so `objdump -d` skips it; it is
disassembled as data over that range and compared with the rest. Images without those symbols are unaffected. That
section lies outside every loaded segment, so a pointer into SDRAM code or data compares as the raw constant it is.

Literal data placed at a halfword offset (seen in Rust's async state machines) prints as words that straddle two
halfwords; those are compared as they print, so such a function shows as changed whenever data moves.

A ThinLTO image's code is not fixed by its own source alone: a change in one module can move import and inlining
decisions elsewhere. A non-empty result is classified by reading each function's diff (`--diff NAME`), not taken as
a verdict.

With --rename OLD=NEW, a namespace the second image renamed is read under its new name in both images.

Usage:
    tools/compare_codegen.py BASE.elf NEW.elf [--rename OLD=NEW ...] [--objdump llvm-objdump] [--nm llvm-nm] [--readelf llvm-readelf]
                             [--cxxfilt llvm-cxxfilt] [--show N] [--diff NAME ...]
"""

from __future__ import annotations

import argparse
import bisect
import difflib
import re
import subprocess
import sys

HEADER = re.compile(r"^([0-9a-f]+) <(.+)>:$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+(.*)$")
ANNOTATION = re.compile(r"\s*@.*$")
RAW_BYTES = re.compile(r"^(?:[0-9a-f]{2} )+\s*")
TARGET = re.compile(r"0x([0-9a-f]+)\s+<([^>+]+)(?:\+0x[0-9a-f]+)?>")
SYMBOL = re.compile(r"<([^<>]+)>")
LTO_SUFFIX = re.compile(r"\.llvm\.\d+")
WORD = re.compile(r"^\.word\s+0x([0-9a-f]{8})$")
PC_RELATIVE = re.compile(r"\[pc, #-?\w+\]")
ADR = re.compile(r"^(adr\S*\s+\w+), #-?\w+")
MOV_HALF = re.compile(r"^(mov[wt])(\S*) (\w+), #0x([0-9a-f]+)$")
UNCONDITIONAL_BRANCH = re.compile(r"^(?:b(?:\.w)?\s|bx\s|pop(?:\.w)?\s+\{.*\bpc\})")
LOCAL_TARGET = re.compile(r"<\.(\d+)>")
# Mnemonics whose first operand is read, not written.
NO_WRITE = re.compile(r"^(?:str|cmp|cmn|tst|teq|b|push|vst|vcmp|it|pld)")
# lld fills the gaps between functions with 0xd4: `bmi .-0x54` as Thumb, `ldrble sp, [r4], #1236` as ARM. The fill's
# length follows the layout, so a function's trailing fill is dropped.
PADDING = re.compile(r"^\s*(?:bmi\s.*@ imm = #-0x58|ldrble\s+sp, \[r4\], #1236)\s*$")


def keep(name: str) -> str:
    return name


def low_ram(value: int) -> str | None:
    """A classify for tests: the 0x20000000 SRAM as the one loaded segment, no symbols."""
    return "ADDR" if 0x20000000 <= value < 0x30000000 else None


def canonical(demangled: str) -> str:
    """A demangled name without its final parameter list, the qualifiers after it, or a leading return type."""
    name = demangled.strip()
    if "(" not in name:
        return name  # data and special symbols ("vtable for X") keep their whole name
    close = name.rfind(")")
    if close > 0 and not name.endswith(">"):
        depth = 0
        for i in range(close, -1, -1):
            depth += {")": 1, "(": -1}.get(name[i], 0)
            if depth == 0:
                name = name[:i]
                break
    depth = 0
    for i in range(len(name) - 1, -1, -1):
        depth += {">": 1, ")": 1, "<": -1, "(": -1}.get(name[i], 0)
        if depth == 0 and name[i] == " " and not name[:i].endswith("operator"):
            return name[i + 1 :]
    return name


def normalise(line: str, rename=keep, classify=low_ram, local=None, alias=None) -> str:
    """One disassembled instruction with its raw bytes, annotations and pc-relative offsets removed. A target in the
    function goes through @p local (address -> its instruction index); any other goes through @p alias (address -> the
    name of a body several symbols share) or, without one, @p rename. A literal word that @p classify names an address
    becomes that name."""
    text = RAW_BYTES.sub("", ANNOTATION.sub("", line.strip()))

    def target(m: re.Match) -> str:
        if local is not None:
            index = local(int(m.group(1), 16))
            if index is not None:
                return f"<.{index}>"
        shared = (alias or {}).get(int(m.group(1), 16))
        if shared is not None:
            return f"<{shared}>"
        return f"<{rename(LTO_SUFFIX.sub('', m.group(2)))}>"

    text = TARGET.sub(target, text)
    # Whatever names a symbol without an address (adr's target): its offset follows the layout.
    text = SYMBOL.sub(
        lambda m: (
            m.group(0)
            if m.group(1).startswith(".")
            else f"<{rename(LTO_SUFFIX.sub('', re.sub(r'\+0x[0-9a-f]+$', '', m.group(1))))}>"
        ),
        text,
    )
    text = PC_RELATIVE.sub("[pc]", text)
    text = ADR.sub(r"\1", text)
    text = " ".join(text.split())
    word = WORD.match(text)
    if word:
        name = classify(int(word.group(1), 16))
        if name is not None:
            return f".word {name}"
    return text


def written(insn: str) -> str | None:
    """The register @p insn writes as its first operand, if it writes one."""
    fields = insn.split(None, 1)
    if len(fields) != 2 or NO_WRITE.match(fields[0]):
        return None
    return fields[1].split(",", 1)[0].strip()


def reaching_lows(body: list[str], at: int, register: str, seen: set[int]) -> list[int]:
    """The `movw`s of @p register that reach instruction @p at: walking back, by fall-through and by every branch into
    the stretch walked, until the register is otherwise written or an unconditional `movw` settles that path."""
    lows: list[int] = []
    stop = -1
    for j in range(at - 1, -1, -1):
        if UNCONDITIONAL_BRANCH.match(body[j]):
            stop = j
            break
        half = MOV_HALF.match(body[j])
        if half and half.group(3) == register:
            if half.group(1) == "movw":
                lows.append(j)
                if not half.group(2):
                    stop = j
                    break
                continue
            stop = j
            break
        if written(body[j]) == register:
            stop = j
            break
    for k, insn in enumerate(body):
        landing = LOCAL_TARGET.search(insn)
        if (
            landing
            and insn.startswith(("b", "cb"))
            and stop < int(landing.group(1)) <= at
            and k not in seen
        ):
            seen.add(k)
            lows += reaching_lows(body, k, register, seen)
    return lows


def mask_address_halves(body: list[str], classify=low_ram) -> list[str]:
    """@p body with each `movt` that completes an address, and the `movw`s that reach it (reaching_lows), naming the
    address. A `movt` no `movw` reaches pairs with the next `movw` of its register."""
    out = list(body)
    for i, insn in enumerate(body):
        high = MOV_HALF.match(insn)
        if not high or high.group(1) != "movt":
            continue
        register = high.group(3)
        lows = sorted(set(reaching_lows(body, i, register, {i})))
        if not lows:
            for j in range(i + 1, len(body)):
                half = MOV_HALF.match(body[j])
                if half and half.group(1) == "movw" and half.group(3) == register:
                    lows.append(j)
                    break
                if written(body[j]) == register:
                    break
        named = {}
        for j in lows:
            name = classify(
                (int(high.group(4), 16) << 16)
                | int(MOV_HALF.match(body[j]).group(4), 16)
            )
            if name is None:
                break
            named[j] = name
        else:
            if lows:
                for j, name in named.items():
                    out[j] = (
                        f"movw{MOV_HALF.match(body[j]).group(2)} {register}, lo({name})"
                    )
                out[i] = (
                    f"movt{high.group(2)} {register}, hi({' '.join(sorted(set(named.values())))})"
                )
    return out


def functions(
    disassembly: str, rename=keep, classify=low_ram, alias=None
) -> dict[str, list[tuple[str, ...]]]:
    """Each function's normalised instructions, keyed by its name passed through @p rename, or by its @p alias (see
    aliases()) when its address has one; the bodies of the functions that share a key, sorted. Addresses go through
    @p classify."""
    found: dict[str, list[list[tuple[int, str]]]] = {}
    current: list[tuple[int, str]] | None = None
    for line in disassembly.splitlines():
        header = HEADER.match(line)
        if header:
            current = []
            for name in (alias or {}).get(int(header.group(1), 16)) or (
                rename(LTO_SUFFIX.sub("", header.group(2))),
            ):
                found.setdefault(name, []).append(current)
            continue
        insn = INSN.match(line)
        if insn and current is not None:
            current.append((int(insn.group(1), 16), insn.group(2)))
        elif not line.strip():
            current = None
    least = {address: names[0] for address, names in (alias or {}).items()}
    result = {}
    for name, bodies in found.items():
        normalised = []
        for body in bodies:
            while body and PADDING.match(body[-1][1]):
                body.pop()
            index = {address: i for i, (address, _) in enumerate(body)}
            lines = [
                normalise(text, rename, classify, index.get, least) for _, text in body
            ]
            normalised.append(tuple(mask_address_halves(lines, classify)))
        result[name] = sorted(normalised)
    return result


def compare(
    base: dict[str, list], new: dict[str, list]
) -> tuple[list[str], list[str], list[str]]:
    """The changed, added and removed function names."""
    changed = sorted(n for n in base.keys() & new.keys() if base[n] != new[n])
    return changed, sorted(new.keys() - base.keys()), sorted(base.keys() - new.keys())


def run(tool: list[str], stdin: str | None = None) -> str:
    return subprocess.run(
        tool, input=stdin, check=True, capture_output=True, text=True
    ).stdout


def disassemble(objdump: str, elf: str, nm_text: str) -> str:
    """@p elf's disassembly: its executable sections, plus the SDRAM-resident code when @p nm_text (its
    `nm -S -n --defined-only` listing) defines both ends of that range."""
    text = run([objdump, "-d", "--no-show-raw-insn", elf])
    ends = {}
    for line in nm_text.splitlines():
        fields = line.split()
        if fields and fields[-1] in ("__sdram_text_start", "__sdram_text_end"):
            ends[fields[-1]] = fields[0]
    if len(ends) == 2:
        text += run(
            [
                objdump,
                "-D",
                "--no-show-raw-insn",
                "-j",
                ".sdram_init",
                f"--start-address=0x{ends['__sdram_text_start']}",
                f"--stop-address=0x{ends['__sdram_text_end']}",
                elf,
            ]
        )
    return text


def namespace_renamer(pairs: list[tuple[str, str]]):
    """Maps each renamed namespace to its new spelling wherever it stands in a demangled name, so a function that only
    moved namespace compares with itself. Pairs are ordered: at a given place the first pair that matches wins, and a
    name is rewritten once, so a pair's new spelling is never renamed again. A pair matches whole `::` components."""
    if not pairs:
        return lambda name: name
    news = dict(pairs)
    pattern = re.compile(
        r"(?<![\w:])(?:" + "|".join(re.escape(old) for old, _ in pairs) + r")(?=::|\b)"
    )

    def rename(name: str) -> str:
        return pattern.sub(lambda match: news[match.group(0)], name)

    return rename


def mangled_renamer(pairs: list[tuple[str, str]]):
    """A rename for a name no demangler can read: the pairs applied to its mangled text, where a namespace is a run of
    length-prefixed components (`6deluge10instrument`) that starts at a component boundary (not inside an identifier,
    not under another namespace). Where a pair keeps its first component, the rest is also renamed behind `S_`, but only
    in a name that starts with that component, because only there is `S_` known to stand for it. A reference through
    another substitution (`S0_`) to the same namespace is not recognised: the name then compares as different, never as
    the same. Substitution indexes shift with the names, so in a name that holds a renamed namespace (either spelling) each
    reference is reduced to `S_`; any other name keeps its text, since nothing in it moves. The reduction can make two
    names of one image equal, which demangler() detects."""

    def run_of(components: list[str]) -> str:
        return "".join(f"{len(c)}{c}" for c in components)

    plain: dict[str, str] = {}
    behind_head: dict[str, dict[str, str]] = {}
    for old, new in pairs:
        a, b = old.split("::"), new.split("::")
        plain.setdefault(run_of(a), run_of(b))
        if a[0] == b[0] and len(a) > 1:
            behind_head.setdefault(run_of(a[:1]), {}).setdefault(
                run_of(a[1:]), run_of(b[1:])
            )
    # Both spellings of every renamed namespace, also as the tail behind `S_`: a name without one has nothing to rename.
    spellings = {
        text
        for old, new in pairs
        for spelling in (old, new)
        for text in (
            run_of(spelling.split("::")),
            "S_" + run_of(spelling.split("::")[1:]),
        )
        if text != "S_"
    }
    patterns: dict[tuple[str, ...], tuple[re.Pattern | None, dict[str, str]]] = {}

    def pattern_for(heads: tuple[str, ...]):
        if heads not in patterns:
            table = dict(plain)
            behind = {}
            for head in heads:
                behind.update(behind_head[head])
            alternatives = [r"(?<![0-9a-z])" + re.escape(o) for o in table] + [
                r"(?<=S_)" + re.escape(o) for o in behind
            ]
            patterns[heads] = (
                re.compile("(?:" + "|".join(alternatives) + ")")
                if alternatives
                else None,
                {**table, **behind},
            )
        return patterns[heads]

    def rename(name: str) -> str:
        collapses = any(spelling in name for spelling in spellings)
        heads = tuple(
            sorted(
                h
                for h in behind_head
                if re.match(r"_Z+N[rVK]*[RO]?" + re.escape(h), name)
            )
        )
        pattern, table = pattern_for(heads)
        if pattern is not None:
            name = pattern.sub(lambda m: table[m.group(0)], name)
        return SUBSTITUTION.sub("S_", name) if collapses else name

    return rename


def aliases(nm_text: str, rename=keep) -> dict[int, tuple[str, ...]]:
    """For each address several symbols share (functions the linker folded into one body), all of their @p rename names,
    least first. The least names the body wherever it is called; functions() registers the body under every one, so a
    function that moves to another group is a change whichever name it has."""
    at: dict[int, set[str]] = {}
    for line in nm_text.splitlines():
        if not NM_LINE.match(line):
            continue
        address, size, kind, name = line.split()
        if kind in "aA" or int(size, 16) == 0 or name[0] in ".$":
            continue
        at.setdefault(int(address, 16), set()).add(rename(LTO_SUFFIX.sub("", name)))
    return {a: tuple(sorted(names)) for a, names in at.items() if len(names) > 1}


def symbol_names(texts: list[str]) -> set[str]:
    """The symbol names @p texts (disassembly or `llvm-nm` listings) mention, without ThinLTO suffixes."""
    return {
        LTO_SUFFIX.sub("", n)
        for text in texts
        for n in re.findall(r"<([^<>+\s]+)", text)
    } | {
        LTO_SUFFIX.sub("", line.split()[-1])
        for text in texts
        for line in text.splitlines()
        if NM_LINE.match(line)
    }


def demangler(cxxfilt: str, texts: list[str], renames=None, mangled=None, images=None):
    """A rename that maps every mangled symbol in @p texts to its canonical() demangled name, with @p renames (see
    namespace_renamer()) applied first. A name that wraps a mangled one (`__const._ZN…sources`, a linker veneer, a static
    guard's destructor) is renamed through the mangled part and keeps its prefix and suffix. A veneer is named by its
    target's canonical name; any other wrapped name keeps the whole demangled text, which tells apart what canonical()
    would cut (`(anonymous namespace)`, a local `f()::scene`). A mangled name the demangler cannot read goes through
    @p mangled (mangled_renamer()). @p images holds each image's own texts: if two names of one image reach the same
    key that way, they keep their raw names instead, since renaming would merge them."""
    names = sorted(symbol_names(texts))
    split = [WRAPPED.match(n) for n in names]
    targets = [m.group(2) if m else n for m, n in zip(split, names)]
    demangled = run([cxxfilt], "\n".join(targets)).splitlines()
    table = {}
    unreadable = set()
    for raw, m, target, plain in zip(names, split, targets, demangled):
        if plain == target and m and mangled:
            table[raw] = m.group(1) + mangled(target) + m.group(3)
            unreadable.add(raw)
            continue
        if renames:
            plain = renames(plain)
        if not m or not m.group(1):
            table[raw] = canonical(plain)
        elif m.group(1).endswith("Thunk_"):
            table[raw] = m.group(1) + canonical(plain) + m.group(3)
        else:
            table[raw] = m.group(1) + plain + m.group(3)
    for image in images if images is not None else [texts]:
        seen: dict[str, str] = {}
        merged = set()
        for raw in sorted(symbol_names(image) & unreadable):
            if seen.setdefault(table[raw], raw) != raw:
                merged |= {raw, seen[table[raw]]}
        for raw in merged:
            table[raw] = raw
    return lambda name: table.get(name, name)


# A mangled name inside a wrapper: a prefix (`__const.`, `__Thumbv7ABSLongThunk_`, `__dtor_`), `_Z…`, a `.suffix`.
WRAPPED = re.compile(r"^(.*?)(_Z[\w$]+)((?:\.\w[\w.]*)?)$")
# A substitution reference in a mangled name: `S_`, `S0_`, `SK_`.
SUBSTITUTION = re.compile(r"S[0-9A-Z]*_")
NM_LINE = re.compile(r"^[0-9a-f]{8} [0-9a-f]{8} [a-zA-Z?] \S+$")


def classifier(segments_text: str, nm_sized: str, rename=keep):
    """A classify for one image: None for a value outside its loaded segments (from `llvm-readelf -lW`), ADDR<name> for
    one inside a sized data or code symbol (from `llvm-nm -S -n`), ADDR otherwise."""
    segments = []
    for line in segments_text.splitlines():
        fields = line.split()
        if fields and fields[0] == "LOAD":
            start, size = int(fields[2], 16), int(fields[5], 16)
            if size:
                segments.append((start, start + size))
    starts: list[int] = []
    ends: list[int] = []
    names: list[str] = []
    for line in nm_sized.splitlines():
        if not NM_LINE.match(line):
            continue
        address, size, kind, name = line.split()
        # Local labels (.str.N, .L…) name merged string literals, which the linker places and renames freely.
        if kind in "aA" or int(size, 16) == 0 or name.startswith("."):
            continue
        starts.append(int(address, 16))
        ends.append(int(address, 16) + int(size, 16))
        names.append(rename(LTO_SUFFIX.sub("", name)))

    def classify(value: int) -> str | None:
        # An end-of-segment pointer (a linker-script `_end` symbol) is an address too.
        if not any(start <= value <= end for start, end in segments):
            return None
        # Thumb function addresses carry bit 0. Aliases share a start: the least name of those containing it.
        for probe in (value, value & ~1):
            i = bisect.bisect_right(starts, probe) - 1
            containing = []
            while (
                i >= 0 and starts[i] == starts[bisect.bisect_right(starts, probe) - 1]
            ):
                if probe < ends[i]:
                    containing.append(names[i])
                i -= 1
            if containing:
                return f"ADDR<{min(containing)}>"
        return "ADDR"

    return classify


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("base")
    parser.add_argument("new")
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--nm", default="llvm-nm")
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--cxxfilt", default="llvm-cxxfilt")
    parser.add_argument(
        "--rename",
        action="append",
        default=[],
        help="OLD=NEW: a namespace the second image renamed",
    )
    parser.add_argument(
        "--show", type=int, default=20, help="list at most this many names per kind"
    )
    parser.add_argument(
        "--diff",
        nargs="*",
        default=[],
        help="print these changed functions' masked diffs",
    )
    args = parser.parse_args()
    images = {}
    for which in (args.base, args.new):
        nm = run([args.nm, "-S", "-n", "--defined-only", which])
        images[which] = (
            disassemble(args.objdump, which, nm),
            nm,
            run([args.readelf, "-lW", which]),
        )
    pairs = []
    for spec in args.rename:
        old, sep, new = spec.partition("=")
        if not sep or not old or not new:
            parser.error(f"--rename wants OLD=NEW, got {spec!r}")
        pairs.append((old, new))
    rename = demangler(
        args.cxxfilt,
        [text for image in images.values() for text in image[:2]],
        namespace_renamer(pairs),
        mangled_renamer(pairs),
        [list(image[:2]) for image in images.values()],
    )
    base, new = (
        functions(dis, rename, classifier(segments, nm, rename), aliases(nm, rename))
        for dis, nm, segments in images.values()
    )
    changed, added, removed = compare(base, new)
    for kind, names in (("changed", changed), ("added", added), ("removed", removed)):
        print(f"{kind}: {len(names)}")
        for name in names[: args.show]:
            print(f"  {name}")
    for name in args.diff:
        print(f"== {name}")
        before = [line for body in base.get(name, []) for line in (*body, "--")]
        after = [line for body in new.get(name, []) for line in (*body, "--")]
        for line in difflib.unified_diff(before, after, lineterm="", n=1):
            print(line)
    return 1 if changed or added or removed else 0


if __name__ == "__main__":
    sys.exit(main())
