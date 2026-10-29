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


if __name__ == "__main__":
    unittest.main()
