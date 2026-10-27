#!/usr/bin/env python3
"""Run several stream-bench images over interleaved rounds on the rig, and compare them.

Usage:
    tools/stream_bench_matrix.py run --image LABEL=ELF [--image LABEL=ELF ...] [--rounds 3]
                                     [--name NAME] [--out DIR] [--host HOST] [--timeout 320]
                                     [--no-park]
    tools/stream_bench_matrix.py summarise DIR [--compare A B] [--match REGEX]

run        For each round, runs every image once, in the order given, so drift on the rig (card
           temperature, the network to the probe host) falls on every image alike. Each run goes
           through tools/stream_bench.py into DIR/<label>-<round>/, after a rig restart; a run the
           probe drops is retried up to twice. DIR defaults to
           .superpowers/estate/bench/matrix-<date>-<sha>-<name>. A manifest.json there records the
           images and rounds. Afterwards the rig is parked and its server stopped (--no-park leaves
           it running).
summarise  Reads every complete run in DIR (its stream-*.json) and prints, per image, each metric's
           mean ± sample standard deviation [min–max] over its runs. Metrics are the phase
           histograms (mean, p99, max, in ms), underruns, each tagged phase line's values, the arm
           time, each load's phases, and the mean of each firmware log line's values. --compare A B
           adds the difference B − A and whether their ranges overlap. --match keeps the metrics
           whose name matches.

Exit codes: run: 0 every run completed, 1 some did not. summarise: 0, or 2 if DIR holds no runs.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rig
import stream_bench

REPO = Path(__file__).resolve().parent.parent
ESTATE = REPO / ".superpowers" / "estate" / "bench"


def flatten(run: dict) -> dict[str, float]:
    """One run's metrics, by name."""
    out: dict[str, float] = {}
    for name, p in run["phases"].items():
        for metric, h in p.get("hist", {}).items():
            if not h["n"]:
                continue
            unit = h["unit"]
            out[f"{name}.{metric}.mean_ms"] = stream_bench.to_ms(h["mean"], unit)
            out[f"{name}.{metric}.max_ms"] = stream_bench.to_ms(h["max"], unit)
            p99 = stream_bench.percentile(h["buckets"], 0.99)
            if p99 is not None:
                out[f"{name}.{metric}.p99_ms"] = stream_bench.to_ms(
                    min(p99, h["max"]), unit
                )
        if "underrun_wait" in p:
            out[f"{name}.underruns"] = p["underrun_wait"] + p["underrun_unassign"]
        for tag, fields in p.get("lines", {}).items():
            for k, v in fields.items():
                out[f"{name}.{tag}.{k}"] = v
        for k, v in p.get("card", {}).items():
            out[f"{name}.card.{k}"] = v
    if run.get("armed_ms") is not None:
        out["armed_ms"] = run["armed_ms"]
    for i, phases in enumerate(run.get("load_phases", []), 1):
        for phase, t in phases.items():
            for k, v in t.items():
                out[f"load{i}.{phase}.{k}"] = v

    def logs(prefix: str, by_tag: dict) -> None:
        for tag, lines in by_tag.items():
            out[f"{prefix}{tag}.count"] = len(lines)
            for k in {k for line in lines for k in line}:
                out[f"{prefix}{tag}.{k}"] = statistics.mean(
                    line[k] for line in lines if k in line
                )

    logs("log.", run.get("logs", {}))
    # And by the load each line fell in, so one load's lines are not averaged with another's.
    for load, by_tag in run.get("load_logs", {}).items():
        logs(f"load{load}.log.", by_tag)
    return out


def stats(values: list[float]) -> dict[str, float]:
    return {
        "n": len(values),
        "mean": statistics.mean(values),
        "sd": statistics.stdev(values) if len(values) > 1 else 0.0,
        "min": min(values),
        "max": max(values),
    }


def load_runs(root: Path) -> dict[str, list[dict[str, float]]]:
    """Each label's runs' metrics; a run directory is <label>-<round>, holding stream-*.json."""
    runs: dict[str, list[dict[str, float]]] = {}
    for run_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        label = run_dir.name.rsplit("-", 1)[0]
        for doc in run_dir.glob("stream-*.json"):
            runs.setdefault(label, []).append(
                flatten(json.loads(doc.read_text())["run"])
            )
    return runs


def fmt(s: dict[str, float] | None) -> str:
    if s is None:
        return "-"
    if s["n"] == 1:
        return f"{s['mean']:.3g} (n=1)"
    return f"{s['mean']:.3g} ± {s['sd']:.2g} [{s['min']:.3g}–{s['max']:.3g}] n={s['n']}"


def summarise(root: Path, compare: tuple[str, str] | None, match: str | None) -> str:
    runs = load_runs(root)
    labels = sorted(runs)
    table: dict[str, dict[str, dict[str, float]]] = {}
    for label in labels:
        names = {k for r in runs[label] for k in r}
        table[label] = {k: stats([r[k] for r in runs[label] if k in r]) for k in names}
    metrics = sorted({k for t in table.values() for k in t})
    if match:
        metrics = [m for m in metrics if re.search(match, m)]
    head = ["metric", *labels]
    if compare:
        head += [f"{compare[1]} − {compare[0]}", "ranges"]
    out = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    for m in metrics:
        cells = [m] + [fmt(table[l].get(m)) for l in labels]
        if compare:
            a, b = (table.get(c, {}).get(m) for c in compare)
            if a and b:
                delta = b["mean"] - a["mean"]
                pct = f" ({100 * delta / a['mean']:+.0f}%)" if a["mean"] else ""
                overlap = a["max"] >= b["min"] and b["max"] >= a["min"]
                cells += [f"{delta:+.3g}{pct}", "overlap" if overlap else "**apart**"]
            else:
                cells += ["-", "-"]
        out.append("| " + " | ".join(cells) + " |")
    return "\n".join(out) + "\n"


def run_matrix(args: argparse.Namespace) -> int:
    images = []
    for spec in args.image:
        label, sep, elf = spec.partition("=")
        if not sep or not label or "-" in label:
            raise SystemExit(
                f"stream_bench_matrix: --image wants LABEL=ELF, LABEL without '-': {spec}"
            )
        images.append((label, elf))
    root = args.out or ESTATE / (
        f"matrix-{stream_bench.today()}-{stream_bench.git_sha()}-{args.name}"
    )
    root.mkdir(parents=True, exist_ok=True)
    (root / "manifest.json").write_text(
        json.dumps(
            {
                "images": dict(images),
                "rounds": args.rounds,
                "sha": stream_bench.git_sha(),
                "date": stream_bench.today(),
            },
            indent=2,
        )
        + "\n"
    )
    incomplete = 0
    try:
        for r in range(1, args.rounds + 1):
            for label, elf in images:
                code = None
                run_dir = root / f"{label}-{r}"
                run_dir.mkdir(exist_ok=True)
                log = []
                for attempt in range(1, 4):
                    if not rig.restart(args.host):
                        log.append(f"attempt {attempt}: the rig did not restart")
                        continue
                    proc = subprocess.run(
                        [
                            sys.executable,
                            str(REPO / "tools" / "stream_bench.py"),
                            "--elf",
                            elf,
                            "--select",
                            f"IP={rig.probe_address(args.host)}",
                            "--timeout",
                            str(args.timeout),
                            "--out",
                            str(run_dir),
                        ],
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    code = proc.returncode
                    log.append(f"attempt {attempt}: exit {code}\n{proc.stderr}")
                    if code in (0, stream_bench.EXIT_FAILED):
                        break
                # Kept beside the run, so a run that failed says why.
                (run_dir / "run.log").write_text("\n".join(log) + "\n")
                incomplete += code not in (0, stream_bench.EXIT_FAILED)
                last = next(
                    (l for l in reversed("\n".join(log).splitlines()) if l.strip()), ""
                )
                print(
                    f"{label} round {r}: exit {code}" + (f" ({last})" if code else ""),
                    file=sys.stderr,
                )
    finally:
        if not args.no_park:
            rig.park(args.host, rig.DEFAULT_PARK)
            rig.stop(args.host)
    print(summarise(root, None, None), end="")
    print(f"stream_bench_matrix: runs in {root}", file=sys.stderr)
    return 1 if incomplete else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Compare stream-bench images over interleaved rounds."
    )
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--image", action="append", required=True, metavar="LABEL=ELF")
    r.add_argument("--rounds", type=int, default=3)
    r.add_argument("--name", default="run")
    r.add_argument("--out", type=Path)
    r.add_argument(
        "--host",
        default=rig.DEFAULT_HOST,
        required=rig.DEFAULT_HOST is None,
        help="ssh destination of the probe's Pi (default $DELUGE_PROBE_HOST)",
    )
    r.add_argument("--timeout", type=int, default=320)
    r.add_argument("--no-park", action="store_true")
    s = sub.add_parser("summarise")
    s.add_argument("dir", type=Path)
    s.add_argument("--compare", nargs=2, metavar=("A", "B"))
    s.add_argument("--match")
    args = ap.parse_args(argv)
    if args.cmd == "run":
        return run_matrix(args)
    if not any(args.dir.glob("*/stream-*.json")):
        print(f"stream_bench_matrix: no runs in {args.dir}", file=sys.stderr)
        return 2
    print(
        summarise(args.dir, tuple(args.compare) if args.compare else None, args.match),
        end="",
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
