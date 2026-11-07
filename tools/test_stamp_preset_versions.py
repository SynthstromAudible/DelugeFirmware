"""Tests for stamp_preset_versions: which presets are stamped, and how."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import stamp_preset_versions as s

DECL = b'<?xml version="1.0" encoding="UTF-8"?>\n'


class StampTest(unittest.TestCase):
    def test_an_unstamped_preset_gets_the_official_version_first(self):
        out = s.stamp(DECL + b"<sound>\n</sound>\n")
        self.assertEqual(
            out, DECL + b"<firmwareVersion>4.1.3</firmwareVersion>\n<sound>\n</sound>\n"
        )

    def test_an_old_official_element_stamp_is_replaced(self):
        out = s.stamp(DECL + b"<firmwareVersion>2.1.0</firmwareVersion>\n<sound/>\n")
        self.assertEqual(
            out, DECL + b"<firmwareVersion>4.1.3</firmwareVersion>\n<sound/>\n"
        )

    def test_an_old_attribute_stamp_is_replaced(self):
        out = s.stamp(DECL + b'<sound firmwareVersion="2.0.0-beta" x="1"/>\n')
        self.assertEqual(out, DECL + b'<sound firmwareVersion="4.1.3" x="1"/>\n')

    def test_community_and_current_official_stamps_are_left(self):
        for v in (b"c1.2.1", b"c1.0.0", b"4.1.3", b"4.1.0-beta", b"4.2.0"):
            with self.subTest(v=v):
                self.assertIsNone(
                    s.stamp(
                        DECL
                        + b"<firmwareVersion>"
                        + v
                        + b"</firmwareVersion>\n<sound/>\n"
                    )
                )

    def test_a_directory_stamps_synths_and_kits_only(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            for sub in ("SYNTHS", "KITS", "SONGS"):
                (root / sub).mkdir()
                (root / sub / "A.XML").write_bytes(DECL + b"<sound/>\n")
            self.assertEqual(s.stamp_dir(root), 2)
            self.assertEqual(
                (root / "SONGS" / "A.XML").read_bytes(), DECL + b"<sound/>\n"
            )

    @unittest.skipUnless(
        shutil.which("mformat") and shutil.which("mcopy"), "mtools not installed"
    )
    def test_an_image_is_stamped_in_place(self):
        with tempfile.TemporaryDirectory() as d:
            img = Path(d) / "card.img"
            subprocess.run(
                ["mformat", "-C", "-f", "1440", "-i", str(img), "::"], check=True
            )
            src = Path(d) / "SYNTHS"
            src.mkdir()
            (src / "A.XML").write_bytes(DECL + b"<sound/>\n")
            subprocess.run(["mcopy", "-s", "-i", str(img), str(src), "::/"], check=True)
            self.assertEqual(s.stamp_image(img), 1)
            back = subprocess.run(
                ["mtype", "-i", str(img), "::/SYNTHS/A.XML"],
                check=True,
                capture_output=True,
            )
            self.assertIn(b"<firmwareVersion>4.1.3</firmwareVersion>", back.stdout)


if __name__ == "__main__":
    unittest.main()
