# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Self-test for check_dx7_presets: a DX7 on either slot is found, and the other oscillator counts as audible by its
volume or by a cable that reaches it.

Gate dx7-presets-selftest:
    the DX7 preset scan finds a DX7 on either slot of every sound in XML and JSON files, kits' included, reads a volume
    written as an attribute or a child element as the signed word it is, counts a cable to the other oscillator's
    volume, ring modulation, oscillator sync, automation or an unwritten volume as making it audible, and its check
    names the first preset that differs from the manifest
"""

import json
import pathlib
import tempfile
import unittest

import check_dx7_presets as scanner

SILENT = "0x80000000"


def xml_preset(
    osc1: str,
    osc2: str,
    a_volume: str = "0x7FFFFFFF",
    b_volume: str = SILENT,
    cables: str = "",
) -> str:
    return (
        f'<sound mode="subtractive"><osc1 type="{osc1}"/><osc2 type="{osc2}"/>'
        f'<defaultParams oscAVolume="{a_volume}" oscBVolume="{b_volume}"><patchCables>{cables}</patchCables>'
        "</defaultParams></sound>"
    )


class ScanTest(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.dir.name)

    def tearDown(self) -> None:
        self.dir.cleanup()

    def write(self, name: str, text: str) -> None:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def test_a_silent_second_oscillator_is_not_audible(self) -> None:
        self.write("a.xml", xml_preset("dx7", "square"))
        self.assertEqual(
            scanner.scan(self.root),
            [
                {
                    "path": "a.xml",
                    "dx7_slot": "osc1",
                    "other_type": "square",
                    "other_audible": False,
                }
            ],
        )

    def test_a_second_oscillator_above_its_minimum_is_audible(self) -> None:
        self.write("a.xml", xml_preset("dx7", "square", b_volume="0x40000000"))
        self.assertTrue(scanner.scan(self.root)[0]["other_audible"])

    def test_a_cable_to_the_second_oscillator_makes_it_audible(self) -> None:
        cable = (
            '<patchCable source="lfo1" destination="oscBVolume" amount="0x10000000"/>'
        )
        self.write("a.xml", xml_preset("dx7", "square", cables=cable))
        self.assertTrue(scanner.scan(self.root)[0]["other_audible"])

    def test_a_dx7_on_the_second_slot_reads_the_first_as_the_other(self) -> None:
        self.write("b.XML", xml_preset("saw", "dx7", a_volume=SILENT))
        self.assertEqual(
            scanner.scan(self.root),
            [
                {
                    "path": "b.XML",
                    "dx7_slot": "osc2",
                    "other_type": "saw",
                    "other_audible": False,
                }
            ],
        )

    def test_params_as_child_elements_read_as_attributes_do(self) -> None:
        self.write(
            "c.xml",
            '<sound><osc1 type="dx7"/><osc2 type="triangle"/><defaultParams><oscBVolume>0x40000000</oscBVolume>'
            "</defaultParams></sound>",
        )
        self.assertTrue(scanner.scan(self.root)[0]["other_audible"])

    def test_a_json_preset_reads_as_its_xml_form_does(self) -> None:
        sound = {
            "osc1": {"type": "dx7"},
            "osc2": {"type": "square"},
            "defaultParams": {
                "oscAVolume": "0x7FFFFFFF",
                "oscBVolume": SILENT,
                "patchCables": [
                    {
                        "patchCable": {
                            "source": "lfo1",
                            "destination": "oscBVolume",
                            "amount": "0x1",
                        }
                    }
                ],
            },
        }
        self.write("d.json", json.dumps({"sound": sound}))
        self.assertEqual(
            scanner.scan(self.root),
            [
                {
                    "path": "d.json",
                    "dx7_slot": "osc1",
                    "other_type": "square",
                    "other_audible": True,
                }
            ],
        )

    def test_ring_modulation_and_oscillator_sync_count_the_other_oscillator_audible(
        self,
    ) -> None:
        self.write(
            "r.xml",
            xml_preset("dx7", "square").replace('mode="subtractive"', 'mode="ringmod"'),
        )
        self.write(
            "s.xml",
            xml_preset("saw", "dx7", a_volume=SILENT).replace(
                "<osc1", "<oscillatorSync>1</oscillatorSync><osc1"
            ),
        )
        self.assertEqual(
            [p["other_audible"] for p in scanner.scan(self.root)], [True, True]
        )

    def test_an_automated_or_unwritten_volume_counts_audible(self) -> None:
        self.write(
            "a.xml",
            xml_preset("dx7", "square", b_volume="0x80000000000000000000000040000000"),
        )
        self.write(
            "b.xml",
            '<sound><osc1 type="dx7"/><osc2 type="square"/><defaultParams oscAVolume="0x7FFFFFFF"/></sound>',
        )
        self.assertEqual(
            [p["other_audible"] for p in scanner.scan(self.root)], [True, True]
        )

    def test_each_sound_a_kit_holds_is_scanned(self) -> None:
        silent = xml_preset("dx7", "square")
        self.write(
            "k.xml",
            f"<kit><soundSources>{xml_preset('saw', 'square')}{silent}</soundSources></kit>",
        )
        self.assertEqual(
            scanner.scan(self.root),
            [
                {
                    "path": "k.xml#1",
                    "dx7_slot": "osc1",
                    "other_type": "square",
                    "other_audible": False,
                }
            ],
        )

    def test_a_preset_without_a_dx7_is_not_listed(self) -> None:
        self.write("e.xml", xml_preset("saw", "square"))
        self.assertEqual(scanner.scan(self.root), [])

    def test_check_names_the_first_preset_that_differs_and_write_records_the_scan(
        self,
    ) -> None:
        self.write("a.xml", xml_preset("dx7", "square"))
        manifest = self.root / "manifest.json"
        self.assertEqual(
            scanner.main(
                ["--write", "--root", str(self.root), "--manifest", str(manifest)]
            ),
            0,
        )
        self.assertEqual(
            scanner.main(
                ["--check", "--root", str(self.root), "--manifest", str(manifest)]
            ),
            0,
        )
        recorded = json.loads(manifest.read_text())
        recorded["presets"][0]["other_audible"] = True
        manifest.write_text(json.dumps(recorded))
        self.assertEqual(
            scanner.main(
                ["--check", "--root", str(self.root), "--manifest", str(manifest)]
            ),
            1,
        )


if __name__ == "__main__":
    unittest.main()
