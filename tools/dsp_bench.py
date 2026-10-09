#!/usr/bin/env python3
"""Flash the DSP bench build, capture its BENCH lines over RTT and tabulate them.

Usage:
    tools/dsp_bench.py [--elf build/RelWithDebInfo/deluge.elf] [--timeout 120]
                       [--out .superpowers/estate/bench] [--from-capture FILE]
                       [--probe jlink|probe-rs]

The ELF is built with `./dbt configure -DDELUGE_DSP_BENCH=ON`. By default the script runs
deluge_jlink.py (or deluge_run.py for --probe probe-rs) until `BENCH-DONE`, failing on a panic or a
`BENCH-ERROR` line, and reads the captured RTT lines from its --json result. --from-capture parses a
saved capture (one RTT line per line) instead, for re-tabulating.

The device prints one line per measurement:
    BENCH case=<name> n=<block> k=<blocks> cycles=<u64> cps=<x.xx> l1d_refill=<u32> inst=<u32>
    dc_stall=<u32> issue_empty=<u32> neon=<u32>
and ends with `BENCH-DONE cases=<count>`, where count covers every BENCH line before it, the
per-size `calibration` lines included. Other RTT lines are ignored. The run is complete only when
BENCH-DONE is present, its count equals the parsed lines and no line is malformed or a BENCH-ERROR.

Output, in --out: <YYYY-MM-DD>-<git short sha>.md (a Markdown table: case, n, cps, cycles and each
event per sample, to two decimals) and .json (the records plus the run's metadata). Re-running on
the same sha overwrites. Nothing is written for an incomplete run.

Exit codes: 0 success; 2 incomplete or malformed capture; the probe script's own code (3 probe
error, 4 usage) is passed through.
"""

import argparse
import datetime
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
PROBES = {"jlink": "deluge_jlink.py", "probe-rs": "deluge_run.py"}
EVENTS = ("l1d_refill", "inst", "dc_stall", "issue_empty", "neon")
FIELDS = ("case", "n", "k", "cycles", "cps", *EVENTS)
INT_FIELDS = ("n", "k", "cycles", *EVENTS)
CPS_FORM = re.compile(r"\d+\.\d\d")
DONE = re.compile(r"^BENCH-DONE cases=(\d+)\s*$")


class Record(NamedTuple):
    """One BENCH line. `cps` stays the device's two-decimal text."""

    case: str
    n: int
    k: int
    cycles: int
    cps: str
    l1d_refill: int
    inst: int
    dc_stall: int
    issue_empty: int
    neon: int


class Capture(NamedTuple):
    """What a stream of RTT lines contained."""

    records: list[Record]
    done_count: int | None
    errors: list[str]


def parse_line(line: str) -> Record:
    """Parse one `BENCH case=...` line; raise ValueError naming what is wrong."""
    pairs = {}
    for token in line.split()[1:]:
        key, sep, value = token.partition("=")
        if not sep:
            raise ValueError(f"token {token!r} is not key=value")
        pairs[key] = value
    missing = [f for f in FIELDS if f not in pairs]
    if missing:
        raise ValueError("missing field " + ", ".join(missing))
    if not CPS_FORM.fullmatch(pairs["cps"]):
        raise ValueError(f"cps {pairs['cps']!r} is not x.xx")
    try:
        numbers = {f: int(pairs[f]) for f in INT_FIELDS}
    except ValueError as e:
        raise ValueError(f"non-integer field ({e})") from e
    return Record(case=pairs["case"], cps=pairs["cps"], **numbers)


def parse_capture(lines: list[str]) -> Capture:
    """Pick the BENCH lines out of a capture; everything else is the app's own output."""
    records: list[Record] = []
    errors: list[str] = []
    done_count = None
    for number, raw in enumerate(lines, start=1):
        line = raw.strip()
        if done_count is not None:
            break
        if line.startswith("BENCH-ERROR"):
            errors.append(f"line {number}: {line}")
        elif m := DONE.match(line):
            done_count = int(m.group(1))
        elif line.startswith("BENCH-DONE"):
            errors.append(f"line {number}: malformed {line!r}")
        elif line.startswith("BENCH "):
            try:
                records.append(parse_line(line))
            except ValueError as e:
                errors.append(f"line {number}: malformed BENCH line: {e}")
    return Capture(records, done_count, errors)


def per_sample(count: int, k: int, n: int) -> str:
    """count / (k*n) to two decimals, rounded half up, in integers."""
    samples = k * n
    hundredths = (100 * count + samples // 2) // samples
    return f"{hundredths // 100}.{hundredths % 100:02d}"


def render_table(records: list[Record]) -> str:
    """The Markdown table: events are per sample."""
    head = ["case", "n", "cps", "cycles", *(f"{e}/sample" for e in EVENTS)]
    out = [
        "| " + " | ".join(head) + " |",
        "| --- | ---: | ---: | ---: | " + " | ".join(["---:"] * len(EVENTS)) + " |",
    ]
    for r in records:
        cells = [r.case, str(r.n), r.cps, str(r.cycles)]
        cells += [per_sample(getattr(r, e), r.k, r.n) for e in EVENTS]
        out.append("| " + " | ".join(cells) + " |")
    return "\n".join(out) + "\n"


def problems(cap: Capture) -> list[str]:
    """Why the capture is not a complete run; empty when it is."""
    found = list(cap.errors)
    if cap.done_count is None:
        found.append("no BENCH-DONE line: the run did not finish")
    elif cap.done_count != len(cap.records) + len(cap.errors):
        found.append(
            f"BENCH-DONE says cases={cap.done_count} but {len(cap.records)} BENCH lines parsed"
        )
    return found


def git_sha() -> str:
    """The repository's short HEAD sha."""
    try:
        r = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=True,
        )
        return r.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def today() -> str:
    """Today's date, ISO."""
    return datetime.datetime.now(tz=datetime.UTC).astimezone().date().isoformat()


def elf_stamp(elf: str) -> str | None:
    """The ELF's mtime, ISO, as its identity; None when it is not on disk."""
    try:
        mtime = Path(elf).stat().st_mtime
    except OSError:
        return None
    return datetime.datetime.fromtimestamp(mtime, tz=datetime.UTC).isoformat(
        timespec="seconds"
    )


def run_probe(args: argparse.Namespace) -> tuple[int, list[str]]:
    """Run the probe script; return its exit code and the RTT lines it captured."""
    cmd = [
        sys.executable,
        str(TOOLS / PROBES[args.probe]),
        args.elf,
        "--until",
        "^BENCH-DONE",
        "--fail-on",
        "panicked at",
        "--fail-on",
        "^BENCH-ERROR",
        "--json",
        "--quiet",
        "--timeout",
        str(args.timeout),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    try:
        rtt = list(json.loads(proc.stdout).get("rtt", []))
    except (json.JSONDecodeError, AttributeError):
        rtt = []
        if proc.returncode in (0, 1, 2):
            print("dsp_bench: the probe script printed no JSON result", file=sys.stderr)
            return 3, rtt
    if proc.returncode not in (0, 1, 2):
        print(
            f"dsp_bench: probe failed ({proc.returncode}): {proc.stderr.strip()}",
            file=sys.stderr,
        )
    return proc.returncode, rtt


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Flash the DSP bench build, capture BENCH lines and tabulate them."
    )
    ap.add_argument("--elf", default="build/RelWithDebInfo/deluge.elf")
    ap.add_argument(
        "--timeout", type=int, default=120, help="seconds before the probe gives up"
    )
    ap.add_argument("--out", default=".superpowers/estate/bench")
    ap.add_argument(
        "--from-capture",
        metavar="FILE",
        help="parse a saved capture instead of running",
    )
    ap.add_argument("--probe", choices=sorted(PROBES), default="jlink")
    args = ap.parse_args(argv)

    if args.from_capture:
        rtt = Path(args.from_capture).read_text().splitlines()
        probe = "capture"
    else:
        code, rtt = run_probe(args)
        if code not in (0, 1, 2):
            return code
        probe = args.probe

    cap = parse_capture(rtt)
    found = problems(cap)
    if found:
        print("dsp_bench: incomplete run, nothing written:", file=sys.stderr)
        for p in found:
            print(f"  {p}", file=sys.stderr)
        return 2

    meta = {
        "sha": git_sha(),
        "date": today(),
        "probe": probe,
        "elf": args.elf,
        "elf_mtime": elf_stamp(args.elf),
    }
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    stem = out / f"{meta['date']}-{meta['sha']}"
    table = render_table(cap.records)
    stem.with_suffix(".md").write_text(table)
    doc = {"meta": meta, "records": [r._asdict() for r in cap.records]}
    stem.with_suffix(".json").write_text(json.dumps(doc, indent=2) + "\n")
    print(table, end="")
    print(f"dsp_bench: wrote {stem}.md and .json", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
