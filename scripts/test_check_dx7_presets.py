# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Self-test for check_dx7_presets: a sound in DX7 mode is listed, and a DX7 on an oscillator is refused.

Gate dx7-presets-selftest:
    the DX7 preset scan lists every sound in DX7 mode in XML and JSON files, kits' included, finds a DX7 on either
    oscillator, and its check fails on an oscillator DX7 and names the presets that differ from the manifest
"""

import json
import pathlib
import tempfile
import unittest

import check_dx7_presets as scanner


class ScanTest(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.dir.name)
        self.manifest = self.root / "manifest.json"

    def tearDown(self) -> None:
        self.dir.cleanup()

    def write(self, name: str, text: str) -> None:
        path = self.root / "presets" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def check(self, *flags: str) -> int:
        return scanner.main(
            [
                *flags,
                "--root",
                str(self.root / "presets"),
                "--manifest",
                str(self.manifest),
            ]
        )

    def test_a_dx7_mode_sound_is_listed_in_xml_and_json(self) -> None:
        self.write("a.xml", '<sound mode="dx7"><dx7 patch="00"/></sound>')
        self.write("b.json", json.dumps({"sound": {"mode": "dx7", "dx7": {}}}))
        self.write("c.xml", '<sound mode="subtractive"><osc1 type="saw"/></sound>')
        self.assertEqual(
            scanner.scan(self.root / "presets"),
            ([{"path": "a.xml"}, {"path": "b.json"}], []),
        )

    def test_a_kits_drums_are_each_named_by_index(self) -> None:
        self.write(
            "kit.xml",
            '<kit><soundSources><sound mode="subtractive"/><sound mode="dx7"/></soundSources></kit>',
        )
        self.assertEqual(
            scanner.scan(self.root / "presets"), ([{"path": "kit.xml#1"}], [])
        )

    def test_a_dx7_on_either_oscillator_is_found(self) -> None:
        self.write("one.xml", '<sound><osc1 type="dx7"/><osc2 type="saw"/></sound>')
        self.write(
            "two.json",
            json.dumps({"sound": {"osc1": {"type": "saw"}, "osc2": {"type": "dx7"}}}),
        )
        self.assertEqual(
            scanner.scan(self.root / "presets"), ([], ["one.xml", "two.json"])
        )

    def test_the_check_refuses_an_oscillator_dx7(self) -> None:
        self.write("one.xml", '<sound><osc1 type="dx7"/></sound>')
        self.assertEqual(self.check("--write"), 1)
        self.assertEqual(self.check(), 1)

    def test_the_check_holds_the_scan_to_the_manifest(self) -> None:
        self.write("a.xml", '<sound mode="dx7"/>')
        self.assertEqual(self.check("--write"), 0)
        self.assertEqual(self.check(), 0)
        self.write("b.xml", '<sound mode="dx7"/>')
        self.assertEqual(self.check(), 1)


if __name__ == "__main__":
    unittest.main()
