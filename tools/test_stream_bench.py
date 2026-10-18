#!/usr/bin/env python3
"""Tests for stream_bench.py's parsing and tables: run with `python3 -m unittest test_stream_bench`."""

import json
import tempfile
import unittest
from pathlib import Path

import stream_bench

PREFIX = "[stream_bench.cpp:1] "


def phase_lines(phase: str) -> list[str]:
    return [
        f"STREAM-BENCH-PHASE phase={phase} ms=30000 underrun_wait=1 underrun_unassign=0 depth_mean_x100=120 depth_max=3",
        f"STREAM-BENCH-HIST phase={phase} metric=queue_wait unit=frames n=4 mean=700 max=1100 b=0,0,0,0,0,0,0,0,0,0,2,2",
        f"STREAM-BENCH-HIST phase={phase} metric=fill unit=frames n=4 mean=255 max=500 b=0,0,0,0,0,0,0,0,0,4",
        f"STREAM-BENCH-HIST phase={phase} metric=wake unit=us n=2 mean=40 max=60 b=0,0,0,0,0,0,2",
        f"STREAM-BENCH-HIST phase={phase} metric=fs_wait unit=us n=0 mean=0 max=0 b=",
        f"STREAM-BENCH-FS phase={phase} long_waits=3,0,0 max_us=40000,0,0",
        f'STREAM-BENCH-JOB phase={phase} name="song load" max_us=250000 runs=12',
    ]


def full_run() -> list[str]:
    lines = [
        "boot noise",
        'STREAM-BENCH-BEGIN first="SONGS/A.XML" second="SONGS/Icoustic/Grunnmur 58.XML"',
    ]
    lines.append("STREAM-BENCH-LOADED song=A ok=1")
    for phase in stream_bench.PHASES:
        lines += phase_lines(phase)
    lines.append("STREAM-BENCH-LOADED song=B ok=1")
    lines.append("STREAM-BENCH-DONE failures=0")
    return [PREFIX + line for line in lines]


class ParseTests(unittest.TestCase):
    def test_full_run_parses_every_phase(self) -> None:
        run = stream_bench.parse_capture(full_run())
        self.assertEqual(run["done"], 0)
        self.assertEqual(
            run["songs"], ["SONGS/A.XML", "SONGS/Icoustic/Grunnmur 58.XML"]
        )
        self.assertEqual([l["song"] for l in run["loaded"]], ["A", "B"])
        steady = run["phases"]["steady"]
        self.assertEqual(steady["underrun_wait"], 1)
        self.assertEqual(steady["depth_mean"], 1.2)
        self.assertEqual(steady["hist"]["queue_wait"]["buckets"][10:], [2, 2])
        self.assertEqual(steady["hist"]["fs_wait"]["buckets"], [])
        self.assertEqual(
            steady["fs"]["interaction"], {"long_waits": 3, "max_us": 40000}
        )
        self.assertEqual(
            steady["jobs"], [{"name": "song load", "max_us": 250000, "runs": 12}]
        )
        self.assertEqual(stream_bench.problems(run), [])

    def test_a_run_without_done_is_incomplete(self) -> None:
        run = stream_bench.parse_capture(full_run()[:-1])
        self.assertIn("no STREAM-BENCH-DONE line", stream_bench.problems(run))

    def test_a_missing_phase_is_incomplete(self) -> None:
        lines = [l for l in full_run() if "phase=idle" not in l]
        self.assertIn(
            "no idle phase", stream_bench.problems(stream_bench.parse_capture(lines))
        )

    def test_a_failed_stage_needs_no_phases(self) -> None:
        lines = [
            PREFIX + "STREAM-BENCH-FAIL the first song's load never finished",
            PREFIX + "STREAM-BENCH-DONE failures=1",
        ]
        run = stream_bench.parse_capture(lines)
        self.assertEqual(stream_bench.problems(run), [])
        self.assertEqual(run["failures"], ["the first song's load never finished"])


class PercentileTests(unittest.TestCase):
    def test_percentile_is_the_upper_edge_of_its_bucket(self) -> None:
        # Two samples in [512, 1024), two in [1024, 2048).
        buckets = [0] * 10 + [2, 2]
        self.assertEqual(stream_bench.percentile(buckets, 0.5), 1024)
        self.assertEqual(stream_bench.percentile(buckets, 0.9), 2048)
        self.assertIsNone(stream_bench.percentile([], 0.5))
        self.assertEqual(stream_bench.percentile([3], 0.5), 1)

    def test_frames_convert_at_44100(self) -> None:
        self.assertAlmostEqual(stream_bench.to_ms(44100, "frames"), 1000.0)
        self.assertAlmostEqual(stream_bench.to_ms(1500, "us"), 1.5)


class MainTests(unittest.TestCase):
    def run_capture(self, lines: list[str]) -> tuple[int, Path]:
        tmp = Path(tempfile.mkdtemp())
        capture = tmp / "capture.txt"
        capture.write_text("\n".join(lines) + "\n")
        code = stream_bench.main(
            ["--from-capture", str(capture), "--out", str(tmp / "out")]
        )
        return code, tmp / "out"

    def test_a_complete_run_writes_the_tables(self) -> None:
        code, out = self.run_capture(full_run())
        self.assertEqual(code, 0)
        md = next(out.glob("*.md")).read_text()
        self.assertIn("## steady", md)
        self.assertIn("| queue_wait | 4 | 15.87 | 23.22 | 24.94 | 24.94 | 24.94 |", md)
        self.assertIn("interaction 3 (max 40.00 ms)", md)
        doc = json.loads(next(out.glob("*.json")).read_text())
        self.assertEqual(sorted(doc["run"]["phases"]), sorted(stream_bench.PHASES))

    def test_a_reported_failure_exits_4(self) -> None:
        lines = full_run()[:-1] + [
            PREFIX + "STREAM-BENCH-FAIL playback stopped",
            PREFIX + "STREAM-BENCH-DONE failures=1",
        ]
        code, _ = self.run_capture(lines)
        self.assertEqual(code, stream_bench.EXIT_FAILED)

    def test_an_incomplete_run_exits_2(self) -> None:
        code, out = self.run_capture(full_run()[:-1])
        self.assertEqual(code, stream_bench.EXIT_INCOMPLETE)
        self.assertEqual(list(out.glob("*.md")), [])


if __name__ == "__main__":
    unittest.main()
