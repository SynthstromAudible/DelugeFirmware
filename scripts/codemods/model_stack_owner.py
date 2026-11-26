#!/usr/bin/env python3
"""Rewrite the ModelStack builders' owner argument to a typed owner. Idempotent. Run from the repo root.

    <x>->toModControllable()   ->  deluge::model::owner_of(*<x>)   (with --all, a chained ->effectsOwner() or
                                                                     == nullptr becomes effects_owner_of / is_none)
    <x>.toModControllable()    ->  deluge::model::owner_of(<x>)
    toModControllable()        ->  deluge::model::owner_of(*this)
    nullptr                    ->  deluge::model::LaneOwner{}

only where the expression is a builder's owner argument (BUILDERS names its position). Any other argument is left: a
typed pointer converts to the variant, and the compiler lists the rest. model_stack.h and model_stack.cpp, which
declare the builders, are skipped. With --all, every toModControllable() call is rewritten wherever it is.
"""

import re
import sys
from pathlib import Path

BUILDERS = {
    "addOtherTwoThings": 0,
    "addOtherTwoThingsButNoNoteRow": 0,
    "addModControllable": 0,
    "addModControllableButNoNoteRow": 0,
    "setupModelStackWithModControllable": 3,
    "setupModelStackWithThreeMainThingsButNoNoteRow": 2,
    "setupModelStackWithThreeMainThingsIncludingNoteRow": 5,
}
SKIP = {"src/deluge/model/model_stack.h", "src/deluge/model/model_stack.cpp"}
CALL = re.compile(r"(?<![\w:])(" + "|".join(BUILDERS) + r")\s*\(")
ARROW = re.compile(r"^(?P<x>.+?)->toModControllable\(\)$", re.DOTALL)
DOT = re.compile(r"^(?P<x>.+?)\.toModControllable\(\)$", re.DOTALL)
ANY_CALL = re.compile(
    r"(?P<x>(?:[\w\]\)]+(?:->|\.))*[\w\]\)]+)(?P<op>->|\.)toModControllable\(\)"
    # A bare call, not a declaration or a definition (`ModControllable* toModControllable()`, `Kit::toModControllable()`).
    r"|(?<![\w.>:*])(?<!\* )toModControllable\(\)"
)


def split_args(text, start):
    """The argument spans of the call whose '(' is at text[start - 1]; returns (spans, index of the closing ')')."""
    depth, i, spans, arg_start = 0, start, [], start
    quote = None
    while i < len(text):
        c = text[i]
        if quote:
            if c == "\\":
                i += 2
                continue
            if c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                spans.append((arg_start, i))
                return spans, i
            depth -= 1
        elif c == "," and depth == 0:
            spans.append((arg_start, i))
            arg_start = i + 1
        i += 1
    raise ValueError("unbalanced call")


def typed(arg):
    lead = arg[: len(arg) - len(arg.lstrip())]
    body = arg.strip()
    if body == "nullptr":
        return lead + "deluge::model::LaneOwner{}"
    if body == "toModControllable()":
        return lead + "deluge::model::owner_of(*this)"
    m = ARROW.match(body)
    if m:
        return lead + f"deluge::model::owner_of(*{m.group('x')})"
    m = DOT.match(body)
    if m:
        return lead + f"deluge::model::owner_of({m.group('x')})"
    return arg


def rewrite_builders(text):
    out, pos = [], 0
    for m in CALL.finditer(text):
        if m.start() < pos:
            continue
        index = BUILDERS[m.group(1)]
        spans, _close = split_args(text, m.end())
        if index >= len(spans):
            continue
        a, b = spans[index]
        new = typed(text[a:b])
        if new != text[a:b]:
            out.append(text[pos:a])
            out.append(new)
            pos = b
    out.append(text[pos:])
    return "".join(out)


def rewrite_all(text):
    def one(m):
        if m.group("x") is None:
            return "deluge::model::owner_of(*this)"
        return f"deluge::model::owner_of({'*' if m.group('op') == '->' else ''}{m.group('x')})"

    return typed_reads(ANY_CALL.sub(one, text))


OWNER = r"deluge::model::owner_of\((?P<arg>[^()]*(?:\([^()]*\))?[^()]*)\)"
EFFECTS_READ = re.compile(OWNER + r"->effectsOwner\(\)")
NULL_TEST = re.compile(OWNER + r" (?P<op>==|!=) nullptr")


def typed_reads(text):
    """The reads a rewritten owner is chained into: its effects, and whether it names none."""
    text = EFFECTS_READ.sub(
        r"deluge::model::effects_owner_of(deluge::model::owner_of(\g<arg>))", text
    )
    return NULL_TEST.sub(
        lambda m: (
            f"{'' if m.group('op') == '==' else '!'}deluge::model::is_none(deluge::model::owner_of({m.group('arg')}))"
        ),
        text,
    )


def main(argv):
    every = "--all" in argv
    paths = [p for p in argv if p != "--all"] or ["src", "tests"]
    changed = 0
    for root in paths:
        for path in sorted(Path(root).rglob("*")):
            if path.suffix not in {".h", ".hpp", ".cpp"} or str(path) in SKIP:
                continue
            text = path.read_text()
            new = rewrite_all(text) if every else rewrite_builders(text)
            if new != text:
                path.write_text(new)
                changed += 1
                print(path)
    print(f"model_stack_owner: {changed} file(s) rewritten", file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1:])
