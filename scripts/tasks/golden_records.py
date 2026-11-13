"""Record headers: C++ headers whose records a run writes.

A record header keeps its generated lines between two marker lines, inside a clang-format off block:

    // clang-format off
    // GOLDEN-RECORDS BEGIN <region>
        ...the lines the run writes...
    // GOLDEN-RECORDS END <region>
    // clang-format on

Under `./dbt test --update <name>`, the spec or scenario that checks a region hands every line of it, in order, to
golden::record() (tests/support/golden_update.h), which logs `GOLDEN-RECORD <header> <region> <line>`. After a run
that passed, the runner calls apply(): each region named is replaced by exactly its logged lines, trailing `//`
comments aligned as clang-format aligns them, and each region that changed is reported as a GOLDEN-UPDATED line. A
region no run wrote is left as it is. A run re-records the records its header lists: to pin a new one, add its line by
hand (any value) and re-record.
"""

import hashlib
from pathlib import Path

import golden_update as gu

TAG = "GOLDEN-RECORD"


def parse(lines) -> dict[tuple[str, str], list[str]]:
    """The record lines in `lines`, by (header, region), in the order logged."""
    regions: dict[tuple[str, str], list[str]] = {}
    for line in lines:
        if line.startswith(TAG + " "):
            _, header, region, text = (line.split(" ", 3) + [""])[:4]
            regions.setdefault((header, region), []).append(text)
    return regions


def align(lines: list[str]) -> list[str]:
    """Each line with code before a trailing ` // ` comment, the code padded to the widest such code in `lines`;
    comment-only lines and lines without a comment as they are."""
    parts = [
        line.split(" // ", 1)
        if " // " in line and not line.lstrip().startswith("//")
        else None
        for line in lines
    ]
    width = max((len(p[0]) for p in parts if p), default=0)
    return [
        line if p is None else f"{p[0]:<{width}} // {p[1]}"
        for line, p in zip(lines, parts)
    ]


def _describe(lines: list[str]) -> str:
    text = "".join(line + "\n" for line in lines)
    return f"lines={len(lines)},sha256={hashlib.sha256(text.encode()).hexdigest()[:16]}"


def rewrite(
    text: str, region: str, lines: list[str], header: str = ""
) -> tuple[str, list[str]]:
    """`text` with `region`'s lines replaced by `lines` (aligned), and the lines the region held. Every byte outside
    the region survives, line endings included: the new lines take the ending of the BEGIN marker."""
    src = text.split("\n")
    stripped = [line.rstrip("\r") for line in src]
    begin, end = f"// GOLDEN-RECORDS BEGIN {region}", f"// GOLDEN-RECORDS END {region}"
    if (
        stripped.count(begin) != 1
        or stripped.count(end) != 1
        or stripped.index(end) < stripped.index(begin)
    ):
        raise ValueError(
            f"{header} region {region}: the header needs one '{begin}' line, then one '{end}'"
        )
    i, j = stripped.index(begin), stripped.index(end)
    cr = "\r" if src[i].endswith("\r") else ""
    old = stripped[i + 1 : j]
    return "\n".join(src[: i + 1] + [line + cr for line in align(lines)] + src[j:]), old


def apply(root: Path, lines) -> list[str]:
    """Rewrite every region `lines` carry record lines for; a GOLDEN-UPDATED line per region that changed."""
    updated = []
    by_header: dict[str, list[tuple[str, list[str]]]] = {}
    for (header, region), records in parse(lines).items():
        by_header.setdefault(header, []).append((region, records))
    for header, regions in by_header.items():
        path = root / header
        text = path.read_bytes().decode()
        new = text
        for region, records in regions:
            new, old = rewrite(new, region, records, header)
            if old != align(records):
                updated.append(
                    f"GOLDEN-UPDATED {header}:{region} {_describe(old)} -> {_describe(align(records))}"
                )
        if new != text:
            gu.atomic_write(path, new.encode())
    return updated
