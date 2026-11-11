#!/usr/bin/env python3
"""Tests for audio_timing.py: run with `python3 -m unittest test_audio_timing`."""

import unittest

import audio_timing


def timing(
    renders,
    frames,
    us,
    max_us=1000,
    min_queued=900,
    min_slack=850,
    overruns=0,
    late=20,
    drive=None,
):
    # `drive=None` is a line from an image before the fixed-block drive, which has no drive_overruns.
    drive_field = "" if drive is None else f"drive_overruns={drive} "
    return (
        f"12.0: INFO  [deluge_bsp_rust::audio::device::probe] AUDIO-TIMING renders={renders} frames={frames} "
        f"us={us} f2={frames * frames // max(renders, 1)} fus=0 max_us={max_us} max_us_frames=128 "
        f"min_queued={min_queued} min_slack={min_slack} overruns={overruns} max_wake_late_us={late} "
        f"{drive_field}us_hist=0,0,0,0,0,0,0,0,0,0,{renders},0,0,0 frames_hist=0,0,0,0,0,0,0,0,{renders}"
    )


def capture():
    return [
        timing(10, 1280, 900, min_queued=100),  # boot, before the song loads
        "13.0: stream_bench.cpp:1: STREAM-BENCH-LOADED song=Cordae ok=1",
        timing(700, 88200, 200000),
        "18.0: stream_bench.cpp:1: STREAM-BENCH-PHASE phase=idle ms=5005 underrun_wait=0",
        timing(690, 88320, 700000, max_us=2200, min_queued=880, min_slack=800, late=35),
        timing(
            690,
            88320,
            800000,
            max_us=1900,
            min_queued=870,
            min_slack=820,
            overruns=1,
            late=60,
            drive=2,
        ),
        "48.0: stream_bench.cpp:1: STREAM-BENCH-PHASE phase=steady ms=30000 underrun_wait=0",
    ]


class ParseTest(unittest.TestCase):
    def test_lines_belong_to_the_phase_they_precede_and_boot_is_dropped(self):
        phases = audio_timing.parse(capture())
        self.assertEqual(list(phases), ["idle", "steady"])
        self.assertEqual(phases["idle"].renders, 700)
        self.assertEqual(phases["steady"].renders, 1380)

    def test_a_phase_keeps_sums_and_extremes(self):
        steady = audio_timing.parse(capture())["steady"]
        self.assertEqual(steady.frames, 176640)
        self.assertEqual(steady.us, 1500000)
        self.assertEqual(steady.max_us, 2200)
        self.assertEqual(steady.min_queued, 870)
        self.assertEqual(steady.min_slack, 800)
        self.assertEqual(steady.overruns, 1)
        self.assertEqual(steady.max_wake_late_us, 60)
        self.assertEqual(steady.drive_overruns, 2)

    def test_a_line_without_drive_overruns_reads_as_none(self):
        idle = audio_timing.parse(capture())["idle"]
        self.assertIsNone(idle.drive_overruns)

    def test_load_is_render_time_over_real_time(self):
        steady = audio_timing.parse(capture())["steady"]
        self.assertAlmostEqual(steady.load, 1.5 / (176640 / 44100), places=6)


class OverheadTest(unittest.TestCase):
    def test_two_sizes_give_the_fixed_and_per_frame_cost(self):
        small = audio_timing.Phase(renders=100, frames=6400, us=100 * (200 + 64 * 5))
        large = audio_timing.Phase(renders=100, frames=12800, us=100 * (200 + 128 * 5))
        fixed, per_frame = audio_timing.overhead(small, large)
        self.assertAlmostEqual(fixed, 200.0)
        self.assertAlmostEqual(per_frame, 5.0)


if __name__ == "__main__":
    unittest.main()
