#!/usr/bin/env python3
"""Run the streaming benchmark build, capture its STREAM-BENCH lines over RTT and tabulate them.

Usage:
    tools/stream_bench.py [--elf build/RelWithDebInfo/deluge.elf] [--timeout 180]
                          [--out .superpowers/estate/bench] [--from-capture FILE]
                          [--select IP=pi-top]

The ELF is built with `./dbt configure -DDELUGE_STREAM_BENCH=ON` (and the songs it plays with
-DDELUGE_STREAM_BENCH_SONG_A=... / _B=..., paths as on the card). The image boots the app, loads the
first song, and measures three phases: `idle` (loaded, stopped), `steady` (playing) and
`load-while-playing` (the second song loaded while the first plays). src/deluge/bench/stream_bench.h
documents the lines; this script runs deluge_jlink.py until `STREAM-BENCH-DONE`, failing on a panic,
and reads the captured RTT lines from its --json result. --from-capture parses a saved capture (one
RTT line per line) instead.

Each histogram is log2: bucket 0 holds 0, bucket i holds [2^(i-1), 2^i) of its unit. The table gives
each metric's count, mean and max as the device measured them, and p50/p90/p99 as the upper edge of
the bucket the percentile falls in (or the max, if that is less), all in ms (frames are converted at
44.1 kHz).

Output, in --out: stream-<YYYY-MM-DD>-<git short sha>.md (the tables) and .json (the parsed records
and the run's metadata), and the raw capture as .rtt, kept even for an incomplete run so it can be
re-read with --from-capture. Re-running on the same sha overwrites. No tables are written for a run
without `STREAM-BENCH-DONE`.

Exit codes:
    0  success
    2  incomplete or malformed capture (nothing written)
    3  the probe script failed (its own exit code is in the message)
    4  the run completed but reported failures (STREAM-BENCH-FAIL lines; the tables are written)
"""

import argparse
import datetime
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TOOLS = REPO / "tools"

EXIT_INCOMPLETE = 2
EXIT_PROBE = 3
EXIT_FAILED = 4

SAMPLE_RATE = 44_100
PHASES = ("idle", "steady", "load-while-playing")
METRICS = (
    "queue_wait",
    "fill",
    "wake",
    "pop",
    "begin",
    "fs_wait",
    "read",
    "finish",
    "gap",
)
TIERS = ("interaction", "storage", "audio")

# Lines may carry the logger's own prefix (file:line), so each pattern searches rather than matches.
PHASE_RE = re.compile(
    r"STREAM-BENCH-PHASE phase=(?P<phase>\S+) ms=(?P<ms>\d+) underrun_wait=(?P<wait>\d+) "
    r"underrun_unassign=(?P<unassign>\d+) depth_mean_x100=(?P<depth_mean>\d+) depth_max=(?P<depth_max>\d+)"
)
HIST_RE = re.compile(
    r"STREAM-BENCH-HIST phase=(?P<phase>\S+) metric=(?P<metric>\S+) unit=(?P<unit>us|frames) "
    r"n=(?P<n>\d+) mean=(?P<mean>\d+) max=(?P<max>\d+) b=(?P<buckets>[\d,]*)"
)
FS_RE = re.compile(
    r"STREAM-BENCH-FS phase=(?P<phase>\S+) long_waits=(?P<waits>\d+,\d+,\d+) max_us=(?P<max>\d+,\d+,\d+)"
)
JOB_RE = re.compile(
    r'STREAM-BENCH-JOB phase=(?P<phase>\S+) name="(?P<name>[^"]*)" max_us=(?P<max>\d+) runs=(?P<runs>\d+)'
)
LOADED_RE = re.compile(r"STREAM-BENCH-LOADED song=(?P<song>.*?) ok=(?P<ok>[01])")
BEGIN_RE = re.compile(
    r'STREAM-BENCH-BEGIN first="(?P<first>[^"]*)" second="(?P<second>[^"]*)"'
)
FAIL_RE = re.compile(r"STREAM-BENCH-FAIL (?P<what>.*)")
DONE_RE = re.compile(r"STREAM-BENCH-DONE failures=(?P<failures>\d+)")


def to_ms(value: float, unit: str) -> float:
    """A duration in `unit` (us or frames), in ms."""
    if unit == "frames":
        return value * 1000.0 / SAMPLE_RATE
    return value / 1000.0


def bucket_upper(i: int) -> int:
    """The exclusive upper edge of log2 bucket i: 1 for bucket 0, 2^i otherwise."""
    return 1 if i == 0 else 1 << i


def percentile(buckets: list[int], q: float) -> int | None:
    """The upper edge of the bucket holding the q-th quantile, or None for an empty histogram."""
    total = sum(buckets)
    if total == 0:
        return None
    want = q * total
    seen = 0
    for i, count in enumerate(buckets):
        seen += count
        if seen >= want:
            return bucket_upper(i)
    return bucket_upper(len(buckets) - 1)


def parse_capture(lines: list[str]) -> dict:
    """The run's records, keyed by phase, plus its songs, failures and whether it finished."""
    run: dict = {
        "phases": {},
        "loaded": [],
        "failures": [],
        "done": None,
        "songs": None,
    }

    def phase(name: str) -> dict:
        return run["phases"].setdefault(name, {"hist": {}, "jobs": []})

    for line in lines:
        if m := PHASE_RE.search(line):
            p = phase(m["phase"])
            p.update(
                ms=int(m["ms"]),
                underrun_wait=int(m["wait"]),
                underrun_unassign=int(m["unassign"]),
                depth_mean=int(m["depth_mean"]) / 100.0,
                depth_max=int(m["depth_max"]),
            )
        elif m := HIST_RE.search(line):
            buckets = [int(b) for b in m["buckets"].split(",") if b]
            phase(m["phase"])["hist"][m["metric"]] = {
                "unit": m["unit"],
                "n": int(m["n"]),
                "mean": int(m["mean"]),
                "max": int(m["max"]),
                "buckets": buckets,
            }
        elif m := FS_RE.search(line):
            waits = [int(v) for v in m["waits"].split(",")]
            maxes = [int(v) for v in m["max"].split(",")]
            phase(m["phase"])["fs"] = {
                t: {"long_waits": w, "max_us": x}
                for t, w, x in zip(TIERS, waits, maxes)
            }
        elif m := JOB_RE.search(line):
            phase(m["phase"])["jobs"].append(
                {"name": m["name"], "max_us": int(m["max"]), "runs": int(m["runs"])}
            )
        elif m := LOADED_RE.search(line):
            run["loaded"].append({"song": m["song"], "ok": m["ok"] == "1"})
        elif m := BEGIN_RE.search(line):
            run["songs"] = [m["first"], m["second"]]
        elif m := FAIL_RE.search(line):
            run["failures"].append(m["what"].strip())
        elif m := DONE_RE.search(line):
            run["done"] = int(m["failures"])
    return run


def fmt_ms(value: float | None) -> str:
    return "-" if value is None else f"{value:.2f}"


def render(run: dict) -> str:
    """The run as Markdown: a summary per phase, its histograms, filesystem waits and slowest jobs."""
    out: list[str] = []
    if run["songs"]:
        out.append(
            f"Songs: `{run['songs'][0]}`, then `{run['songs'][1]}` while playing.\n"
        )
    for loaded in run["loaded"]:
        out.append(
            f"- loaded `{loaded['song']}` ({'ok' if loaded['ok'] else 'FAILED'})"
        )
    if run["loaded"]:
        out.append("")
    for f in run["failures"]:
        out.append(f"**FAIL**: {f}")
    if run["failures"]:
        out.append("")
    for name in [p for p in PHASES if p in run["phases"]] + [
        p for p in run["phases"] if p not in PHASES
    ]:
        p = run["phases"][name]
        out.append(f"## {name}\n")
        if "ms" in p:
            out.append(
                f"{p['ms'] / 1000:.1f} s; underruns: {p['underrun_wait']} waits, "
                f"{p['underrun_unassign']} dropped voices; loader depth mean {p['depth_mean']:.2f}, "
                f"max {p['depth_max']}\n"
            )
        out.append("| metric | n | mean ms | p50 ms | p90 ms | p99 ms | max ms |")
        out.append("|---|---:|---:|---:|---:|---:|---:|")
        for metric in METRICS:
            h = p["hist"].get(metric)
            if h is None:
                continue
            unit = h["unit"]
            # A bucket's upper edge can pass the longest duration measured, which bounds it.
            pct = [
                None if v is None else min(v, h["max"])
                for v in (percentile(h["buckets"], q) for q in (0.5, 0.9, 0.99))
            ]
            cells = [
                metric,
                str(h["n"]),
                fmt_ms(to_ms(h["mean"], unit) if h["n"] else None),
                *(fmt_ms(None if v is None else to_ms(v, unit)) for v in pct),
                fmt_ms(to_ms(h["max"], unit) if h["n"] else None),
            ]
            out.append("| " + " | ".join(cells) + " |")
        out.append("")
        if fs := p.get("fs"):
            parts = [
                f"{t} {v['long_waits']} (max {v['max_us'] / 1000:.2f} ms)"
                for t, v in fs.items()
            ]
            out.append(
                "Filesystem waits of 2 ms or more, by holder: "
                + ", ".join(parts)
                + "\n"
            )
        if p["jobs"]:
            out.append("| slowest jobs | max ms | runs |")
            out.append("|---|---:|---:|")
            for job in p["jobs"]:
                out.append(
                    f"| {job['name']} | {job['max_us'] / 1000:.2f} | {job['runs']} |"
                )
            out.append("")
    return "\n".join(out) + "\n"


def problems(run: dict) -> list[str]:
    """Why the capture is incomplete, if it is."""
    found = []
    if run["done"] is None:
        found.append("no STREAM-BENCH-DONE line")
    for name in PHASES:
        if (
            run["done"] is not None
            and not run["failures"]
            and name not in run["phases"]
        ):
            found.append(f"no {name} phase")
    return found


def git_sha() -> str:
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
    return datetime.datetime.now(tz=datetime.UTC).astimezone().date().isoformat()


def run_probe(args: argparse.Namespace) -> tuple[int, list[str]]:
    """Run deluge_jlink.py until the benchmark is done; return its exit code and the RTT lines."""
    cmd = [
        sys.executable,
        str(TOOLS / "deluge_jlink.py"),
        args.elf,
        "--until",
        "STREAM-BENCH-DONE",
        "--fail-on",
        "panicked at",
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
        print(f"stream_bench: no JSON result from: {shlex.join(cmd)}", file=sys.stderr)
        return EXIT_PROBE, []
    if proc.returncode not in (0, 1, 2):
        print(
            f"stream_bench: probe failed ({proc.returncode}): {proc.stderr.strip()}",
            file=sys.stderr,
        )
        return EXIT_PROBE, rtt
    return proc.returncode, rtt


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Run the streaming benchmark and tabulate it."
    )
    ap.add_argument("--elf", default="build/RelWithDebInfo/deluge.elf")
    ap.add_argument(
        "--timeout", type=int, default=180, help="seconds before the probe gives up"
    )
    ap.add_argument("--out", default=".superpowers/estate/bench")
    ap.add_argument(
        "--from-capture",
        metavar="FILE",
        help="parse a saved capture instead of running",
    )
    ap.add_argument(
        "--select",
        default=None,
        metavar="SEL",
        help="passed to deluge_jlink.py, e.g. IP=pi-top",
    )
    args = ap.parse_args(argv)

    if args.from_capture:
        rtt = Path(args.from_capture).read_text().splitlines()
    else:
        code, rtt = run_probe(args)
        if code == EXIT_PROBE:
            return code

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    stem = out / f"stream-{today()}-{git_sha()}"
    if not args.from_capture:
        stem.with_suffix(".rtt").write_text("\n".join(rtt) + "\n")

    run = parse_capture(rtt)
    found = problems(run)
    if found:
        print("stream_bench: incomplete run, no tables written:", file=sys.stderr)
        for p in found:
            print(f"  {p}", file=sys.stderr)
        if rtt:
            print(f"stream_bench: the device's last line: {rtt[-1]!r}", file=sys.stderr)
        return EXIT_INCOMPLETE

    table = render(run)
    stem.with_suffix(".md").write_text(table)
    meta = {"sha": git_sha(), "date": today(), "elf": args.elf}
    stem.with_suffix(".json").write_text(
        json.dumps({"meta": meta, "run": run}, indent=2) + "\n"
    )
    print(table, end="")
    print(f"stream_bench: wrote {stem}.md and .json", file=sys.stderr)
    if run["failures"] or run["done"]:
        return EXIT_FAILED
    return 0


if __name__ == "__main__":
    sys.exit(main())
