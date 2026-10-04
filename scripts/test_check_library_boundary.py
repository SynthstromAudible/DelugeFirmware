# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for check_library_boundary.py: what each library's no-allowlist check must accept and reject."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from check_library_boundary import LIBRARIES, Library, violations

ENGINE_TYPES = LIBRARIES["engine-types"]
WITH_DEPENDENCY = Library(
    root=Path("unused"),
    own_prefix="b/",
    may_include=("engine-api/types/", "foundation/panic.h"),
)


class LibraryBoundaryTest(unittest.TestCase):
    def check(
        self, text: str, name: str = "probe.h", library: Library = ENGINE_TYPES
    ) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "lib"
            root.mkdir()
            (root / name).write_text(text)
            return violations(library, root)

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

    def test_accepts_a_declared_dependency_by_prefix_or_exact_path(self):
        text = '#include "engine-api/types/sync.h"\n#include "foundation/panic.h"\n#include "b/x.h"\n'
        self.assertEqual(self.check(text, library=WITH_DEPENDENCY), [])

    def test_rejects_an_undeclared_neighbour_of_a_declared_dependency(self):
        self.assertEqual(
            len(
                self.check(
                    '#include "foundation/string_format.h"\n', library=WITH_DEPENDENCY
                )
            ),
            1,
        )
        self.assertEqual(
            len(
                self.check('#include "engine-api/other/x.h"\n', library=WITH_DEPENDENCY)
            ),
            1,
        )

    def test_rejects_traversal_through_a_declared_dependency(self):
        self.assertEqual(
            len(
                self.check(
                    '#include "engine-api/types/../../deluge/x.h"\n',
                    library=WITH_DEPENDENCY,
                )
            ),
            1,
        )

    def test_the_engine_api_may_use_the_document_tree_but_not_the_model(self):
        engine_api = LIBRARIES["engine-api"]
        self.assertEqual(
            self.check('#include "serialization/value.h"\n', library=engine_api), []
        )
        self.assertEqual(
            len(self.check('#include "deluge-project/song.h"\n', library=engine_api)), 1
        )

    def test_the_instrument_may_use_the_engine_api_but_not_the_app(self):
        instrument = LIBRARIES["instrument"]
        self.assertEqual(
            self.check('#include "engine-api/engine.h"\n', library=instrument), []
        )
        self.assertEqual(
            len(
                self.check('#include "processing/sound/sound.h"\n', library=instrument)
            ),
            1,
        )

    def test_the_global_effectable_table_may_use_only_the_engine_types(self):
        table = LIBRARIES["global-effectable"]
        self.assertEqual(
            self.check('#include "engine-api/types/param.h"\n', library=table), []
        )
        self.assertEqual(
            len(self.check('#include "engine-api/engine.h"\n', library=table)), 1
        )
        self.assertEqual(
            len(self.check('#include "engines/deluge/params.h"\n', library=table)), 1
        )


if __name__ == "__main__":
    unittest.main()
