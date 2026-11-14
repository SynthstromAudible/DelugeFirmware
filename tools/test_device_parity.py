#!/usr/bin/env python3
"""Tests for device_parity.py's settings-flash image: `python3 -m unittest test_device_parity`."""

import argparse
import contextlib
import io
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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


class HeaderCard(unittest.TestCase):
    def _script(self, d, name, *cards):
        path = Path(d) / name
        path.write_text(
            "".join(f"# card: {c}\n" for c in cards) + "100 snapshot boot\n"
        )
        return path

    def test_scripts_sharing_a_first_card_run_on_it(self):
        with (
            tempfile.TemporaryDirectory() as d,
            mock.patch.dict(os.environ, {"DELUGE_CONTENT_CARDS": "/cards"}),
        ):
            scripts = [
                self._script(d, "d01-a.txt", "content/card.img"),
                self._script(d, "d02-b.txt", "content/card.img"),
            ]
            self.assertEqual(
                device_parity.header_card(scripts), Path("/cards/card.img")
            )

    def test_scripts_on_different_cards_or_none_are_refused(self):
        with tempfile.TemporaryDirectory() as d:
            a = self._script(d, "d01-a.txt", "content/card.img")
            for other in (
                self._script(d, "d02-b.txt", "content/bare.img"),
                self._script(d, "d03-c.txt"),
            ):
                with (
                    self.subTest(other.name),
                    self.assertRaisesRegex(ValueError, "--card or --sd"),
                ):
                    device_parity.header_card([a, other])


class Run(unittest.TestCase):
    """run(): the device side, and the SD image it builds for delugemu."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.d = Path(tmp.name)
        script = self.d / "d01-a.txt"
        script.write_text("100 snapshot boot\n")
        flash = self.d / "flash.bin"
        flash.write_bytes(b"")
        self.args = argparse.Namespace(
            record_only=False,
            emu_ref=self.d / "ref.elf",
            device=self.d / "device.elf",
            card=self.d / "card",
            sd=None,
            flash=flash,
            delugemu=self.d,
            out=self.d / "out",
            jobs=1,
            host="probe",
            midi_port=None,
            scripts=[script],
        )

    def run_quietly(self):
        with (
            contextlib.redirect_stderr(io.StringIO()) as err,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            return device_parity.run(self.args), err.getvalue()

    @staticmethod
    def sd_image(card, at, delugemu):
        at.parent.mkdir(parents=True, exist_ok=True)
        at.write_bytes(b"img")
        at.with_suffix(".tree").mkdir()
        return at, True

    def test_the_device_side_captures_no_din_midi(self):
        # The rig's MIDI is USB: delugemu's DIN output has nothing to compare against.
        self.args.record_only = True
        with mock.patch.object(
            device_parity.emu_parity, "run_image", return_value={}
        ) as run_image:
            self.run_quietly()
        self.assertIs(run_image.call_args.kwargs.get("din_out"), False)

    def test_a_card_that_does_not_image_is_a_usage_error(self):
        with mock.patch.object(
            device_parity.emu_parity,
            "sd_image",
            side_effect=subprocess.CalledProcessError(1, ["mksd.sh"]),
        ):
            code, err = self.run_quietly()
        self.assertEqual(code, 4)
        self.assertIn("card", err)

    def test_the_built_image_and_tree_go_when_a_run_is_interrupted(self):
        with (
            mock.patch.object(device_parity.emu_parity, "sd_image", self.sd_image),
            mock.patch.object(
                device_parity.emu_parity, "run_image", side_effect=KeyboardInterrupt
            ),
            self.assertRaises(KeyboardInterrupt),
        ):
            self.run_quietly()
        self.assertEqual(sorted(p.name for p in self.args.out.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
