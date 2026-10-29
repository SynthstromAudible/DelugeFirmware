#!/usr/bin/env python3
"""Tests for device_check.py's capture analysis: `python3 -m unittest test_device_check`."""

import unittest

import device_check

BOOT = [
    "INFO  [deluge_bsp_rust::runtime::device] deluge-rust: boot — Rust/Embassy BSP for RZ/A1L",
    "INFO  [deluge_bsp_rust::sdram_pack] SDRAM memory image ready",
    "1.499: deluge.cpp:700: going into main loop",
    "1.520: oled.cpp:90: first OLED frame sent",
]


def check(lines: list[str]) -> dict:
    report = device_check.analyse(lines)
    return device_check.verdict(report, 0, 0, expect_harness=True)


class HarnessReady(unittest.TestCase):
    def test_harness_ready_is_required(self):
        checks = check(BOOT + ["@T 0 1600000", "@T 1 1610000"])
        self.assertFalse(checks["harness_ready"])

    def test_ready_and_streaming_after_the_first_frame_pass(self):
        checks = check(
            ["@R 0 50000"] + BOOT + ["@T 1 1600000", "@O 2 1600000 128 48 00"]
        )
        self.assertTrue(checks["harness_ready"])
        self.assertTrue(checks["harness_streaming"])

    def test_a_stream_that_stops_before_the_app_draws_fails(self):
        checks = check(["@R 0 50000", "@T 1 60000"] + BOOT)
        self.assertTrue(checks["harness_ready"])
        self.assertFalse(checks["harness_streaming"])

    def test_harness_lines_are_not_log_lines(self):
        # A frame's hex can hold any letters; it is never an error or a setting.
        report = device_check.analyse(BOOT + ["@O 3 1700000 128 48 deadbeefFREEZE"])
        self.assertEqual(report["errors"], [])

    def test_without_expect_harness_no_harness_checks(self):
        report = device_check.analyse(BOOT)
        self.assertNotIn("harness_ready", device_check.verdict(report, 0, 0))


if __name__ == "__main__":
    unittest.main()
