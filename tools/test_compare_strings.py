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
from typing import ClassVar
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
    def read(self, at=None, ranges=(), objects=(), targets=(), **by_name):
        """strings() over the sections @p by_name, loaded at @p at (section name -> address), in an image whose loaded
        address ranges are @p ranges, whose data objects (no strings) occupy @p objects, and whose symbols start at
        @p targets."""
        layout = lambda readelf, elf: (at or {}, list(ranges))
        with (
            mock.patch.object(compare_strings, "section", sections(**by_name)),
            mock.patch.object(compare_strings, "layout", layout, create=True),
            mock.patch.object(
                compare_strings,
                "data_objects",
                lambda nm, elf: list(objects),
                create=True,
            ),
            mock.patch.object(
                compare_strings,
                "pointer_targets",
                lambda nm, elf: set(targets),
                create=True,
            ),
        ):
            return strings("objcopy", "x.elf")

    # A string with no NUL after it, then a pointer whose two low bytes print (`E2`, `E3`: 0x20013245, 0x20013345)
    # to the start of a string elsewhere in the section, then a byte that does not.
    POINTED = b"\x00hello world"
    TARGETS: ClassVar[dict[int, bytes]] = {
        0x3245: b"\x00pointed-at text\x00",
        0x3345: b"\x00another target\x00",
    }

    def section_with(self, head: bytes, size: int = 0x3400, targets=None) -> bytes:
        """@p head, then zeros, with each of @p targets (offset of its first character -> NUL-framed text) in place."""
        blob = bytearray(head + bytes(size - len(head)))
        for offset, text in (self.TARGETS if targets is None else targets).items():
            blob[offset - 1 : offset - 1 + len(text)] = text
        return bytes(blob)

    def pointed(
        self, text: bytes, pointer: int, base: int = 0x20010000, targets=None
    ) -> list[str]:
        blob = self.section_with(
            text + struct.pack("<I", pointer) + b"\x01", targets=targets
        )
        found = self.read(
            at={".rodata": base}, ranges=[(0x20000000, 0x20300000)], rodata=blob
        )
        return [s for s in found if s not in ("pointed-at text", "another target")]

    def test_a_run_ends_where_a_pointer_word_starts(self):
        self.assertEqual(self.pointed(self.POINTED, 0x20013245), ["hello world"])
        self.assertEqual(
            self.pointed(self.POINTED, 0x20013245),
            self.pointed(self.POINTED, 0x20013345),
        )

    def test_a_changed_character_before_a_pointer_is_reported(self):
        old, new = (
            self.pointed(self.POINTED, 0x20013245),
            self.pointed(b"\x00hello wurld", 0x20013345),
        )
        self.assertEqual(diff_strings(old, new, []), (["hello world"], ["hello wurld"]))

    def test_a_word_is_a_pointer_only_aligned_and_at_a_string_start(self):
        # Two bytes off its alignment (the section loads at an address ≡ 2 mod 4), pointing at no text (a zero byte;
        # code, an encoded format string's argument bytes), or into the middle of a string, the word is text.
        self.assertEqual(
            # The string is planted where the word points from this base, so only the load address's alignment,
            # not the blob offset's, keeps the word whole.
            self.pointed(
                self.POINTED,
                0x20013245,
                base=0x20010002,
                targets={0x3243: b"\x00pointed-at text\x00"},
            ),
            ["hello worldE2"],
        )
        self.assertEqual(self.pointed(self.POINTED, 0x20012045), ["hello worldE "])
        self.assertEqual(self.pointed(self.POINTED, 0x20013247), ["hello worldG2"])

    def test_a_word_is_a_pointer_only_to_a_string_of_eight_or_more(self):
        short = {0x3245: b"\x00seven c\x00"}
        blob = self.section_with(
            self.POINTED + struct.pack("<I", 0x20013245) + b"\x01", targets=short
        )
        found = self.read(
            at={".rodata": 0x20010000}, ranges=[(0x20000000, 0x20300000)], rodata=blob
        )
        self.assertIn("hello worldE2", found)

    def words(self, *values: int) -> bytes:
        return struct.pack(f"<{len(values)}I", *values)

    # Where the pointer words below point: symbol starts.
    SYMBOL_STARTS = frozenset({0x20253245, 0x20253345, 0x20243346, 0x20212121})

    def loaded(
        self, rodata: bytes, objects=(), targets=SYMBOL_STARTS, base=0x20000000
    ) -> list[str]:
        return self.read(
            at={".rodata": base},
            ranges=[(0x20000000, 0x20300000)],
            objects=objects,
            targets=targets,
            rodata=rodata,
        )

    def test_a_zero_word_is_no_pointer(self):
        # `__fini_array_start` and absolute constants (`IRQ_MODE`, stack sizes) are symbols at no loaded address: a
        # NUL word in a named object that ends a C string stays a NUL.
        chars = bytes(5) + b"abc" + bytes(4)
        found = self.loaded(
            chars, [(0x20000004, 0x2000000C)], targets={0, 0x12, 0x2000}
        )
        self.assertEqual(found, ["abc"])

    def test_text_in_a_named_char_array_is_read(self):
        # A `static const char` table: `    ` reads as 0x20202020, an address in the image, but no symbol starts there.
        chars = b"\x00\x00\x00\x00" + b"ab      cdefgh\x00"
        self.assertEqual(
            self.loaded(chars, [(0x20000004, 0x20000013)]), ["ab      cdefgh"]
        )

    def test_a_pointer_word_is_aligned_by_its_load_address(self):
        # The object starts at 0x20000002; its pointer word sits at 0x20000004, two bytes into it.
        blob = b"\x00\x00\x00\x00" + self.words(0x20253245) + b"St9bad_alloc\x00"
        self.assertEqual(
            self.loaded(blob, [(0x20000002, 0x20000008)]), ["St9bad_alloc"]
        )

    def test_a_table_of_pointers_is_no_string(self):
        # SRAM addresses end in `2X 20`, so a table of them (a data object) prints; the NUL after it would make it a
        # C string.
        table = self.words(0x20253245, 0x20243346, 0x20212121)
        self.assertEqual(
            self.loaded(
                b"\x00\x00\x00\x00" + table + b"\x00", [(0x20000004, 0x20000010)]
            ),
            [],
        )

    def test_a_name_after_a_pointer_word_compares_alike_when_the_pointer_changes(self):
        # A type-info object (its vtable pointer), then its name (`_ZTS…`, a string).
        typeinfo = [(0x20000004, 0x20000008)]
        old = self.loaded(
            b"\x00\x00\x00\x00" + self.words(0x20253245) + b"St9bad_alloc\x00", typeinfo
        )
        new = self.loaded(
            b"\x00\x00\x00\x00" + self.words(0x20253345) + b"St9bad_alloc\x00", typeinfo
        )
        self.assertEqual(old, ["St9bad_alloc"])
        self.assertEqual(diff_strings(old, new, []), ([], []))

    def test_a_changed_name_after_a_pointer_word_is_reported(self):
        typeinfo = [(0x20000004, 0x20000008)]
        old = self.loaded(
            b"\x00\x00\x00\x00" + self.words(0x20253245) + b"St9bad_alloc\x00", typeinfo
        )
        new = self.loaded(
            b"\x00\x00\x00\x00" + self.words(0x20253345) + b"St9bad_allor\x00", typeinfo
        )
        self.assertEqual(
            diff_strings(old, new, []), (["St9bad_alloc"], ["St9bad_allor"])
        )

    def test_a_pointer_like_word_outside_a_data_object_is_text(self):
        # ` -  ` reads as 0x20202d20, but a string literal is no data object.
        found = self.loaded(
            b"\x00\x00\x00\x00failed -  %d and more\x00", [(0x20000100, 0x20000110)]
        )
        self.assertEqual(found, ["failed -  %d and more"])

    def test_a_string_the_section_ends_short_of_eight_is_no_target(self):
        blob = self.section_with(
            self.POINTED + struct.pack("<I", 0x20013245) + b"\x01",
            0x3248,
            {0x3245: b"\x00abc"},
        )
        found = self.read(
            at={".rodata": 0x20010000}, ranges=[(0x20000000, 0x20300000)], rodata=blob
        )
        self.assertIn("hello worldE2", found)

    def lone_targets(self, head: bytes, at: int, base: int) -> list[str]:
        """strings() over @p head, loaded at @p base, with a printable byte alone wherever the word at @p at of any
        variant of the last character could point: what a jump table or a code byte looks like."""
        blob = bytearray(head + bytes(0x10000))
        for last in range(0x20, 0x7F):
            word = bytearray(blob[at : at + 4])
            value = struct.unpack("<I", bytes(word))[0]
            for candidate in (
                value,
                (value & ~0xFF) | last,
                (value & ~0xFF00) | (last << 8),
            ):
                offset = candidate - base
                if (
                    at + 4 < offset - 1
                    and offset + 1 < len(blob)
                    and not any(blob[offset - 1 : offset + 2])
                ):
                    blob[offset - 1 : offset + 2] = b"\x018\x01"
        return self.read(
            at={".rodata": base}, ranges=[(0x20000000, 0x20300000)], rodata=bytes(blob)
        )

    def test_the_dma_channel_space_is_text(self):
        # From a device image: an encoded Rust format template, `: DMA channel ` then argument bytes `c0 1f`, then
        # ` didn't stop`. The run's last word `20 c0 1f 20` reads as 0x201fc020, a one-character run of a jump table.
        def image(space: bytes) -> list[str]:
            head = (
                b"\x00\x00\x00\x00sdhi\xc0\x0e: DMA channel"
                + space
                + b"\xc0\x1f didn't stop after a read\x00"
            )
            at = head.index(b"\xc0\x1f") - 1
            base = 0x201FC020 - 0xC000 - at
            base -= (base + at) % 4
            return self.lone_targets(head, at, base)

        self.assertIn(": DMA channel ", image(b" "))
        self.assertNotEqual(diff_strings(image(b" "), image(b"8"), []), ([], []))

    def test_a_change_in_a_word_that_points_at_no_string_start_is_reported(self):
        # The last word printable throughout (`ab  `, 0x20206261), or the run ending two bytes into it (`ab` then
        # `1e 20`, 0x201e6261): each points at a single printable byte, so the word is text and `ab`->`ac` shows.
        def image(tail: bytes, after: bytes, base: int) -> list[str]:
            head = b"\x00\x00\x00the value is " + tail + after
            return self.lone_targets(head, 16, base)

        full_old, full_new = (
            image(b"ab  ", b"\x01", 0x20200000),
            image(b"ac  ", b"\x01", 0x20200000),
        )
        self.assertEqual(
            diff_strings(full_old, full_new, []),
            (["the value is ab  "], ["the value is ac  "]),
        )
        mid_old, mid_new = (
            image(b"ab", b"\x1e\x20\x01", 0x201E0000),
            image(b"ac", b"\x1e\x20\x01", 0x201E0000),
        )
        self.assertEqual(
            diff_strings(mid_old, mid_new, []),
            (["the value is ab"], ["the value is ac"]),
        )

    def test_a_pointer_after_a_pointer_is_cut_too(self):
        # `E2% ` (0x20253245, all printable) then `E2\x01 ` (0x20013245): both point at string starts, one in .rodata
        # and one in .data, so the run loses both words.
        rodata = self.section_with(
            b"\x00\x00\x00\x00hello world!"
            + struct.pack("<II", 0x20253245, 0x20013245)
            + b"\x01",
            targets={0x3245: b"\x00rodata target\x00"},
        )
        data = self.section_with(b"", targets={0x3245: b"\x00data target text\x00"})
        found = self.read(
            at={".rodata": 0x20250000, ".data": 0x20010000},
            ranges=[(0x20000000, 0x20300000)],
            rodata=rodata,
            data=data,
        )
        self.assertIn("hello world!", found)

    def test_a_word_the_run_reads_on_past_is_text(self):
        # ` -  ` is 0x20202d20, inside the range, but the text goes on after it.
        found = self.read(
            at={".rodata": 0x20000000},
            ranges=[(0x20000000, 0x20300000)],
            rodata=b"\x00failed -  %d and more\x00",
        )
        self.assertEqual(found, ["failed -  %d and more"])

    def test_a_c_string_is_never_cut(self):
        # `rs\0\x0c` reads as 0x0c007372, the start of a string in .data, but the NUL ends a C string there.
        data = self.section_with(
            b"", size=0x7400, targets={0x7372: b"\x00a string in data\x00"}
        )
        found = self.read(
            at={".rodata": 0x20000000, ".data": 0x0C000000},
            ranges=[(0x0C000000, 0x0C200000)],
            rodata=b"\x00src/device.rs\x00\x0c",
            data=data,
        )
        self.assertIn("src/device.rs", found)

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


class DataObjects(unittest.TestCase):
    def test_string_symbols_are_no_data_objects(self):
        listing = (
            "201de59f 00000031 r .str.9.llvm.2661485587678768373\n201e0000 00000010 V _ZTISt9bad_alloc\n"
            "201e0010 0000000d V _ZTSSt9bad_alloc\n201e0020 00000000 t $d\n2007cf21 00000008 T f\n"
            "201e0030 00000000 r kEmpty\n"
        )
        with mock.patch.object(compare_strings.subprocess, "run") as run:
            run.return_value.stdout = listing
            found = compare_strings.data_objects("nm", "x.elf")
        self.assertEqual(found, [(0x201E0000, 0x201E0010), (0x2007CF20, 0x2007CF28)])


class PointerTargets(unittest.TestCase):
    def test_symbol_starts_thumb_entries_and_places_in_vtables(self):
        listing = "20001000 00000004 T f\n20200000 0000000c r _ZTV1X\n20200020 00000000 t $d\n"
        with mock.patch.object(compare_strings.subprocess, "run") as run:
            run.return_value.stdout = listing
            found = compare_strings.pointer_targets("nm", "x.elf")
        self.assertEqual(
            found,
            {
                0x20001000,
                0x20001001,
                0x20200000,
                0x20200001,
                0x20200004,
                0x20200008,
                0x2020000C,
            },
        )


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
    def test_the_default_is_llvm_objcopy(self):
        with (
            mock.patch.object(
                compare_strings.sys, "argv", ["compare_strings.py", "a.elf", "b.elf"]
            ),
            mock.patch.object(compare_strings, "strings", return_value=[]) as read,
            mock.patch("builtins.print"),
        ):
            compare_strings.main()
        self.assertEqual(read.call_args.args[0], "llvm-objcopy")


if __name__ == "__main__":
    unittest.main()
