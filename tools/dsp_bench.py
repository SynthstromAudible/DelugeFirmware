#!/usr/bin/env python3
"""Flash the DSP bench build, capture its BENCH lines over RTT and tabulate them.

Usage:
    tools/dsp_bench.py [--elf build/RelWithDebInfo/deluge.elf] [--timeout 120]
                       [--out .superpowers/estate/bench] [--from-capture FILE]
                       [--probe jlink|probe-rs]

The ELF is built with `./dbt configure -DDELUGE_DSP_BENCH=ON`. By default the script runs
deluge_jlink.py (or deluge_run.py for --probe probe-rs) until the device's `bench: parked` line,
failing on a panic or a `BENCH-ERROR` line, and reads the captured RTT lines from its --json result.
--from-capture parses a saved capture (one RTT line per line) instead, for re-tabulating.

A bench image never starts the app. After `BENCH-DONE` it turns off the pads, button LEDs, gold-knob
rings and OLED, which keep their state across a CPU reset, then prints
`bench: parked, pads/LEDs/OLED off` and parks. The script waits for that line before the probe
detaches, so the device it leaves running is dark. A complete run without it is still written, with a
warning that the device may still be lit and the last line it printed; "parked" in the JSON meta
records which.

The device prints one line per measurement:
    BENCH case=<name> n=<block> [lanes=<N>] k=<blocks> cycles=<u32> cps=<x.xx> l1d_refill=<u32> inst=<u32>
    dc_stall=<u32> issue_empty=<u32> neon=<u32>
A case of N lanes (`lanes=` present, N > 1) renders blocks of n frames, N samples each. Its cps is per
channel-sample, cycles / (k*n*N), as the device prints it; the table divides its events by N too, and
shows its n as `<n>x<N>`.
and ends with `BENCH-DONE cases=<count>`, where count covers every BENCH line before it, the
per-size `calibration` lines included. Before the cases the device prints metadata lines,
`BENCH-INFO <key>=<value>` (today `dbgauthstatus=0x...`, the Cortex-A9 DBGAUTHSTATUS register); they
are not measurements, are not counted by BENCH-DONE and are recorded under "info" in the JSON meta.
Other RTT lines (the boot log, and the plain `bench: ...` marker lines the device prints, the parked
line apart) are ignored. The run is complete only when BENCH-DONE is present, its count equals the parsed lines and no
line is malformed or a BENCH-ERROR.

A complete run is then checked for sanity:
  * `calibration_loop` is present at every block size n in the run, with `inst` in
    [1024*n, 1024*n + 2048] and `cps` at least 32.00 (the arithmetic is in cases.h);
  * `BENCH-INFO dbgauthstatus` is present with SNE, bit 6, set: without it the event counters do not
    count in Secure state.
A failure means the counters, not the components, are at fault.

Output, in --out: <YYYY-MM-DD>-<git short sha>.md (a Markdown table: case, n, cps, cycles and each
event per sample, to two decimals) and .json (the records plus the run's metadata, its "sanity"
result among it: {"passed": bool, "failures": [...]}). A run that fails the sanity check is still
written, with a `**SANITY CHECK FAILED**` banner line above the table. Re-running on the same sha
overwrites. Nothing is written for an incomplete run.

Exit codes:
    0  success
    2  incomplete or malformed capture (nothing written)
    3  the probe script failed (its own exit code is in the message)
    4  the run is complete but fails the sanity check (the marked table is written)
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
INFO = re.compile(r"^BENCH-INFO (\w+)=(\S+)\s*$")
PARKED = "^bench: parked"
CALIBRATION_LOOP = "calibration_loop"
# calibration_loop renames 4 instructions * 4 iterations per sample in each block, plus under 32
# instructions of per-block constant: inst lies in [16*k*n, 16*k*n + 32*k], [1024*n, 1024*n + 2048] at
# the device's k = 64.
LOOP_INST_PER_BLOCK_SAMPLE = 16
LOOP_INST_SLACK_PER_BLOCK = 32
LOOP_MIN_CPS_HUNDREDTHS = 3200
DBGAUTHSTATUS_SNE = 1 << 6
EXIT_INCOMPLETE = 2
EXIT_PROBE = 3
EXIT_INSANE = 4


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
    lanes: int = 1


class Capture(NamedTuple):
    """What a stream of RTT lines contained."""

    records: list[Record]
    done_count: int | None
    errors: list[str]
    info: dict[str, str]
    parked: bool


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
    lanes = 1
    if "lanes" in pairs:
        try:
            lanes = int(pairs["lanes"])
        except ValueError as e:
            raise ValueError(f"non-integer field ({e})") from e
        if lanes < 1:
            raise ValueError(f"lanes {lanes} is below 1")
    return Record(case=pairs["case"], cps=pairs["cps"], lanes=lanes, **numbers)


def parse_capture(lines: list[str]) -> Capture:
    """Pick the BENCH lines out of a capture, and whether the device parked after them.

    Lines after BENCH-DONE are not measurements; only the parked line is looked for there.
    """
    records: list[Record] = []
    errors: list[str] = []
    info: dict[str, str] = {}
    done_count = None
    parked = False
    for number, raw in enumerate(lines, start=1):
        line = raw.strip()
        if done_count is not None:
            if re.match(PARKED, line):
                parked = True
                break
            continue
        if line.startswith("BENCH-ERROR"):
            errors.append(f"line {number}: {line}")
        elif m := DONE.match(line):
            done_count = int(m.group(1))
        elif m := INFO.match(line):
            info[m.group(1)] = m.group(2)
        elif line.startswith(("BENCH-INFO", "BENCH-DONE")):
            errors.append(f"line {number}: malformed {line!r}")
        elif line.startswith("BENCH "):
            try:
                records.append(parse_line(line))
            except ValueError as e:
                errors.append(f"line {number}: malformed BENCH line: {e}")
    return Capture(records, done_count, errors, info, parked)


def last_line(lines: list[str]) -> str | None:
    """The capture's last non-blank line, or None for an empty capture.

    For a run that did not park it says where the device stopped: a boot step after BENCH-DONE
    means its boot stalled before the park task ran, while BENCH-DONE itself means nothing
    reached the probe after it.
    """
    return next((line.strip() for line in reversed(lines) if line.strip()), None)


def per_sample(count: int, k: int, n: int, lanes: int = 1) -> str:
    """count / (k*n*lanes) to two decimals, rounded half up, in integers; 0.00 for no samples."""
    samples = k * n * lanes
    if samples == 0:
        return "0.00"
    hundredths = (100 * count + samples // 2) // samples
    return f"{hundredths // 100}.{hundredths % 100:02d}"


def render_table(records: list[Record]) -> str:
    """The Markdown table: events are per channel-sample, and a case of several lanes shows n as <n>x<lanes>."""
    head = ["case", "n", "cps", "cycles", *(f"{e}/sample" for e in EVENTS)]
    out = [
        "| " + " | ".join(head) + " |",
        "| --- | ---: | ---: | ---: | " + " | ".join(["---:"] * len(EVENTS)) + " |",
    ]
    for r in records:
        n = str(r.n) if r.lanes == 1 else f"{r.n}x{r.lanes}"
        cells = [r.case, n, r.cps, str(r.cycles)]
        cells += [per_sample(getattr(r, e), r.k, r.n, r.lanes) for e in EVENTS]
        out.append("| " + " | ".join(cells) + " |")
    return "\n".join(out) + "\n"


def problems(cap: Capture) -> list[str]:
    """Why the capture is not a complete run; empty when it is."""
    found = list(cap.errors)
    if cap.done_count is None:
        found.append("no BENCH-DONE line: the run did not finish")
    elif cap.done_count != len(cap.records) + len(cap.errors):
        parsed = len(cap.records) + len(cap.errors)
        found.append(
            f"BENCH-DONE counts {cap.done_count} lines but {len(cap.records)} BENCH lines"
            f" and {len(cap.errors)} errors make {parsed}"
        )
    return found


def cps_hundredths(cps: str) -> int:
    """A two-decimal cps text as hundredths."""
    whole, _, fraction = cps.partition(".")
    return int(whole) * 100 + int(fraction)


def sanity(cap: Capture) -> list[str]:
    """Why a complete capture's counters are not to be trusted; empty when they are."""
    found = []
    for n in sorted({r.n for r in cap.records}):
        loops = [r for r in cap.records if r.case == CALIBRATION_LOOP and r.n == n]
        if not loops:
            found.append(f"no {CALIBRATION_LOOP} line at n={n}")
        for r in loops:
            low = LOOP_INST_PER_BLOCK_SAMPLE * r.k * n
            high = low + LOOP_INST_SLACK_PER_BLOCK * r.k
            if not low <= r.inst <= high:
                found.append(
                    f"{CALIBRATION_LOOP} n={n}: inst={r.inst} is outside [{low}, {high}]"
                )
            if cps_hundredths(r.cps) < LOOP_MIN_CPS_HUNDREDTHS:
                found.append(f"{CALIBRATION_LOOP} n={n}: cps={r.cps} is below 32.00")
    status = cap.info.get("dbgauthstatus")
    if status is None:
        found.append("no BENCH-INFO dbgauthstatus line")
    else:
        try:
            value = int(status, 16)
        except ValueError:
            found.append(f"dbgauthstatus={status} is not a hex number")
        else:
            if not value & DBGAUTHSTATUS_SNE:
                found.append(
                    f"dbgauthstatus={status}: SNE (bit 6) is clear, so the event"
                    " counters do not count in Secure state"
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
        PARKED,
        "--fail-on",
        "panicked at",
        "--fail-on",
        "^BENCH-ERROR",
        "--json",
        "--quiet",
        "--timeout",
        str(args.timeout),
    ]
    if args.select:
        cmd += ["--select", args.select]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    try:
        rtt = list(json.loads(proc.stdout).get("rtt", []))
    except (json.JSONDecodeError, AttributeError):
        rtt = []
        if proc.returncode in (0, 1, 2):
            print("dsp_bench: the probe script printed no JSON result", file=sys.stderr)
            return EXIT_PROBE, rtt
    if proc.returncode not in (0, 1, 2):
        print(
            f"dsp_bench: probe failed ({proc.returncode}): {proc.stderr.strip()}",
            file=sys.stderr,
        )
        return EXIT_PROBE, rtt
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
    ap.add_argument(
        "--select",
        default=None,
        metavar="SEL",
        help="passed to the probe script's --select, e.g. IP=pi-top for a remote J-Link",
    )
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
        return EXIT_INCOMPLETE

    failures = sanity(cap)
    meta = {
        "sha": git_sha(),
        "date": today(),
        "probe": probe,
        "elf": args.elf,
        "elf_mtime": elf_stamp(args.elf),
        "info": cap.info,
        "parked": cap.parked,
        "sanity": {"passed": not failures, "failures": failures},
    }
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    stem = out / f"{meta['date']}-{meta['sha']}"
    table = render_table(cap.records)
    if failures:
        table = f"**SANITY CHECK FAILED**: {'; '.join(failures)}\n\n{table}"
    stem.with_suffix(".md").write_text(table)
    doc = {"meta": meta, "records": [r._asdict() for r in cap.records]}
    stem.with_suffix(".json").write_text(json.dumps(doc, indent=2) + "\n")
    print(table, end="")
    print(f"dsp_bench: wrote {stem}.md and .json", file=sys.stderr)
    if not cap.parked:
        print(
            "dsp_bench: warning: no `bench: parked` line after BENCH-DONE; "
            "the pads, LEDs and OLED may still be lit",
            file=sys.stderr,
        )
        print(f"dsp_bench: the device's last line: {last_line(rtt)!r}", file=sys.stderr)
    if failures:
        print("dsp_bench: the run fails the sanity check:", file=sys.stderr)
        for f in failures:
            print(f"  {f}", file=sys.stderr)
        return EXIT_INSANE
    return 0


if __name__ == "__main__":
    sys.exit(main())
