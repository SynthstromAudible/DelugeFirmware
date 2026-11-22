# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for compare_strings.py: what a rename may change in an image's strings, and what it must not.

Gate compare-strings-selftest:
    the string comparison of two images equates strings that differ only by the renamed paths and crates, applied in
    one pass with the earlier rename first at a position, and reports a changed message or a changed count
"""

from __future__ import annotations

import struct
import unittest
import zlib
from pathlib import Path
from unittest import mock

import compare_strings
from compare_strings import diff_strings, sdram, strings


class DiffStrings(unittest.TestCase):
    def test_equal_after_a_path_rename(self):
        old = ["/r/crates/deluge_alloc/src/lib.rs", "keep me"]
        new = ["/r/src/base/alloc/src/lib.rs", "keep me"]
        self.assertEqual(
            diff_strings(old, new, [("crates/deluge_alloc", "src/base/alloc")]),
            ([], []),
        )

    def test_a_changed_message_is_reported(self):
        self.assertEqual(
            diff_strings(["out of memory"], ["out of memry"], []),
            (["out of memory"], ["out of memry"]),
        )

    def test_counts_matter(self):
        self.assertEqual(
            diff_strings(["a path", "a path"], ["a path"], []), (["a path"], [])
        )

    def test_renames_apply_in_order_once(self):
        renames = [
            ("crates/deluge_sample_fill", "src/storage/sample/fill"),
            ("deluge_sample_fill", "deluge_storage_sample_fill"),
        ]
        self.assertEqual(
            diff_strings(
                ["crates/deluge_sample_fill/src/x.rs deluge_sample_fill::f"],
                ["src/storage/sample/fill/src/x.rs deluge_storage_sample_fill::f"],
                renames,
            ),
            ([], []),
        )

    def test_a_cascade_is_not_renamed_again_by_an_earlier_rename(self):
        renames = [("a/x", "b/x"), ("b/x", "a/x")]
        self.assertEqual(diff_strings(["a/x file"], ["b/x file"], renames), ([], []))
        self.assertEqual(diff_strings(["b/x file"], ["a/x file"], renames), ([], []))
        self.assertEqual(
            diff_strings(["a/x file"], ["a/x file"], renames),
            (["b/x file"], ["a/x file"]),
        )


def pack(raw: bytes, claimed: int | None = None) -> bytes:
    deflated = zlib.compress(raw)
    return (
        struct.pack("<II", len(raw) if claimed is None else claimed, len(deflated))
        + deflated
    )


def sections(**by_name: bytes):
    """A stand-in for section() serving @p by_name (keys without the leading dot)."""
    return lambda objcopy, elf, name: by_name.get(name.lstrip("."), b"")


class Strings(unittest.TestCase):
    def read(self, at=None, ranges=(), **by_name):
        """strings() over the sections @p by_name, loaded at @p at (section name -> address), in an image whose loaded
        address ranges are @p ranges."""
        layout = lambda readelf, elf: (at or {}, list(ranges))
        with (
            mock.patch.object(compare_strings, "section", sections(**by_name)),
            mock.patch.object(compare_strings, "layout", layout, create=True),
        ):
            return strings("objcopy", "x.elf")

    # A string with no NUL after it, then a pointer to text elsewhere in the section whose two low bytes print
    # (`E2`, `@2`: 0x20013245, 0x20013240, into `pointed-at text` at offset 0x3240), then a byte that does not.
    POINTED = b"\x00hello world"

    def pointed(self, text: bytes, pointer: int, base: int = 0x20010000) -> list[str]:
        blob = bytearray(text + struct.pack("<I", pointer) + b"\x01")
        blob += bytes(0x3240 - len(blob)) + b"pointed-at text\x00"
        return [
            s
            for s in self.read(
                at={".rodata": base},
                ranges=[(0x20000000, 0x20300000)],
                rodata=bytes(blob),
            )
            if s != "pointed-at text"
        ]

    def test_a_run_ends_where_a_pointer_word_starts(self):
        self.assertEqual(self.pointed(self.POINTED, 0x20013245), ["hello world"])
        self.assertEqual(
            self.pointed(self.POINTED, 0x20013245),
            self.pointed(self.POINTED, 0x20013240),
        )

    def test_a_changed_character_before_a_pointer_is_reported(self):
        old, new = (
            self.pointed(self.POINTED, 0x20013245),
            self.pointed(b"\x00hello wurld", 0x20013240),
        )
        self.assertEqual(diff_strings(old, new, []), (["hello world"], ["hello wurld"]))

    def test_a_word_is_a_pointer_only_aligned_and_at_loaded_text(self):
        # Two bytes off its alignment (the section loads at an address ≡ 2 mod 4), or pointing at no text (a zero byte
        # of the section; code, an encoded format string's argument bytes), the word is text.
        self.assertEqual(
            self.pointed(self.POINTED, 0x20013245, base=0x20010002), ["hello worldE2"]
        )
        self.assertEqual(self.pointed(self.POINTED, 0x20012045), ["hello worldE "])

    def test_a_word_the_run_reads_on_past_is_text(self):
        # ` -  ` is 0x20202d20, inside the range, but the text goes on after it.
        found = self.read(
            at={".rodata": 0x20000000},
            ranges=[(0x20000000, 0x20300000)],
            rodata=b"\x00failed -  %d and more\x00",
        )
        self.assertEqual(found, ["failed -  %d and more"])

    def test_a_c_string_is_never_cut(self):
        # `rs\0\x0c` reads as 0x0c007372, an address, but the NUL ends a C string there.
        found = self.read(
            at={".rodata": 0x20000000},
            ranges=[(0x0C000000, 0x0C200000)],
            rodata=b"\x00src/device.rs\x00\x0c",
        )
        self.assertEqual(found, ["src/device.rs"])

    def test_a_run_cut_short_needs_six_characters(self):
        self.assertEqual(self.pointed(b"\x00\x00\x00\x00abcd", 0x20013245), [])

    def test_a_changed_four_character_c_string_is_reported(self):
        old, new = self.read(rodata=b"\x00abcd\x00"), self.read(rodata=b"\x00abce\x00")
        self.assertEqual(diff_strings(old, new, []), (["abcd"], ["abce"]))

    def test_short_runs_count_only_before_a_nul(self):
        self.assertEqual(
            self.read(rodata=b"\x00ab\x00abc\x00abcde\x00abcdef\xff"),
            ["abc", "abcde", "abcdef"],
        )
        self.assertEqual(self.read(rodata=b"abcde\xff\x01abcd"), [])

    def test_strings_come_from_rodata_data_and_the_inflated_pack(self):
        found = self.read(
            rodata=b"read only\x00",
            data=b"read write\x00",
            sdram_init_pack=pack(b"in sdram\x00"),
        )
        self.assertEqual(sorted(found), ["in sdram", "read only", "read write"])


class Sdram(unittest.TestCase):
    def read(self, blob):
        with mock.patch.object(
            compare_strings, "section", sections(sdram_init_pack=blob)
        ):
            return sdram("objcopy", "x.elf")

    def test_a_valid_pack_inflates(self):
        self.assertEqual(self.read(pack(b"hello there")), b"hello there")

    def test_a_short_pack_is_empty(self):
        self.assertEqual(self.read(b"\x01\x02\x03"), b"")

    def test_a_length_mismatch_raises(self):
        with self.assertRaises(ValueError):
            self.read(pack(b"hello there", claimed=5))


class Section(unittest.TestCase):
    def test_it_asks_objcopy_for_one_section_as_binary(self):
        with mock.patch.object(compare_strings.subprocess, "run") as run:
            run.side_effect = lambda cmd, check: Path(cmd[-1]).write_bytes(b"bytes")
            self.assertEqual(
                compare_strings.section("oc", "x.elf", ".rodata"), b"bytes"
            )
        cmd = run.call_args.args[0]
        self.assertEqual(cmd[:4], ["oc", "-O", "binary", "--only-section=.rodata"])
        self.assertEqual(cmd[4], "x.elf")


class DefaultObjcopy(unittest.TestCase):
    def test_it_resolves_against_the_repo_root_not_the_cwd(self):
        self.assertTrue(compare_strings.DEFAULT_OBJCOPY.is_absolute())
        self.assertTrue(compare_strings.DEFAULT_OBJCOPY.is_file())


if __name__ == "__main__":
    unittest.main()
