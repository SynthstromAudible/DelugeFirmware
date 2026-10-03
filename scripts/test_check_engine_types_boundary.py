# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for check_engine_types_boundary.py: what the no-allowlist check must accept and reject."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from check_engine_types_boundary import violations


class EngineTypesBoundaryTest(unittest.TestCase):
    def check(self, text: str, name: str = "probe.h") -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "types"
            root.mkdir()
            (root / name).write_text(text)
            return violations(root)

    def test_accepts_standard_headers_and_its_own(self):
        text = '#pragma once\n#include <cstdint>\n#include <optional>\n#include "engine-api/types/sync.h"\n'
        self.assertEqual(self.check(text), [])

    def test_accepts_spacing_variants_of_valid_includes(self):
        self.assertEqual(
            self.check('#  include  <cstdint>\n  #include "engine-api/types/sync.h"\n'),
            [],
        )

    def test_rejects_an_app_header_in_quotes(self):
        self.assertEqual(len(self.check('#include "definitions_cxx.hpp"\n')), 1)

    def test_rejects_an_app_header_in_angle_brackets(self):
        self.assertEqual(len(self.check("#include <definitions_cxx.hpp>\n")), 1)

    def test_rejects_a_non_standard_slashless_angle_header(self):
        self.assertEqual(len(self.check("#include <definitions.h>\n")), 1)

    def test_rejects_path_traversal_out_of_the_layer(self):
        self.assertEqual(
            len(
                self.check(
                    '#include "engine-api/types/../../deluge/model/song/song.h"\n'
                )
            ),
            1,
        )

    def test_rejects_an_include_it_cannot_parse(self):
        self.assertEqual(len(self.check('#include /* c */ "model/song/song.h"\n')), 1)
        self.assertEqual(len(self.check("#define HDR <x.h>\n#include HDR\n")), 1)

    def test_scans_every_c_family_suffix(self):
        for suffix in (".inl", ".ipp", ".cc", ".cxx", ".hh", ".hxx", ".tcc"):
            with self.subTest(suffix=suffix):
                self.assertEqual(
                    len(
                        self.check(
                            '#include "definitions_cxx.hpp"\n', name="probe" + suffix
                        )
                    ),
                    1,
                )


if __name__ == "__main__":
    unittest.main()
