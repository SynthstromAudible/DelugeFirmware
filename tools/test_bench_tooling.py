#!/usr/bin/env python3
"""Tests for stream_bench_matrix.py and bench_image.py: `python3 -m unittest test_bench_tooling`."""

import json
import tempfile
import unittest
from pathlib import Path

import bench_image
import rig
import stream_bench
import stream_bench_matrix

PREFIX = "[stream_bench.cpp:1] "


def capture(fill_mean_us: int, armed_ms: int, data_us: int) -> list[str]:
    """A complete capture whose steady fill mean, arm time and one log value vary."""
    lines = [
        'STREAM-BENCH-BEGIN first="A.XML" second="B.XML"',
        "STREAM-BENCH-LOADED song=A ok=1",
    ]
    for phase in stream_bench.PHASES:
        lines += [
            (
                f"STREAM-BENCH-PHASE phase={phase} ms=30000 underrun_wait=0 underrun_unassign=1 "
                "depth_mean_x100=50 depth_max=2"
            ),
            (
                f"STREAM-BENCH-HIST phase={phase} metric=read unit=us n=4 mean={fill_mean_us} "
                f"max={fill_mean_us + 1000} b=0,0,0,0,0,0,0,0,0,0,0,0,0,4"
            ),
            f"STREAM-BENCH-LOADER phase={phase} requests=10 open_hit_ms=140",
        ]
    lines += [
        "STREAM-BENCH-LOADPHASE phase=begin steps=1 step_ms=650 span_ms=650",
        f"STREAM-BENCH-ARMED ms={armed_ms}",
        f"SD-PHASE cmd_us=19 data_us={data_us} drain_us=2",
        "STREAM-BENCH-LOADED song=B ok=1",
        "STREAM-BENCH-DONE failures=0",
    ]
    return [PREFIX + l for l in lines]


def write_run(root: Path, label: str, r: int, lines: list[str]) -> None:
    run_dir = root / f"{label}-{r}"
    run_dir.mkdir(parents=True)
    doc = {"meta": {}, "run": stream_bench.parse_capture(lines)}
    (run_dir / "stream-x.json").write_text(json.dumps(doc))


class FlattenTests(unittest.TestCase):
    def test_a_run_flattens_to_named_metrics(self) -> None:
        m = stream_bench_matrix.flatten(
            stream_bench.parse_capture(capture(4000, 4046, 4390))
        )
        self.assertAlmostEqual(m["steady.read.mean_ms"], 4.0)
        self.assertEqual(m["steady.underruns"], 1)
        self.assertEqual(m["steady.LOADER.open_hit_ms"], 140)
        self.assertEqual(m["armed_ms"], 4046)
        self.assertEqual(m["load1.begin.step_ms"], 650)
        self.assertEqual(m["log.SD-PHASE.data_us"], 4390)
        self.assertEqual(m["log.SD-PHASE.count"], 1)
        # Logged before song B's LOADED line, after song A's: load 2.
        self.assertEqual(m["load2.log.SD-PHASE.data_us"], 4390)
        self.assertNotIn("load1.log.SD-PHASE.data_us", m)


class SummariseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path(tempfile.mkdtemp())
        for r, (fill, armed) in enumerate(
            [(4000, 5800), (4100, 5700), (3900, 5900)], 1
        ):
            write_run(self.root, "before", r, capture(fill, armed, 4400))
        for r, (fill, armed) in enumerate(
            [(4050, 4000), (3950, 4100), (4000, 4050)], 1
        ):
            write_run(self.root, "after", r, capture(fill, armed, 4000))

    def test_each_label_gets_mean_sd_and_range(self) -> None:
        md = stream_bench_matrix.summarise(self.root, None, "^armed_ms$")
        self.assertIn(
            "| armed_ms | 4.05e+03 ± 50 [4e+03–4.1e+03] n=3 | 5.8e+03 ± 1e+02 [5.7e+03–5.9e+03] n=3 |",
            md,
        )

    def test_compare_marks_ranges_apart_or_overlapping(self) -> None:
        md = stream_bench_matrix.summarise(
            self.root, ("before", "after"), r"^(armed_ms|steady\.read\.mean_ms)$"
        )
        armed = next(l for l in md.splitlines() if l.startswith("| armed_ms"))
        fill = next(l for l in md.splitlines() if l.startswith("| steady.read.mean_ms"))
        self.assertIn("-1.75e+03 (-30%) | **apart** |", armed)
        self.assertIn("| overlap |", fill)

    def test_an_incomplete_run_is_not_counted(self) -> None:
        (self.root / "after-4").mkdir()
        (self.root / "after-4" / "stream-x.rtt").write_text("partial\n")
        md = stream_bench_matrix.summarise(self.root, None, "^armed_ms$")
        self.assertIn("n=3 |", md)


class RigTests(unittest.TestCase):
    def test_the_probe_is_selected_by_the_host_without_its_user(self) -> None:
        self.assertEqual(rig.probe_address("pi@probe-host"), "probe-host")
        self.assertEqual(rig.probe_address("probe-host"), "probe-host")


class BenchImageTests(unittest.TestCase):
    def test_the_override_is_pointed_at_another_checkout(self) -> None:
        text = 'deluge-bsp = { path = "../../../deluge-sdk/crates/deluge-bsp" }\n'
        out = bench_image.point_override_at(text, Path("/tmp/sdk-wt"))
        self.assertEqual(
            out, 'deluge-bsp = { path = "/tmp/sdk-wt/crates/deluge-bsp" }\n'
        )

    def test_an_override_without_sdk_paths_is_refused(self) -> None:
        with self.assertRaises(ValueError):
            bench_image.point_override_at("[patch]\n", Path("/tmp/sdk-wt"))


if __name__ == "__main__":
    unittest.main()
