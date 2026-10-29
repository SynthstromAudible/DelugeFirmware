#!/usr/bin/env python3
"""Tests for device_parity.py's settings-flash image: `python3 -m unittest test_device_parity`."""

import unittest

import device_parity


class SettingsImage(unittest.TestCase):
    def test_the_sector_lands_at_the_settings_offset_in_an_erased_array(self):
        sector = bytes(range(256)) * 16
        image = device_parity.settings_flash_image(sector)
        self.assertEqual(len(image), device_parity.SETTINGS_OFFSET + len(sector))
        self.assertEqual(
            image[: device_parity.SETTINGS_OFFSET],
            b"\xff" * device_parity.SETTINGS_OFFSET,
        )
        self.assertEqual(image[device_parity.SETTINGS_OFFSET :], sector)

    def test_an_all_zero_read_is_refused(self):
        # The flash window reads zeros while the SPI controller is in manual mode.
        with self.assertRaisesRegex(ValueError, "zero"):
            device_parity.settings_flash_image(bytes(device_parity.SETTINGS_BYTES))

    def test_a_short_read_is_refused(self):
        with self.assertRaisesRegex(ValueError, "bytes"):
            device_parity.settings_flash_image(b"\xff" * 100)


class Retry(unittest.TestCase):
    def test_a_link_failure_is_retried_once(self):
        calls = []

        def run():
            calls.append(1)
            if len(calls) == 1:
                raise ConnectionError("probe server did not restart")
            return "ok"

        self.assertEqual(device_parity.retried(run, tries=2), "ok")
        self.assertEqual(len(calls), 2)

    def test_a_second_failure_and_other_errors_propagate(self):
        def dead():
            raise TimeoutError("device clock stuck")

        with self.assertRaises(TimeoutError):
            device_parity.retried(dead, tries=2)

        def refused():
            raise RuntimeError("the device refused 'P 3'")

        calls = []
        with self.assertRaises(RuntimeError):
            device_parity.retried(lambda: calls.append(1) or refused(), tries=2)
        self.assertEqual(len(calls), 1)


class LinkFailures(unittest.TestCase):
    def test_a_target_that_never_came_up_is_a_link_failure(self):
        import deluge_jlink

        self.assertTrue(issubclass(deluge_jlink.TargetError, ConnectionError))

    def test_the_rig_is_parked_and_stopped_whatever_happens(self):
        calls = []

        def boom():
            raise ValueError("bad midi hex")

        with self.assertRaises(ValueError):
            device_parity.parked_after(
                "host",
                boom,
                park=lambda h: calls.append(("park", h)),
                stop=lambda h: calls.append(("stop", h)),
            )
        self.assertEqual(calls, [("park", "host"), ("stop", "host")])


if __name__ == "__main__":
    unittest.main()
