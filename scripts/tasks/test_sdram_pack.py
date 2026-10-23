#! /usr/bin/env python3
"""Unit tests for sdram_pack.py: the pack format the boot's decoder reads."""

import struct
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import sdram_pack

FIXTURES = (
    Path(__file__).resolve().parents[2] / "src/bsp/rust/tests/fixtures/sdram_pack"
)


class PackTests(unittest.TestCase):
    def test_round_trips_and_fills_the_reserve(self):
        raw = bytes(range(256)) * 40
        packed = sdram_pack.pack(raw, 4096)
        self.assertEqual(len(packed), 4096)
        self.assertEqual(sdram_pack.unpack(packed), raw)

    def test_header_is_raw_then_stream_length_little_endian(self):
        raw = b"\x00" * 1000
        packed = sdram_pack.pack(raw, 512)
        raw_len, stream_len = struct.unpack_from("<II", packed)
        self.assertEqual(raw_len, 1000)
        self.assertEqual(zlib.decompress(packed[8 : 8 + stream_len]), raw)
        self.assertEqual(packed[8 + stream_len :], bytes(512 - 8 - stream_len))

    def test_refuses_a_pack_past_the_reserve(self):
        with self.assertRaises(sdram_pack.PackDoesNotFit):
            sdram_pack.pack(bytes(range(256)), 64)

    def test_the_rust_fixture_is_a_pack_of_its_raw_bytes(self):
        # Not byte-compared with a fresh pack: zlib builds (zlib, zlib-ng) emit different streams for the same input.
        raw = (FIXTURES / "raw.bin").read_bytes()
        self.assertEqual(sdram_pack.unpack((FIXTURES / "pack.bin").read_bytes()), raw)


if __name__ == "__main__":
    unittest.main()
