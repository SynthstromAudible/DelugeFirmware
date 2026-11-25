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


def usb(in_, out, interrupt_us=None):
    streams = " ".join(
        f"{name}_packets={p} {name}_frames={f} {name}_late={late} {name}_missed={missed} {name}_max_gap_us={gap}"
        for name, (p, f, late, missed, gap) in (("in", in_), ("out", out))
    )
    return f"12.0: INFO  [deluge_platform_device::audio::device::probe] USB-AUDIO-TIMING {streams}{'' if interrupt_us is None else f' interrupt_us={interrupt_us}'}"


def dma_usb(in_, out_frames, interrupt_us=None):
    packets, frames, stops, ahead = in_
    range_ = (
        "" if ahead is None else f" in_ahead_min={ahead[0]} in_ahead_max={ahead[1]}"
    )
    irq = "" if interrupt_us is None else f" interrupt_us={interrupt_us}"
    return f"12.0: INFO  [probe] USB-AUDIO-TIMING in_packets={packets} in_frames={frames} in_stops={stops}{range_} out_frames={out_frames}{irq}"


def usb_capture():
    boot, loaded, idle, idle_end, steady_1, steady_2, steady_end = capture()
    return [
        boot,
        usb((9, 9, 9, 9, 9), (9, 9, 9, 9, 9)),  # the boot's too
        loaded,
        idle,
        idle_end,
        steady_1,
        usb((16000, 88200, 2, 7, 600), (15990, 88150, 1, 4, 700)),
        steady_2,
        usb((15900, 88100, 1, 3, 500), (15980, 88000, 5, 20, 900)),
        steady_end,
    ]


class UsbTest(unittest.TestCase):
    def test_a_phase_sums_each_streams_counts_and_keeps_its_longest_gap(self):
        steady = audio_timing.parse(usb_capture())["steady"]
        self.assertEqual(
            steady.usb_in, audio_timing.UsbStream(31900, 176300, 3, 10, 600)
        )
        self.assertEqual(
            steady.usb_out, audio_timing.UsbStream(31970, 176150, 6, 24, 900)
        )

    def test_boot_lines_are_left_out(self):
        phases = audio_timing.parse(usb_capture())
        self.assertIsNone(phases["idle"].usb_in)

    def test_a_usb_line_is_not_a_render_line(self):
        self.assertEqual(audio_timing.parse(usb_capture())["steady"].renders, 1380)

    def test_a_capture_without_usb_lines_has_no_streams(self):
        steady = audio_timing.parse(capture())["steady"]
        self.assertIsNone(steady.usb_in)
        self.assertIsNone(steady.usb_out)

    def test_lost_frames_are_what_we_sent_less_what_arrived(self):
        self.assertEqual(
            audio_timing.parse(usb_capture())["steady"].usb_lost_frames, 150
        )
        self.assertIsNone(audio_timing.parse(capture())["steady"].usb_lost_frames)

    def dma_capture(self):
        boot, loaded, idle, idle_end, steady_1, steady_2, steady_end = capture()
        return [
            boot,
            loaded,
            idle,
            idle_end,
            steady_1,
            dma_usb((16000, 88200, 1, (39, 62)), 88150, 1000),
            steady_2,
            dma_usb((15900, 88100, 2, (30, 70)), 88000, 2000),
            steady_end,
        ]

    def test_a_dma_line_gives_frames_stops_and_the_queues_range(self):
        steady = audio_timing.parse(self.dma_capture())["steady"]
        self.assertEqual(
            (steady.usb_in.packets, steady.usb_in.frames, steady.usb_in.stops),
            (31900, 176300, 3),
        )
        self.assertEqual(steady.usb_in.ahead, (30, 70))
        self.assertEqual(steady.usb_out.frames, 176150)
        self.assertEqual(steady.usb_lost_frames, 150)
        self.assertEqual(steady.usb_interrupt_us, 3000)

    def test_a_dma_line_has_no_late_services_to_report(self):
        steady = audio_timing.parse(self.dma_capture())["steady"]
        self.assertIsNone(steady.usb_in.late)
        row = audio_timing.row("run", "steady", steady)
        self.assertIn("| 176300 | - | - | - | 176150 | - | - | - | 150 |", row)
        self.assertTrue(row.endswith("| 3 | 30..70 |"), row)

    def test_the_report_dashes_the_usb_columns_of_a_phase_with_no_usb_lines(self):
        row = audio_timing.row("run", "steady", audio_timing.parse(capture())["steady"])
        self.assertTrue(
            row.endswith("| - | - | - | - | - | - | - | - | - | - | - | - |"), row
        )

    def test_the_report_gives_each_streams_columns(self):
        row = audio_timing.row(
            "run", "steady", audio_timing.parse(usb_capture())["steady"]
        )
        self.assertTrue(
            row.endswith(
                "| 176300 | 3 | 10 | 600 | 176150 | 6 | 24 | 900 | 150 | - | - | - |"
            ),
            row,
        )

    def test_the_interrupts_share_is_its_time_over_the_rendered_time(self):
        lines = usb_capture()
        lines[6] = usb((1, 1, 0, 0, 0), (1, 1, 0, 0, 0), interrupt_us=100_000)
        lines[8] = usb((1, 1, 0, 0, 0), (1, 1, 0, 0, 0), interrupt_us=60_000)
        steady = audio_timing.parse(lines)["steady"]
        self.assertEqual(steady.usb_interrupt_us, 160_000)
        self.assertAlmostEqual(
            steady.usb_interrupt_share, 0.16 / (176640 / 44100), places=6
        )
        self.assertTrue(
            audio_timing.row("run", "steady", steady).endswith("| 4.0% | - | - |")
        )

    def test_a_line_without_the_interrupt_time_has_no_share(self):
        steady = audio_timing.parse(usb_capture())["steady"]
        self.assertIsNone(steady.usb_interrupt_us)
        self.assertIsNone(steady.usb_interrupt_share)


class OverheadTest(unittest.TestCase):
    def test_two_sizes_give_the_fixed_and_per_frame_cost(self):
        small = audio_timing.Phase(renders=100, frames=6400, us=100 * (200 + 64 * 5))
        large = audio_timing.Phase(renders=100, frames=12800, us=100 * (200 + 128 * 5))
        fixed, per_frame = audio_timing.overhead(small, large)
        self.assertAlmostEqual(fixed, 200.0)
        self.assertAlmostEqual(per_frame, 5.0)


if __name__ == "__main__":
    unittest.main()
