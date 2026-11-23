# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for check_layers.py: what the table's libraries, rules and tree checks must accept and reject.

Gate layers-selftest:
    the include-layer checker rejects every way an undeclared header could slip into a library, counts each rule's
    includes as the old ratchets did, follows a transitive rule through intermediate headers, and refuses a new
    top-level directory, a stray root file, a shadowed include path and a growing legacy tree
"""

from __future__ import annotations

import tempfile
import textwrap
import unittest
from pathlib import Path

import check_layers as cl

TABLE = textwrap.dedent(
    """
    [tree]
    include_roots = ["src/deluge", "src"]
    legacy = "src/deluge"
    scan_roots = ["src", "tests"]
    root_files = ["CMakeLists.txt"]

    [[directory]]
    path = "lib"
    target = "src/lib"

    [[directory]]
    path = "deluge"
    target = "deleted"

    [[library]]
    name = "lib"
    root = "src/lib"
    prefix = "lib/"
    may_include = ["dep/", "other/one.h"]
    system = ["stdint.h"]

    [[library]]
    name = "lib-inner"
    root = "src/lib/inner"
    prefix = "lib/inner/"
    """
)


class Repo:
    """A throwaway repository: a table, and files planted at repo-relative paths."""

    def __init__(self, table: str = TABLE):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        (self.root / "scripts").mkdir()
        (self.root / "scripts" / "layers.toml").write_text(table)
        (self.root / "src").mkdir()

    def plant(self, rel: str, text: str) -> Path:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        cl.read_lines.cache_clear()
        return path

    def config(self) -> cl.Config:
        return cl.load_config(self.root / "scripts" / "layers.toml")

    def close(self) -> None:
        self._tmp.cleanup()


class LibraryCheck(unittest.TestCase):
    def setUp(self):
        self.repo = Repo()

    def tearDown(self):
        self.repo.close()

    def found(self, text: str, rel: str = "src/lib/probe.h") -> list[str]:
        """The violations in @p text, planted at @p rel (other planted files' violations are left out)."""
        self.repo.plant(rel, text)
        return [
            v
            for v in cl.library_violations(self.repo.config(), self.repo.root)
            if f" {rel}:" in v
        ]

    def test_accepts_standard_headers_its_own_and_its_dependencies(self):
        text = '#pragma once\n#include <cstdint>\n#include <optional>\n#include "lib/x.h"\n#include "dep/y.h"\n#include "other/one.h"\n'
        self.assertEqual(self.found(text), [])

    def test_accepts_spacing_variants(self):
        self.assertEqual(
            self.found('#  include  <cstdint>\n  #include "lib/x.h"\n'), []
        )

    def test_accepts_a_listed_system_header(self):
        self.assertEqual(self.found("#include <stdint.h>\n"), [])

    def test_rejects_an_app_header_in_quotes_or_angle_brackets(self):
        self.assertEqual(len(self.found('#include "definitions_cxx.hpp"\n')), 1)
        self.assertEqual(len(self.found("#include <definitions_cxx.hpp>\n")), 1)

    def test_rejects_an_unlisted_non_standard_angle_header(self):
        self.assertEqual(len(self.found("#include <etl/string.h>\n")), 1)
        self.assertEqual(len(self.found("#include <stdbool.h>\n")), 1)

    def test_rejects_path_traversal(self):
        self.assertEqual(
            len(self.found('#include "dep/../../deluge/model/song/song.h"\n')), 1
        )
        self.assertEqual(len(self.found('#include "../other/x.h"\n')), 1)

    def test_rejects_an_include_it_cannot_parse(self):
        self.assertEqual(len(self.found('#include /* c */ "lib/x.h"\n')), 1)
        self.assertEqual(len(self.found("#define HDR <x.h>\n#include HDR\n")), 1)

    def test_rejects_an_undeclared_neighbour_of_a_declared_dependency(self):
        self.assertEqual(len(self.found('#include "other/two.h"\n')), 1)
        self.assertEqual(len(self.found('#include "depth/x.h"\n')), 1)

    def test_scans_every_c_family_suffix(self):
        for suffix in (
            ".c",
            ".cc",
            ".cpp",
            ".cxx",
            ".h",
            ".hh",
            ".hpp",
            ".hxx",
            ".inc",
            ".inl",
            ".ipp",
            ".tcc",
        ):
            with self.subTest(suffix=suffix):
                self.assertEqual(
                    len(
                        self.found(
                            '#include "definitions_cxx.hpp"\n',
                            rel=f"src/lib/probe{suffix}",
                        )
                    ),
                    1,
                )

    def test_a_relative_include_beside_the_file_is_its_own(self):
        self.repo.plant("src/lib/grid/rgb.h", "#pragma once\n")
        self.assertEqual(
            self.found('#include "rgb.h"\n', rel="src/lib/grid/palette.h"), []
        )

    def test_a_relative_spelling_of_a_missing_file_is_read_as_written(self):
        self.assertEqual(len(self.found('#include "model/song/song.h"\n')), 1)

    def test_a_file_belongs_to_the_deepest_library(self):
        found = self.found('#include "dep/y.h"\n', rel="src/lib/inner/probe.h")
        self.assertEqual(len(found), 1)
        self.assertTrue(found[0].startswith("lib-inner: src/lib/inner/probe.h:1:"))

    def test_a_violation_names_the_library_the_file_and_the_line(self):
        self.assertEqual(
            self.found('#pragma once\n#include "model/song/song.h"\n'),
            ['lib: src/lib/probe.h:2: #include "model/song/song.h"'],
        )


class TableLoading(unittest.TestCase):
    def test_a_name_used_twice_is_refused(self):
        repo = Repo(
            TABLE + '\n[[library]]\nname = "lib"\nroot = "src/x"\nprefix = "x/"\n'
        )
        try:
            with self.assertRaises(ValueError):
                repo.config()
        finally:
            repo.close()

    def test_the_directories_come_from_the_directory_entries(self):
        repo = Repo()
        try:
            self.assertEqual(repo.config().tree.directories, ("lib", "deluge"))
        finally:
            repo.close()


RULES = textwrap.dedent(
    """
    [[directory]]
    path = "ui"
    target = "src/ui"

    [[rule]]
    name = "scenes-reads-outside-bind"
    files = ["src/ui/scenes/**"]
    exclude = ["src/ui/scenes/bind/**"]
    pattern = '^(model|modulation|io|processing|storage|playback)/'
    shrink_only = true
    reason = "scenes read the model through bind"

    [[rule]]
    name = "legacy-includes-ui"
    files = ["src/deluge/{model,io}/**"]
    pattern = '^(gui/(?!l10n/)|hid/)'
    reason = "the model raises notifications"

    [[rule]]
    name = "dsp-boundary"
    files = ["src/dsp/**"]
    pattern = '^(model|memory|util)/'
    allow = ["memory/memory_allocator_interface.h", "util/misc.h"]
    reason = "the kernels reach nothing above them"
    """
)


class CountedRules(unittest.TestCase):
    def setUp(self):
        self.repo = Repo(TABLE + RULES)

    def tearDown(self):
        self.repo.close()

    def counts(self) -> dict[tuple[str, str], int]:
        return cl.rule_counts(self.repo.config(), self.repo.root)

    def test_each_matching_include_counts_in_either_spelling(self):
        self.repo.plant(
            "src/ui/scenes/menu/planted.cpp",
            '#include "model/song/song.h"\n#include <storage/flash_storage.h>\n#include "modulation/sync.h"\n',
        )
        self.assertEqual(
            self.counts(),
            {("src/ui/scenes/menu/planted.cpp", "scenes-reads-outside-bind"): 3},
        )

    def test_an_excluded_glob_is_not_counted(self):
        self.repo.plant(
            "src/ui/scenes/bind/planted.cpp", '#include "model/song/song.h"\n'
        )
        self.assertEqual(self.counts(), {})

    def test_other_families_are_not_counted(self):
        self.repo.plant(
            "src/ui/scenes/menu/planted.cpp",
            '#include "ui/scenes/bind/settings.h"\n#include "gui/l10n/strings.h"\n#include "ui/widgets/oled/columns.h"\n',
        )
        self.assertEqual(self.counts(), {})

    def test_braces_expand_in_a_glob(self):
        self.repo.plant(
            "src/deluge/io/midi/planted.cpp",
            '#include "gui/ui/ui.h"\n#include "gui/l10n/strings.h"\n',
        )
        self.repo.plant("src/deluge/storage/planted.cpp", '#include "gui/ui/ui.h"\n')
        self.assertEqual(
            self.counts(), {("src/deluge/io/midi/planted.cpp", "legacy-includes-ui"): 1}
        )

    def test_an_allowed_header_is_not_counted(self):
        self.repo.plant(
            "src/dsp/core/planted.h",
            '#include "util/misc.h"\n#include "memory/memory_allocator_interface.h"\n#include "util/functions.h"\n',
        )
        self.assertEqual(self.counts(), {("src/dsp/core/planted.h", "dsp-boundary"): 1})

    def test_a_commented_out_include_is_not_counted(self):
        self.repo.plant(
            "src/ui/scenes/menu/planted.cpp", '// #include "model/song/song.h"\n'
        )
        self.assertEqual(self.counts(), {})


class CommandLine(unittest.TestCase):
    def setUp(self):
        self.repo = Repo(TABLE + RULES)
        self.baseline = self.repo.root / "scripts" / "layers_baseline.txt"

    def tearDown(self):
        self.repo.close()

    def run_main(self, *args: str) -> int:
        import contextlib
        import io

        with (
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            return cl.main(["--repo", str(self.repo.root), *args])

    def plant(self, includes: int, baseline: int) -> None:
        self.repo.plant(
            "src/ui/scenes/menu/planted.cpp",
            '#include "model/song/song.h"\n' * includes,
        )
        self.baseline.write_text(
            f"# header\nsrc/ui/scenes/menu/planted.cpp\tscenes-reads-outside-bind\t{baseline}\n"
        )

    def test_a_count_at_the_baseline_passes(self):
        self.plant(includes=1, baseline=1)
        self.assertEqual(self.run_main(), 0)

    def test_a_count_above_the_baseline_fails(self):
        self.plant(includes=2, baseline=1)
        self.assertEqual(self.run_main(), 1)

    def test_a_count_with_no_baseline_row_fails(self):
        self.repo.plant("src/deluge/io/planted.cpp", '#include "hid/x.h"\n')
        self.baseline.write_text("# header\n")
        self.assertEqual(self.run_main(), 1)

    def test_a_library_violation_fails(self):
        self.repo.plant("src/lib/probe.h", '#include "model/song/song.h"\n')
        self.baseline.write_text("# header\n")
        self.assertEqual(self.run_main(), 1)

    def test_update_refuses_to_raise_a_shrink_only_count(self):
        self.plant(includes=2, baseline=1)
        before = self.baseline.read_text()
        self.assertEqual(self.run_main("--update"), 1)
        self.assertEqual(self.baseline.read_text(), before)

    def test_update_lowers_a_count(self):
        self.plant(includes=1, baseline=2)
        self.assertEqual(self.run_main("--update"), 0)
        self.assertIn(
            "src/ui/scenes/menu/planted.cpp\tscenes-reads-outside-bind\t1\n",
            self.baseline.read_text(),
        )

    def test_update_may_raise_a_count_that_is_not_shrink_only(self):
        self.repo.plant(
            "src/deluge/io/planted.cpp", '#include "hid/x.h"\n#include "hid/y.h"\n'
        )
        self.baseline.write_text(
            "# header\nsrc/deluge/io/planted.cpp\tlegacy-includes-ui\t1\nsrc/deluge\tlegacy-shrinks\t1\n"
        )
        self.assertEqual(self.run_main("--update"), 0)
        self.assertIn(
            "src/deluge/io/planted.cpp\tlegacy-includes-ui\t2\n",
            self.baseline.read_text(),
        )

    def test_update_writes_a_first_baseline_whatever_the_counts(self):
        self.repo.plant(
            "src/ui/scenes/menu/planted.cpp", '#include "model/song/song.h"\n'
        )
        self.assertEqual(self.run_main("--update"), 0)
        self.assertIn(
            "src/ui/scenes/menu/planted.cpp\tscenes-reads-outside-bind\t1\n",
            self.baseline.read_text(),
        )

    def test_list_prints_the_counts(self):
        import contextlib
        import io

        self.plant(includes=2, baseline=0)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(cl.main(["--repo", str(self.repo.root), "--list"]), 0)
        self.assertIn(
            "src/ui/scenes/menu/planted.cpp\tscenes-reads-outside-bind\t2\n",
            out.getvalue(),
        )


SYNTH_RULE = textwrap.dedent(
    """
    [[directory]]
    path = "synth"
    target = "src/synth"

    [[rule]]
    name = "synth-boundary"
    files = ["src/synth/**"]
    pattern = '^(model|gui|util|project)/'
    allow = ["util/misc.h", "project/drum_type.h"]
    transitive = true
    reason = "the synth reaches no app layer, directly or through another header"
    """
)


class TransitiveRules(unittest.TestCase):
    def setUp(self):
        self.repo = Repo(TABLE + SYNTH_RULE)

    def tearDown(self):
        self.repo.close()

    def reached(self, rel: str) -> dict[str, list[str]]:
        config = self.repo.config()
        rule = config.rules[0]
        return cl.reaches(config, self.repo.root, rule, self.repo.root / rel)

    def test_a_header_reached_through_an_intermediate_is_counted_with_its_chain(self):
        self.repo.plant(
            "src/deluge/util/functions.h", '#include "gui/colour/colour.h"\n'
        )
        self.repo.plant("src/deluge/gui/colour/colour.h", "#pragma once\n")
        self.repo.plant("src/synth/voice/unison.h", '#include "util/functions.h"\n')
        self.assertEqual(
            self.reached("src/synth/voice/unison.h"),
            {"util/functions.h": ["util/functions.h"]},
        )

    def test_an_allowed_header_is_followed_and_what_it_reaches_counts(self):
        self.repo.plant("src/deluge/util/misc.h", '#include "model/song/song.h"\n')
        self.repo.plant("src/deluge/model/song/song.h", "#pragma once\n")
        self.repo.plant("src/synth/voice/unison.h", '#include "util/misc.h"\n')
        self.assertEqual(
            self.reached("src/synth/voice/unison.h"),
            {"model/song/song.h": ["util/misc.h", "model/song/song.h"]},
        )

    def test_a_clean_chain_through_the_synth_counts_nothing(self):
        self.repo.plant("src/synth/a.h", '#include "synth/b.h"\n')
        self.repo.plant(
            "src/synth/b.h",
            '#include "util/misc.h"\n#include "project/drum_type.h"\n',
        )
        self.repo.plant("src/deluge/util/misc.h", "#pragma once\n")
        self.repo.plant("src/project/drum_type.h", "#pragma once\n")
        self.assertEqual(self.reached("src/synth/a.h"), {})

    def test_an_unresolved_forbidden_include_is_counted_as_written(self):
        self.repo.plant("src/synth/a.h", '#include "gui/missing.h"\n')
        self.assertEqual(
            self.reached("src/synth/a.h"), {"gui/missing.h": ["gui/missing.h"]}
        )

    def test_an_include_cycle_terminates(self):
        self.repo.plant("src/synth/a.h", '#include "synth/b.h"\n')
        self.repo.plant("src/synth/b.h", '#include "synth/a.h"\n')
        self.assertEqual(self.reached("src/synth/a.h"), {})

    def test_the_count_is_the_number_of_headers_reached(self):
        self.repo.plant("src/synth/a.h", '#include "gui/x.h"\n#include "model/y.h"\n')
        self.assertEqual(
            cl.rule_counts(self.repo.config(), self.repo.root),
            {("src/synth/a.h", "synth-boundary"): 2},
        )

    def test_a_failure_prints_the_chain(self):
        import contextlib
        import io

        self.repo.plant(
            "src/deluge/util/functions.h", '#include "gui/colour/colour.h"\n'
        )
        self.repo.plant("src/synth/voice/unison.h", '#include "synth/voice/x.h"\n')
        self.repo.plant("src/synth/voice/x.h", '#include "util/functions.h"\n')
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(cl.main(["--repo", str(self.repo.root)]), 1)
        self.assertIn(
            "util/functions.h   (via synth/voice/x.h -> util/functions.h)",
            out.getvalue(),
        )


class TreeChecks(unittest.TestCase):
    def setUp(self):
        self.repo = Repo()
        self.repo.plant("src/CMakeLists.txt", "")
        self.repo.plant("src/lib/a.h", "#pragma once\n")
        self.repo.plant("src/deluge/model/song.h", "#pragma once\n")

    def tearDown(self):
        self.repo.close()

    def found(self) -> list[str]:
        return cl.tree_violations(self.repo.config(), self.repo.root)

    def test_the_listed_tree_passes(self):
        self.assertEqual(self.found(), [])

    def test_an_unlisted_directory_fails(self):
        self.repo.plant("src/newlib/x.h", "")
        self.assertEqual(
            self.found(),
            ["tree: src/newlib/ is a directory scripts/layers.toml does not list"],
        )

    def test_an_unlisted_root_file_fails(self):
        self.repo.plant("src/stray.h", "")
        self.assertEqual(
            self.found(),
            [
                "tree: src/stray.h is a file at the src/ root that scripts/layers.toml does not list"
            ],
        )

    def test_a_path_under_both_roots_fails(self):
        self.repo.plant("src/model/song.h", "")
        found = self.found()
        self.assertIn(
            "tree: src/model/song.h and src/deluge/model/song.h are both reached by an include of model/song.h",
            found,
        )

    def test_a_build_file_at_the_same_path_is_not_a_shadow(self):
        self.repo.plant("src/deluge/CMakeLists.txt", "")
        self.assertEqual(self.found(), [])

    def test_a_different_file_in_a_same_named_directory_passes(self):
        self.repo.plant("src/lib/model/other.h", "")
        self.assertEqual(self.found(), [])


class LegacyRatchet(unittest.TestCase):
    def setUp(self):
        self.repo = Repo()
        self.repo.plant("src/deluge/a.cpp", "")
        self.repo.plant("src/deluge/model/b.h", "")
        self.repo.plant("src/deluge/README.md", "")
        self.baseline = self.repo.root / "scripts" / "layers_baseline.txt"

    def tearDown(self):
        self.repo.close()

    def run_main(self, *args: str) -> int:
        import contextlib
        import io

        with (
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            return cl.main(["--repo", str(self.repo.root), *args])

    def test_the_count_is_the_source_files(self):
        self.assertEqual(cl.legacy_count(self.repo.config(), self.repo.root), 2)

    def test_a_new_legacy_file_fails(self):
        self.baseline.write_text("# header\nsrc/deluge\tlegacy-shrinks\t2\n")
        self.assertEqual(self.run_main(), 0)
        self.repo.plant("src/deluge/model/c.cpp", "")
        self.assertEqual(self.run_main(), 1)

    def test_update_may_lower_it_but_not_raise_it(self):
        self.baseline.write_text("# header\nsrc/deluge\tlegacy-shrinks\t1\n")
        self.assertEqual(self.run_main("--update"), 1)
        self.baseline.write_text("# header\nsrc/deluge\tlegacy-shrinks\t3\n")
        self.assertEqual(self.run_main("--update"), 0)
        self.assertIn("src/deluge\tlegacy-shrinks\t2\n", self.baseline.read_text())


REAL_TABLE = Path(__file__).resolve().parent / "layers.toml"


class RealTable(unittest.TestCase):
    """The real table's libraries and rules, planted into a throwaway tree."""

    def setUp(self):
        self.repo = Repo(REAL_TABLE.read_text())

    def tearDown(self):
        self.repo.close()

    def library_found(
        self, library: str, text: str, name: str = "probe.h"
    ) -> list[str]:
        config = self.repo.config()
        root = next(entry.root for entry in config.libraries if entry.name == library)
        self.repo.plant(f"{root}/{name}", text)
        return [
            v
            for v in cl.library_violations(config, self.repo.root)
            if f" {root}/{name}:" in v
        ]

    def counts(self, rel: str, text: str) -> dict[str, int]:
        self.repo.plant(rel, text)
        return {
            rule: n
            for (file, rule), n in cl.rule_counts(
                self.repo.config(), self.repo.root
            ).items()
            if file == rel
        }

    def test_the_effects_library_may_use_the_l0_dsp_library(self):
        accepted = '#include "dsp/core/stereo_sample.h"\n#include "dsp/core/detail/raw_ops.h"\n'
        self.assertEqual(self.library_found("effects", accepted), [])
        self.assertEqual(
            len(self.library_found("effects", '#include "util/functions.h"\n')), 1
        )
        self.assertEqual(
            len(
                self.library_found(
                    "effects", '#include "dsp_legacy/timestretch/time_stretcher.h"\n'
                )
            ),
            1,
        )

    def test_base_may_use_the_standard_library_and_its_own_headers_only(self):
        accepted = (
            '#include <stdint.h>\n#include "base/panic.h"\n#include "board_config.h"\n'
        )
        self.assertEqual(self.library_found("base", accepted), [])
        self.assertEqual(
            len(self.library_found("base", '#include "model/song.h"\n')), 1
        )

    def test_the_plugin_api_may_use_the_document_tree_but_not_the_model(self):
        self.assertEqual(
            self.library_found("plugin", '#include "serialization/value.h"\n'), []
        )
        self.assertEqual(
            len(self.library_found("plugin", '#include "project/song.h"\n')),
            1,
        )

    def test_controls_may_use_the_model_and_plugin_api_but_not_instruments(self):
        self.assertEqual(
            self.library_found("ui-controls", '#include "project/song.h"\n'), []
        )
        self.assertEqual(
            self.library_found("ui-controls", '#include "plugin/plugin.h"\n'), []
        )
        self.assertEqual(
            len(
                self.library_found(
                    "ui-controls", '#include "instruments/classic/words.h"\n'
                )
            ),
            1,
        )

    def test_the_instrument_may_use_the_plugin_api_but_not_the_app(self):
        self.assertEqual(
            self.library_found("instruments-classic", '#include "plugin/plugin.h"\n'),
            [],
        )
        self.assertEqual(
            len(
                self.library_found(
                    "instruments-classic", '#include "processing/sound/sound.h"\n'
                )
            ),
            1,
        )

    def test_the_host_chain_table_may_use_only_the_plugin_types(self):
        self.assertEqual(
            self.library_found("host-chains", '#include "plugin/types/param.h"\n'),
            [],
        )
        self.assertEqual(
            len(self.library_found("host-chains", '#include "plugin/plugin.h"\n')), 1
        )
        self.assertEqual(
            len(
                self.library_found(
                    "host-chains", '#include "instruments/classic/params.h"\n'
                )
            ),
            1,
        )

    def test_gfx_oled_and_grid_never_include_each_other(self):
        self.assertEqual(
            len(self.library_found("ui-gfx-oled", '#include "ui/gfx/grid/rgb.h"\n')), 1
        )
        self.assertEqual(
            len(self.library_found("ui-gfx-grid", '#include "ui/gfx/oled/canvas.h"\n')),
            1,
        )
        self.assertEqual(
            self.library_found("ui-gfx-grid", '#include "ui/gfx/contract.h"\n'), []
        )

    def test_a_scene_including_an_edit_is_counted(self):
        for rel in (
            "src/ui/scenes/menu/nodes/planted.cpp",
            "src/ui/scenes/menu/context.cpp",
        ):
            with self.subTest(rel=rel):
                self.assertEqual(
                    self.counts(rel, '#include "model/edits/setting_edit.h"\n').get(
                        "scenes-no-edits"
                    ),
                    1,
                )

    def test_no_scene_file_is_allowed_a_model_stack_or_an_edit(self):
        plants = {
            "scenes-model-stack": '#include "modulation/automation/auto_param.h"\n',
            "scenes-model-stack-outside-context": '#include "model/model_stack.h"\n',
            "scenes-no-edits": '#include "model/edits/param_address_for.h"\n',
        }
        for rel in (
            "src/ui/scenes/menu/context.cpp",
            "src/ui/scenes/menu/context.h",
            "src/ui/scenes/menu/bindings.cpp",
            "src/ui/scenes/overlay/param_popup_scene.h",
            "src/ui/scenes/overlay/param_popup_scene.cpp",
            "src/ui/scenes/services/mod_knobs.cpp",
            "src/ui/scenes/services/mod_knobs.h",
            "src/ui/scenes/services/mod_target.h",
        ):
            for rule, text in plants.items():
                with self.subTest(rel=rel, rule=rule):
                    self.assertEqual(self.counts(rel, text).get(rule), 1)

    def test_each_family_is_counted_outside_bind(self):
        for header in (
            "model/song/song.h",
            "modulation/params/param.h",
            "io/midi/midi_device.h",
            "processing/engines/audio_engine.h",
            "storage/flash_storage.h",
            "playback/playback_handler.h",
        ):
            for spelling in (f'#include "{header}"\n', f"#include <{header}>\n"):
                with self.subTest(spelling=spelling):
                    counts = self.counts(
                        "src/ui/scenes/menu/nodes/planted.cpp", spelling
                    )
                    self.assertEqual(counts.get("scenes-reads-outside-bind"), 1)

    def test_bind_reads_are_not_counted(self):
        self.assertNotIn(
            "scenes-reads-outside-bind",
            self.counts(
                "src/ui/scenes/bind/planted.cpp", '#include "model/song/song.h"\n'
            ),
        )


class TheTree(unittest.TestCase):
    """The repository as it stands passes its own table."""

    def test_the_scenes_hold_no_model_stack_and_no_edit(self):
        counts = cl.rule_counts(cl.load_config(REAL_TABLE), cl.REPO)
        held = {
            key: n
            for key, n in counts.items()
            if key[1]
            in (
                "scenes-no-edits",
                "scenes-model-stack",
                "scenes-model-stack-outside-context",
            )
        }
        self.assertEqual(held, {})

    def test_the_tree_passes(self):
        import contextlib
        import io

        with (
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            self.assertEqual(cl.main([]), 0)


if __name__ == "__main__":
    unittest.main()
