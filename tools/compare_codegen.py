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

Functions and call targets are named by their demangled name without the function's own parameter list, the qualifiers
and requires clause after it, and a template's leading return type, so a function whose signature changes type but whose
code doesn't, and every call to it, compare alike. Nothing else is cut: `(anonymous namespace)` is never a parameter
list, a local or a lambda (`f(int) const::$_0::operator()`, `int r<int>()::x`) keeps its enclosing function's signature
and return type, a compiler clone keeps its suffix (`.constprop.0`, a split global's `.0`) and a thunk its prefix, and
data keeps its whole name. ThinLTO's promoted-local suffixes (`.llvm.<hash>`) hash the module's source, so they are
dropped too.

A key names one symbol of an image. Where several symbols of one image reach one name (overloads; a constructor's or
destructor's variants, whose base and deleting bodies differ; a local's discriminated twins; one function's thunks for
several bases; a Rust generic instantiated in two crates), the demangled text cannot tell them apart, so each of them is
named by its mangled name instead, renamed as below, together with its full renamed demangled text. That needs no
reading of the mangling. Within an image no two symbols share a key. Across images two symbols share a key only when
both their renamed mangled text (with substitution references reduced) and their full renamed demangled text agree, so a
renamed namespace's entities pair with themselves and nothing that either text tells apart. A name llvm-cxxfilt cannot
read is demangled by GNU c++filt (`--gnu-cxxfilt`, the dbt toolchain's by default) and keyed the same way; a name
neither can read keeps its exact mangled text, renamed or not, so it pairs only with itself and a rename shows it as
removed and added. The cost is false positives: such a symbol whose signature changes, or whose group gains or loses a
member, shows as removed and added; and because a local keeps its enclosing signature, a signature-only change to a
function that holds a lambda or a static local shows that lambda or local as added and removed (and the function as
changed, as its calls name the new key).

A name that wraps a mangled one (`__const._ZN…sources`, `__dtor__ZZN…`, a linker veneer `__Thumbv7ABSLongThunk__ZN…`) is
read through its `_Z…` part, so it follows a renamed namespace and keeps its prefix and suffix. A veneer is named by its
target's canonical name; the other wrapped names name data that belongs to what they wrap, so they keep the whole
demangled text, as a local does. A name keyed by its mangled text has the renamed namespaces applied to that text, on
component boundaries, and its substitution references (`S0_`) reduced to `S_`, because their indexes shift with the
names. If two such keys of one image end up equal that way, both symbols keep their raw names. A reference the text
rules do not recognise compares as a difference.

Two symbols of one image can also share a name outright: two modules' statics or anonymous-namespace symbols, and
promoted locals whose names differ only in `.llvm.<hash>`. Each copy is named by its place (number_twins()): the nearest
symbol before it in its section that is no such twin, which stands for its module, by that symbol's key, and its ordinal
among the copies behind that symbol. An `adr`, which prints no address, is placed by the address it computes, in ARM or
Thumb as the mapping symbols (`nm --special-syms`) say. Where each copy has an anchor of its own, a reference moved from
one copy to another is a change, whichever copies the other image adds or drops. A copy that does not have one (it sits
behind no anchor, or behind one with another copy of its name) cannot be told from its neighbour by place, so a name
with such a copy never pairs: it is written `#!` in the base and `#?` in the new image, so its copies show as removed
and added and every reference to one as changed, even between identical images. Local labels (`.…`,
`GCC_except_table…`), of which an image has hundreds, keep pairing by place, so a reference moved between two of them
behind one anchor can compare clean. When a name's set of places differs between the images, none of its copies in the
new image pairs with the base (`#?`), so every copy shows as removed and added and every reference to one as changed: a
false positive at worst, but no copy is paired by a guess. The same holds when the anchor symbol itself changes its key.

rustc and LLVM uniquify a Rust internal function that would clash as `name.N`, with N counted across the image, so a
change anywhere renumbers it. Such a code symbol is read as `name.0`, and when several are, they are twins as above:
named by their places, never merged, and a reference moved between them is a change. The symbol left unsuffixed keeps
its name, and a data symbol's `.N` (a global split into its parts) and other suffixes (`.constprop.0`) keep their text.
A Rust name keyed by its mangled text drops its crates' hashes (`Cs6NFbV2PXKjr_` before `15deluge_bsp_rust`), because a
moved crate changes its own hash and the hashes of the crates that depend on it, and drops the crate that instantiated a
generic (the `<crate-root>` after a v0 name's path, read by its grammar), because LTO keeps any one crate's copy of the
same code. Where that leaves two symbols of one image on one key, they take their instantiating crates back, and then,
if still alike, their raw names. Names at one address (folded aliases) are one symbol and may share a key: a reference
to either is the same.

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

With --rename OLD=NEW, a namespace the second image renamed is read under its new name in both images. Pairs are
tried in order and the first whose OLD matches wins; an OLD that is a prefix of its own NEW
(`deluge::ui=deluge::ui::widgets::core`) needs an identity pair for NEW first
(`deluge::ui::widgets=deluge::ui::widgets`), because the renames apply to both images. An image that holds an unmoved
OLD::f beside a moved NEW::f reads both as one name, so they keep their raw names and show as added and removed.

Usage:
    tools/compare_codegen.py BASE.elf NEW.elf [--rename OLD=NEW ...]
                             [--objdump llvm-objdump] [--nm llvm-nm] [--readelf llvm-readelf]
                             [--cxxfilt llvm-cxxfilt] [--show N] [--diff NAME ...]
"""

from __future__ import annotations

import argparse
import bisect
import difflib
import os
import re
import shutil
import subprocess
import sys

# GNU c++filt from the dbt toolchain, which reads some names llvm-cxxfilt cannot.
GNU_CXXFILT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..",
    "toolchain",
    "current",
    "arm-none-eabi-gcc",
    "bin",
    "arm-none-eabi-c++filt",
)
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


# Demangler names that wrap a type or a datum: they keep their whole text, as data does.
WRAPS_DATA = re.compile(
    r"^(?:vtable|VTT|construction vtable|typeinfo|typeinfo name|guard variable|reference temporary #\d+"
    r"|TLS (?:init|wrapper) function) for "
)
# Demangler names that wrap a function: the function inside is keyed as usual.
WRAPS_FUNCTION = re.compile(r"^(?:non-virtual |virtual |covariant return )thunk to ")
# A compiler clone's suffix (`f(int) (.constprop.0)`): a separate body, so it stays in the key.
CLONE_SUFFIX = re.compile(r"(?: \(\.[\w.]+\))+$")
# What may follow a function's own parameter list: cv and ref qualifiers.
OWN_QUALIFIERS = re.compile(r"(?:\s*(?:const\b|volatile\b|&&|&))*\s*")
# An operator's symbol, longest first; one that holds a bracket must not be read as one.
OPERATOR_SYMBOLS = sorted(
    [
        "()",
        "[]",
        "<=>",
        "<<=",
        ">>=",
        "->*",
        "<<",
        ">>",
        "<=",
        ">=",
        "->",
        "<",
        ">",
        "==",
        "!=",
        "+=",
        "-=",
        "*=",
        "/=",
        "%=",
        "&=",
        "|=",
        "^=",
        "++",
        "--",
        "&&",
        "||",
        "+",
        "-",
        "*",
        "/",
        "%",
        "^",
        "&",
        "|",
        "~",
        "!",
        "=",
        ",",
    ],
    key=len,
    reverse=True,
)
OPENS = {")": "(", ">": "<", "}": "{", "]": "["}


def operator_end(name: str, at: int) -> int:
    """Where the operator name whose `operator` keyword starts at @p at ends: past its symbol, or for a word operator
    (`new`, `delete[]`, a conversion `operator unsigned int`) at the bracket that follows its name at its own level."""
    after = at + len("operator")
    for symbol in OPERATOR_SYMBOLS:
        end = after + len(symbol)
        if name.startswith(symbol, after) and (end == len(name) or name[end] in "<(),"):
            return end
    depth = 0
    for i in range(after, len(name)):
        c = name[i]
        if depth == 0 and c in "(),":
            return i
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
    return len(name)


def top_level(name: str) -> tuple[list[tuple[int, int]], list[int]]:
    """@p name's top-level `(…)` groups as (start, end) and its top-level spaces outside operator names. Brackets pair
    as a stack; `<` and `>` inside parentheses are comparisons, and an operator's symbol is no bracket."""
    groups: list[tuple[int, int]] = []
    spaces: list[int] = []
    stack: list[tuple[str, int]] = []
    i = 0
    while i < len(name):
        if (
            name.startswith("operator", i)
            and (i == 0 or not (name[i - 1].isalnum() or name[i - 1] in "_$"))
            and not (
                i + 8 < len(name) and (name[i + 8].isalnum() or name[i + 8] in "_$")
            )
        ):
            i = operator_end(name, i)
            continue
        c = name[i]
        inside_parens = bool(stack) and stack[-1][0] == "("
        if c in "({[" or (c == "<" and not inside_parens):
            stack.append((c, i))
        elif c in ")}]" or (c == ">" and not inside_parens):
            while stack and stack[-1][0] != OPENS[c]:
                stack.pop()
            if stack:
                opened = stack.pop()[1]
                if not stack and c == ")":
                    groups.append((opened, i + 1))
        elif c == " " and not stack:
            spaces.append(i)
        i += 1
    return groups, spaces


def canonical(demangled: str, local: bool = False) -> str:
    """A demangled name without what a signature change alters: a function's own parameter list, the qualifiers after
    it, its requires clause and its leading return type. Everything else in the text stays, so that two different
    texts share a key only when they are overloads: `(anonymous namespace)` is never a parameter list, and a local or
    a lambda (`f(int) const::$_0::operator()`, `int r<int>()::x`) keeps its enclosing function's signature and return
    type as written. @p local says the symbol is a local entity (`_ZZ…`): the demangled text cannot say where a requires
    clause ends, and in a local the first one is its enclosing function's, so none is cut."""
    name = demangled.strip()
    if WRAPS_DATA.match(name):
        return name
    thunk = WRAPS_FUNCTION.match(name)
    if thunk:
        return thunk.group(0) + canonical(name[thunk.end() :], local)
    clone = CLONE_SUFFIX.search(name)
    suffix = clone.group(0) if clone else ""
    if clone:
        name = name[: clone.start()]
    groups, spaces = top_level(name)
    # A requires clause is free text (braces, comparisons); it ends the name at the first top-level `requires`.
    for space in spaces if not local else []:
        if name.startswith(" requires ", space):
            name = name[:space]
            groups = [g for g in groups if g[1] <= space]
            spaces = [s for s in spaces if s < space]
            break
    if not groups:
        return name + suffix
    start, end = groups[-1]
    if name[start:end] == "(anonymous namespace)" or not OWN_QUALIFIERS.fullmatch(
        name[end:]
    ):
        return name + suffix
    name = name[:start]
    if any(name[g0:g1] != "(anonymous namespace)" for g0, g1 in groups[:-1]):
        return (
            name + suffix
        )  # a local: what leads is its enclosing function's return type
    # A leading return type ends at the last top-level space.
    for space in reversed([s for s in spaces if s < start]):
        name = name[space + 1 :]
        break
    return name + suffix


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
) -> dict[str, tuple[str, ...]]:
    """Each function's normalised instructions, keyed by its name passed through @p rename, or by its @p alias (see
    aliases()) when its address has one. Addresses go through @p classify. A key names one body: one listed twice at
    its address (a section disassembled twice) is read once, and two bodies under one key are an error, because
    comparing them as one would hide a reference moved between them."""
    found: dict[str, tuple[int, list[tuple[int, str]]]] = {}
    current: list[tuple[int, str]] | None = None
    for line in disassembly.splitlines():
        header = HEADER.match(line)
        if header:
            current, start = [], int(header.group(1), 16)
            for name in (alias or {}).get(start) or (
                rename(LTO_SUFFIX.sub("", header.group(2))),
            ):
                if name not in found:
                    found[name] = (start, current)
                elif found[name][0] != start:
                    raise ValueError(f"two bodies are named {name}")
            continue
        insn = INSN.match(line)
        if insn and current is not None:
            current.append((int(insn.group(1), 16), insn.group(2)))
        elif not line.strip():
            current = None
    least = {address: names[0] for address, names in (alias or {}).items()}
    result = {}
    for name, (_, body) in found.items():
        while body and PADDING.match(body[-1][1]):
            body.pop()
        index = {address: i for i, (address, _) in enumerate(body)}
        lines = [
            normalise(text, rename, classify, index.get, least) for _, text in body
        ]
        result[name] = tuple(mask_address_halves(lines, classify))
    return result


def compare(
    base: dict[str, tuple], new: dict[str, tuple]
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
    news: dict[str, str] = {}
    for old, new in pairs:
        news.setdefault(old, new)
    pattern = re.compile(
        r"(?<![\w:])(?:" + "|".join(re.escape(old) for old, _ in pairs) + r")(?=::|\b)"
    )

    def rename(name: str) -> str:
        return pattern.sub(lambda match: news[match.group(0)], name)

    return rename


def mangled_renamer(pairs: list[tuple[str, str]]):
    """A rename for a name keyed by its mangled text (see demangler()): the pairs applied to that text, where a
    namespace is a run of length-prefixed components (`6deluge10instrument`) that starts at a component boundary (not
    inside an identifier, not under another namespace). Where a pair keeps its first component, the rest is also renamed
    behind `S_`, but only in a name that starts with that component, because only there is `S_` known to stand for it. A
    reference through another substitution (`S0_`) to the same namespace is not recognised: the name then compares as
    different, never as the same. Substitution indexes shift with the names, so in a name that holds a renamed namespace
    (either spelling) each reference is reduced to `S_`; any other name keeps its text, since nothing in it moves. The
    reduction can make two names of one image equal, which demangler() detects."""

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


# A function's header in a disassembly; an address with the symbol it falls in (not an immediate's `#0x…`); an `adr`,
# which prints its target's name but not its address.
TWIN_HEADER = re.compile(r"^([0-9a-f]+) <([^<>]+)>:$", re.MULTILINE)
TWIN_TARGET = re.compile(r"(?<!#)0x([0-9a-f]+)(\s+)<([^<>+]+)((?:\+0x[0-9a-f]+)?)>")
TWIN_ADR = re.compile(
    r"^(\s*([0-9a-f]+):\s+adr\S*\s+\w+,\s+#(-?(?:0x[0-9a-f]+|\d+))\s+)<([^<>+]+)((?:\+0x[0-9a-f]+)?)>",
    re.MULTILINE,
)
# A twin's number as number_twins() writes it: `#N~anchor`; `#?N~anchor` in the new image and `#!N~anchor` in the base
# for a copy that must not pair (not `@`, which starts a disassembly's annotation).
TWIN = re.compile(r"#([?!]?\d+)~(.+)$")
SECTION = re.compile(r"\]\s+(\S+)\s+\S+\s+([0-9a-f]{8})\s+[0-9a-f]+\s+([0-9a-f]+)")


def sections_of(readelf_sections: str) -> list[tuple[int, int, str]]:
    """The loaded sections (start, end, name) of a `readelf -SW` listing."""
    found = []
    for line in readelf_sections.splitlines():
        m = SECTION.search(line)
        if m and int(m.group(2), 16):
            found.append(
                (
                    int(m.group(2), 16),
                    int(m.group(2), 16) + int(m.group(3), 16),
                    m.group(1),
                )
            )
    return found


def nm_symbols(nm_text: str) -> list[tuple[int, int, str]]:
    """The (address, size, name) of each symbol in @p nm_text (`nm -n`, with or without `-S`), mapping symbols left."""
    found = []
    for line in nm_text.splitlines():
        fields = line.split()
        if len(fields) < 3 or not re.fullmatch(r"[0-9a-f]+", fields[0]):
            continue
        size = int(fields[1], 16) if len(fields) == 4 else 0
        found.append((int(fields[0], 16), size, fields[-1]))
    return found


class RustPath:
    """Just enough of a Rust v0 mangled name's grammar to find where its path ends: after it may come only the
    crate that instantiated a generic, which names no part of the code. Back references are skipped, not followed."""

    BASE62 = re.compile(r"[0-9a-zA-Z]*_")

    def __init__(self, text: str):
        self.text, self.at = text, 0

    def peek(self) -> str:
        return self.text[self.at : self.at + 1]

    def take(self, char: str) -> bool:
        if self.peek() == char:
            self.at += 1
            return True
        return False

    def base62(self) -> None:
        m = self.BASE62.match(self.text, self.at)
        if not m:
            raise ValueError("base-62 number")
        self.at = m.end()

    def ident(self) -> None:
        self.take("u")
        m = re.compile(r"\d+").match(self.text, self.at)
        if not m:
            raise ValueError("identifier")
        self.at = m.end()
        self.take("_")
        self.at += int(m.group(0))
        if self.at > len(self.text):
            raise ValueError("identifier length")

    def disambiguator(self) -> None:
        if self.take("s"):
            self.base62()

    def path(self) -> None:
        tag = self.peek()
        self.at += 1
        if tag == "C":
            self.disambiguator()
            self.ident()
        elif tag == "N":
            self.at += 1  # the namespace
            self.path()
            self.disambiguator()
            self.ident()
        elif tag == "M":
            self.disambiguator()
            self.path()
            self.type()
        elif tag == "X":
            self.disambiguator()
            self.path()
            self.type()
            self.path()
        elif tag == "Y":
            self.type()
            self.path()
        elif tag == "I":
            self.path()
            while not self.take("E"):
                self.generic_arg()
        elif tag == "B":
            self.base62()
        else:
            raise ValueError(f"path tag {tag!r}")

    def generic_arg(self) -> None:
        if self.take("L"):
            self.base62()
        elif self.take("K"):
            self.const()
        else:
            self.type()

    def const(self) -> None:
        if self.take("p"):
            return
        if self.take("B"):
            self.base62()
            return
        self.type()
        self.take("n")
        m = re.compile(r"[0-9a-f]*_").match(self.text, self.at)
        if not m:
            raise ValueError("const value")
        self.at = m.end()

    def type(self) -> None:
        tag = self.peek()
        if tag and tag in "abcdefhijlmnostuvxyz":
            self.at += 1
        elif tag in ("A",):
            self.at += 1
            self.type()
            self.const()
        elif tag in ("S", "P", "O"):
            self.at += 1
            self.type()
        elif tag in ("R", "Q"):
            self.at += 1
            if self.take("L"):
                self.base62()
            self.type()
        elif tag == "T":
            self.at += 1
            while not self.take("E"):
                self.type()
        elif tag == "F":
            self.at += 1
            if self.take("G"):
                self.base62()
            self.take("U")
            if self.take("K") and not self.take("C"):
                self.ident()
            while not self.take("E"):
                self.type()
            self.type()
        elif tag == "D":
            self.at += 1
            if self.take("G"):
                self.base62()
            while not self.take("E"):
                self.path()
                while self.take("p"):
                    self.ident()
                    self.type()
            if self.take("L"):
                self.base62()
        elif tag == "B":
            self.at += 1
            self.base62()
        else:
            self.path()


def rust_code_name(mangled: str) -> str:
    """@p mangled (Rust v0) without the crate that instantiated it, if it names one, or as it is when it does not
    parse."""
    if not mangled.startswith("_R"):
        return mangled
    reader = RustPath(mangled)
    reader.at = 2
    try:
        while reader.peek().isdigit():  # an encoding version
            reader.at += 1
        reader.path()
    except (ValueError, IndexError):
        return mangled
    return (
        mangled[: reader.at]
        if reader.at < len(mangled) and mangled[reader.at] in "CB"
        else mangled
    )


# A Rust v0 crate root's disambiguator (`Cs6NFbV2PXKjr_` before `15deluge_bsp_rust`).
RUST_CRATE_HASH = re.compile(r"Cs[0-9a-zA-Z]+_(?=\d)")
# A Rust function's name as rustc and LLVM uniquify it in an image: `name.N`, N counted across the image.
UNIQUIFIED = re.compile(r"^(_R[^.]*\.)\d+$")


def uniquified(nm_text: str) -> set[str]:
    """The Rust functions (code symbols, `t`/`T`) of @p nm_text named `name.N`. The number tells nothing about which
    function it is (a change elsewhere renumbers it), so every such name is read as `name.0`, and two of them are
    twins, told apart by their places. The symbol LLVM left unsuffixed stays `name`: it often sits beside a copy, where
    a shared anchor could not tell the two apart. A data symbol's `.N` (a global split into its parts) and other
    suffixes (`.constprop.0`) are not uniquifiers and keep their text."""
    found = set()
    for line in nm_text.splitlines():
        fields = line.split()
        if (
            len(fields) >= 3
            and fields[-2] in ("t", "T")
            and UNIQUIFIED.match(fields[-1])
        ):
            found.add(fields[-1])
    return found


def stem(name: str, copies: set[str]) -> str:
    """@p name without its ThinLTO suffix and, when it is one of @p copies (uniquified()), its `.N` read as `.0`."""
    name = LTO_SUFFIX.sub("", name)
    return UNIQUIFIED.match(name).group(1) + "0" if name in copies else name


def twin_places(nm_text: str, sections=()) -> dict[str, dict[int, tuple[str, int]]]:
    """For each name that several symbols of an image share once `.llvm.<hash>` and a uniquifier's `.N` are dropped,
    each copy's place by its address: the nearest symbol before it in its section that is no twin (its anchor, `^`
    when none), and its ordinal among the copies behind that anchor."""
    copies = uniquified(nm_text)
    symbols = [
        (a & ~1, size, stem(n, copies))
        for a, size, n in nm_symbols(nm_text)
        if not n.startswith("$")
    ]
    starts: dict[str, set[int]] = {}
    for address, _, name in symbols:
        starts.setdefault(name, set()).add(address)
    twins = {name for name, addresses in starts.items() if len(addresses) > 1}
    if not twins:
        return {}

    def section(address: int) -> str:
        return next(
            (name for start, end, name in sections if start <= address < end), ""
        )

    places: dict[str, dict[int, tuple[str, int]]] = {}
    counts: dict[tuple[str, str], int] = {}
    anchor, anchored_in = "^", None
    for address, _, name in sorted(symbols, key=lambda s: (s[0], s[2] in twins, s[2])):
        if name not in twins:
            if not name.startswith("."):
                anchor, anchored_in = name, section(address)
            continue
        if address in places.get(name, {}):
            continue
        here = anchor if anchored_in == section(address) else "^"
        ordinal = counts.get((name, here), 0)
        counts[(name, here)] = ordinal + 1
        places.setdefault(name, {})[address] = (here, ordinal)
    return places


def number_twins(
    nm_text: str,
    disassembly: str,
    sections=(),
    moved=frozenset(),
    mapping: str = "",
    side: str = "!",
) -> tuple[str, str]:
    """@p nm_text (an `nm` listing) and @p disassembly with each copy of a twin name (twin_places()) named
    `name#N~anchor` by its place. A copy pairs with the other image's copy in the same place, so a reference moved from
    one copy to another is a change, provided each copy has an anchor of its own. Two cases do not pair at all:
    - a name in @p moved, whose places differ from the other image's, is written `#?N~anchor`;
    - a name that is no local label (`.…`, `GCC_except_table…`) and has a copy behind no anchor (`^`) or behind an
      anchor with another of its copies is written with @p side (`!` in the base, `?` in the new image), because its
      place does not tell the copies apart; it shows as removed and added, and its referrers as changed.
    A disassembly's name is placed by its address (less its `+0x…` offset); an `adr`'s by Align(pc, 4) + its
    immediate, pc being the instruction's address + 4 in Thumb and + 8 in ARM, as the `$a`/`$t` mapping symbols in
    @p mapping (`nm --special-syms`) or @p nm_text mark the code. An `adr` that lands in no copy of the twin it names is
    an error."""
    places = twin_places(nm_text, sections)
    copies = uniquified(nm_text)
    if not places and not copies:
        return nm_text, disassembly
    unpairable = {
        name
        for name, found in places.items()
        if not name.startswith((".", "GCC_except_table"))
        and any(anchor == "^" or ordinal for anchor, ordinal in found.values())
    }
    extents: dict[str, list[tuple[int, int]]] = {}
    modes = sorted(
        (a, n[1])
        for a, _, n in nm_symbols(mapping + "\n" + nm_text)
        if n.split(".")[0] in ("$a", "$t")
    )
    for address, size, name in nm_symbols(nm_text):
        plain = stem(name, copies)
        if plain in places:
            extents.setdefault(plain, []).append((address & ~1, max(size, 1)))

    def numbered(name: str, address: int) -> str:
        plain = stem(name, copies)
        place = places.get(plain, {}).get(address & ~1)
        if place is None:
            return plain if LTO_SUFFIX.sub("", name) in copies else name
        mark = "?" if plain in moved else side if plain in unpairable else ""
        return f"{plain}#{mark}{place[1]}~{place[0]}"

    def adr(m: re.Match) -> str:
        plain = stem(m.group(4), copies)
        if plain not in places:
            return f"{m.group(1)}<{numbered(m.group(4), 0)}{m.group(5)}>"
        pc = int(m.group(2), 16)
        mode = next((kind for start, kind in reversed(modes) if start <= pc), "t")
        target = ((pc + (8 if mode == "a" else 4)) & ~3) + int(m.group(3), 0)
        start = next((a for a, size in extents[plain] if a <= target < a + size), None)
        if start is None:
            raise ValueError(
                f"adr lands in no copy of {m.group(4)}: {m.group(0).strip()}"
            )
        return f"{m.group(1)}<{numbered(m.group(4), start)}{m.group(5)}>"

    lines = []
    for line in nm_text.splitlines():
        fields = line.split()
        if len(fields) >= 3 and re.fullmatch(r"[0-9a-f]+", fields[0]):
            line = line[: line.rindex(fields[-1])] + numbered(
                fields[-1], int(fields[0], 16)
            )
        lines.append(line)
    disassembly = TWIN_ADR.sub(adr, disassembly)
    disassembly = TWIN_HEADER.sub(
        lambda m: f"{m.group(1)} <{numbered(m.group(2), int(m.group(1), 16))}>:",
        disassembly,
    )
    disassembly = TWIN_TARGET.sub(
        lambda m: (
            f"0x{m.group(1)}{m.group(2)}<"
            f"{numbered(m.group(3), int(m.group(1), 16) - int(m.group(4)[3:] or '0', 16))}{m.group(4)}>"
        ),
        disassembly,
    )
    return "\n".join(lines) + ("\n" if nm_text.endswith("\n") else ""), disassembly


def moved_twins(
    base_nm: str, new_nm: str, base_sections=(), new_sections=(), rename=keep
) -> frozenset[str]:
    """The twin names of the new image whose places (twin_places(), anchors through @p rename) differ from the base
    image's: their copies cannot be paired by place."""

    def places(nm_text, sections):
        return {
            rename(name): sorted(
                (rename(anchor), ordinal) for anchor, ordinal in found.values()
            )
            for name, found in twin_places(nm_text, sections).items()
        }, {rename(name): name for name in twin_places(nm_text, sections)}

    base, _ = places(base_nm, base_sections)
    new, spelt = places(new_nm, new_sections)
    return frozenset(
        spelt[name] for name in new if name in base and base[name] != new[name]
    )


def place_names(
    cxxfilt: str, nm_texts: list[str], sections: list, renames=keep, mangled=keep
):
    """A rename for moved_twins(): each twin name and anchor of @p nm_texts by its renamed canonical() demangled name
    (or, unreadable, its renamed mangled text), so that a place survives what demangling hides, such as a Rust crate's
    hash, as the twin's key does."""
    names = sorted(
        {
            n
            for nm_text, found in zip(nm_texts, sections)
            for name, copies in twin_places(nm_text, found).items()
            for n in (name, *(anchor for anchor, _ in copies.values()))
        }
    )
    plain = run([cxxfilt], "\n".join(names)).splitlines() if names else []
    table = {
        n: mangled(n) if p == n else canonical(renames(p)) for n, p in zip(names, plain)
    }
    return lambda name: table.get(name, name)


def demangler(
    cxxfilt: str,
    texts: list[str],
    renames=None,
    mangled=None,
    images=None,
    gnu_cxxfilt=None,
):
    """A rename that maps every mangled symbol in @p texts to its key: its canonical() demangled name, with @p renames
    (see namespace_renamer()) applied first. A name that wraps a mangled one (`__const._ZN…sources`, a linker veneer,
    a static guard's destructor, a split global's `.0`, a clone's `.constprop.0`) is renamed through the mangled part
    and keeps its prefix and suffix. A veneer and a bare suffix keep the canonical name; any other wrapped name names
    data and keeps the whole demangled text, as a local does.

    A key must name one symbol. Wherever several symbols of one image (@p images holds each image's own texts) reach
    one key, which is what overloads, constructor and destructor variants, a local's discriminated twins, thunks for
    several bases and Rust instantiations do, each of them is keyed by its mangled name instead, through @p mangled
    (mangled_renamer()); so is a name the demangler cannot read. If two mangled keys of one image are still equal (the
    renamer reduces substitution references), both keep their raw names."""
    names = sorted(symbol_names(texts))
    twins = {raw: TWIN.search(raw) for raw in names}
    stems = {raw: raw[: t.start()] if t else raw for raw, t in twins.items()}
    split = [WRAPPED.match(stems[n]) for n in names]
    targets = [m.group(2) if m else stems[n] for m, n in zip(split, names)]
    demangled = run([cxxfilt], "\n".join(targets)).splitlines()
    # What llvm-cxxfilt cannot read (some lambdas, transaction clones), GNU c++filt can.
    unread = [
        i
        for i, (t, d) in enumerate(zip(targets, demangled))
        if t == d and t.startswith("_Z")
    ]
    gnu_read = set()
    if gnu_cxxfilt and unread:
        for i, text in zip(
            unread,
            run([gnu_cxxfilt], "\n".join(targets[i] for i in unread)).splitlines(),
        ):
            if text != targets[i]:
                demangled[i] = text
                gnu_read.add(names[i])
    by_mangled = {}
    by_crate = {}
    table = {}
    for raw, m, target, plain in zip(names, split, targets, demangled):
        if not m:
            # A Rust crate's hash follows its metadata, so a moved crate changes it and every hash that depends on
            # it; the crate's name stays. The crate that instantiated a generic is no part of its code (LTO keeps
            # any one crate's copy), so it is dropped first and kept only to tell two instances of one image apart.
            rust = stems[raw].startswith("_R")
            by_mangled[raw] = (
                RUST_CRATE_HASH.sub("Cs_", rust_code_name(stems[raw]))
                if rust
                else stems[raw]
            )
            by_crate[raw] = (
                RUST_CRATE_HASH.sub("Cs_", stems[raw]) if rust else stems[raw]
            )
        else:
            by_mangled[raw] = m.group(1) + (mangled or keep)(target) + m.group(3)
        if plain == target:
            # Read by neither demangler: only its exact text names it, so a rename never pairs it with another.
            by_mangled[raw] = table[raw] = stems[raw]
            continue
        if renames:
            plain = renames(plain)
        if raw in gnu_read:
            table[raw] = by_mangled[raw] = by_mangled[raw] + " | " + plain
            continue
        if m:
            # The reduced mangled text alone can make two entities alike across images; their full text cannot.
            by_mangled[raw] += " | " + plain
        if not m:
            table[raw] = canonical(plain)
        elif not m.group(1) or m.group(1).endswith("Thunk_"):
            table[raw] = (
                m.group(1) + canonical(plain, target.startswith("_ZZ")) + m.group(3)
            )
        else:
            table[raw] = m.group(1) + plain + m.group(3)
    for raw in names:
        if (
            raw not in by_crate
            or by_mangled[raw] == stems[raw]
            or not stems[raw].startswith("_R")
        ):
            by_crate[raw] = by_mangled[raw]
    own_texts = images if images is not None else [texts]
    owns = [sorted(symbol_names(i)) for i in own_texts]
    places = [addresses(i) for i in own_texts]
    singles = [[raw for raw in own if not twins[raw]] for own in owns]
    final = settle(table, [by_mangled, by_crate], singles, places)
    # A twin's number names its anchor by the anchor's key, so it follows a rename as the anchor does.
    for raw, twin in twins.items():
        if twin:
            number = f"#{twin.group(1)}~{final.get(twin.group(2), twin.group(2))}"
            table[raw] += number
            by_mangled[raw] += number
            by_crate[raw] += number
            final[raw] = table[raw]
    final = settle(final, [by_mangled, by_crate], owns, places)
    return lambda name: final.get(name, name)


def settle(
    keys: dict[str, str],
    fallbacks: list[dict[str, str]],
    owns: list[list[str]],
    places: list[dict[str, int]],
) -> dict[str, str]:
    """@p keys with every key that several symbols of one image (@p owns, at @p places) hold replaced, for those
    symbols, by their next key in @p fallbacks, and after the last by their raw names, which are distinct, so every
    key of an image names one symbol. Names at one address (folded aliases) are one symbol: a reference to either is
    the same, so they may share a key."""
    final = dict(keys)
    level = dict.fromkeys(keys, 0)
    settled = False
    while not settled:
        settled = True
        for own, at in zip(owns, places):
            for group in shared(own, final, at):
                for raw in group:
                    if final[raw] != raw:
                        level[raw] += 1
                        final[raw] = (
                            fallbacks[level[raw] - 1][raw]
                            if level[raw] <= len(fallbacks)
                            else raw
                        )
                        settled = False
    return final


def shared(names: list[str], key: dict[str, str], at=None) -> list[list[str]]:
    """The groups of @p names that one @p key holds for several symbols: names that @p at (name -> address) places at
    one address are one symbol."""
    holders: dict[str, list[str]] = {}
    for name in names:
        holders.setdefault(key[name], []).append(name)
    at = at or {}
    return [
        group
        for group in holders.values()
        if len({at.get(name, ("unplaced", name)) for name in group}) > 1
    ]


def addresses(texts: list[str]) -> dict[str, int]:
    """The address of each symbol the `nm -S` listings among @p texts define, without ThinLTO suffixes."""
    found = {}
    for text in texts:
        for line in text.splitlines():
            if NM_LINE.match(line):
                fields = line.split()
                found.setdefault(
                    LTO_SUFFIX.sub("", fields[-1]), int(fields[0], 16) & ~1
                )
    return found


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
        "--gnu-cxxfilt",
        default=GNU_CXXFILT,
        help="GNU c++filt, for the names llvm-cxxfilt cannot read (default: the dbt toolchain's)",
    )
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
    pairs = []
    for spec in args.rename:
        old, sep, new = spec.partition("=")
        if not sep or not old or not new:
            parser.error(f"--rename wants OLD=NEW, got {spec!r}")
        pairs.append((old, new))
    # By position, not path: one image given twice is two images.
    read = []
    for which in (args.base, args.new):
        nm = run([args.nm, "-S", "-n", "--defined-only", which])
        sections = sections_of(run([args.readelf, "-SW", which]))
        mapping = run([args.nm, "-n", "--special-syms", "--defined-only", which])
        mapping = "".join(
            line for line in mapping.splitlines(keepends=True) if " $" in line
        )
        read.append((which, nm, sections, mapping))
    places = place_names(
        args.cxxfilt,
        [read[0][1], read[1][1]],
        [read[0][2], read[1][2]],
        namespace_renamer(pairs),
        mangled_renamer(pairs),
    )
    moved = moved_twins(read[0][1], read[1][1], read[0][2], read[1][2], places)
    images = []
    for (which, nm, sections, mapping), twins_moved, side in zip(
        read, (frozenset(), moved), "!?"
    ):
        nm, disassembly = number_twins(
            nm,
            disassemble(args.objdump, which, nm),
            sections,
            twins_moved,
            mapping,
            side,
        )
        images.append((disassembly, nm, run([args.readelf, "-lW", which])))
    rename = demangler(
        args.cxxfilt,
        [text for image in images for text in image[:2]],
        namespace_renamer(pairs),
        mangled_renamer(pairs),
        [list(image[:2]) for image in images],
        shutil.which(args.gnu_cxxfilt),
    )
    base, new = (
        functions(dis, rename, classifier(segments, nm, rename), aliases(nm, rename))
        for dis, nm, segments in images
    )
    changed, added, removed = compare(base, new)
    for kind, names in (("changed", changed), ("added", added), ("removed", removed)):
        print(f"{kind}: {len(names)}")
        for name in names[: args.show]:
            print(f"  {name}")
    for name in args.diff:
        print(f"== {name}")
        before = list(base.get(name, ()))
        after = list(new.get(name, ()))
        for line in difflib.unified_diff(before, after, lineterm="", n=1):
            print(line)
    return 1 if changed or added or removed else 0


if __name__ == "__main__":
    sys.exit(main())
