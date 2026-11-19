#! /usr/bin/env python3
"""Unit tests for check_layer_rules.py's scenes rows: the scenes name no Edit, no ModelStack and no engine object,
and read the model and the runtime only through `scenes/bind`.

Run directly with:
    python3 scripts/tasks/test_check_layer_rules.py
"""

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import check_layer_rules as clr


class ScenesRows(unittest.TestCase):
    def counts(self, rel: str, text: str) -> dict[str, int]:
        """Each rule's count for @p text planted at @p rel in a tree of its own."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / rel
            path.parent.mkdir(parents=True)
            path.write_text(text)
            saved = clr.REPO
            clr.REPO = root
            try:
                return clr.count_file(path)
            finally:
                clr.REPO = saved

    def test_a_scene_including_an_edit_is_counted(self):
        counts = self.counts(
            "src/scenes/menu/nodes/planted.cpp",
            '#include "model/edits/setting_edit.h"\n',
        )
        self.assertEqual(counts.get("scenes-no-edits"), 1)

    def test_another_edit_in_the_context_is_counted(self):
        counts = self.counts(
            "src/scenes/menu/context.cpp", '#include "model/edits/setting_edit.h"\n'
        )
        self.assertEqual(counts.get("scenes-no-edits"), 1)

    def test_a_scene_naming_an_engine_object_is_counted(self):
        for line in (
            "Sound* sound = nullptr;",
            "ParamManagerForTimeline* params = nullptr;",
            "auto* clip = static_cast<InstrumentClip*>(c);",
            "ArpeggiatorSettings *arp = nullptr;",
            "const MultiRange* range = r;",
        ):
            with self.subTest(line=line):
                counts = self.counts(
                    "src/scenes/menu/nodes/planted.cpp", f"void f() {{\n\t{line}\n}}\n"
                )
                self.assertEqual(counts.get("scenes-no-engine-pointers"), 1, line)

    def test_a_comment_is_not(self):
        comment = self.counts(
            "src/scenes/menu/nodes/planted.cpp",
            "/// Legacy's `Sound*` is not held here.\n",
        )
        self.assertNotIn("scenes-no-engine-pointers", comment)

    def test_no_scene_file_is_allowed_an_engine_pointer_a_model_stack_or_an_edit(self):
        plants = {
            "scenes-no-engine-pointers": "void f() {\n\tSound* sound = nullptr;\n}\n",
            "scenes-model-stack": '#include "modulation/automation/auto_param.h"\n',
            "scenes-model-stack-outside-context": '#include "model/model_stack.h"\n',
            "scenes-no-edits": '#include "model/edits/param_address_for.h"\n',
        }
        for rel in (
            "src/scenes/menu/context.cpp",
            "src/scenes/menu/context.h",
            "src/scenes/menu/bindings.cpp",
            "src/scenes/overlay/param_popup_scene.h",
            "src/scenes/overlay/param_popup_scene.cpp",
            "src/scenes/services/mod_knobs.cpp",
            "src/scenes/services/mod_knobs.h",
            "src/scenes/services/mod_target.h",
        ):
            for rule, text in plants.items():
                with self.subTest(rel=rel, rule=rule):
                    self.assertEqual(self.counts(rel, text).get(rule), 1)

    def test_the_scenes_hold_none(self):
        found = {
            key: n
            for key, n in clr.current().items()
            if key[1]
            in (
                "scenes-no-edits",
                "scenes-no-engine-pointers",
                "scenes-model-stack",
                "scenes-model-stack-outside-context",
            )
        }
        self.assertEqual(found, {})


class ModControllableBridgeOnly(unittest.TestCase):
    counts = ScenesRows.counts

    def test_an_instrument_event_on_the_bridge_class_is_counted(self):
        counts = self.counts(
            "src/deluge/model/mod_controllable/mod_controllable.h",
            "class ModControllable {\n"
            "  virtual bool allowNoteTails(void* s) { return true; }\n"
            "  virtual void monophonicExpressionEvent(int v, int d) {}\n"
            "};\n",
        )
        self.assertEqual(counts.get("mod-controllable-bridge-only"), 2)

    def test_the_bridge_alone_is_not(self):
        counts = self.counts(
            "src/deluge/model/mod_controllable/mod_controllable.h",
            "class ModControllable {\n  virtual std::optional<EffectsOwner> effectsOwner();\n};\n",
        )
        self.assertNotIn("mod-controllable-bridge-only", counts)

    def test_the_instruments_declaring_the_events_are_not(self):
        counts = self.counts(
            "src/deluge/model/instrument/melodic_instrument.h",
            "virtual void monophonicExpressionEvent(int v, int d) = 0;\n",
        )
        self.assertNotIn("mod-controllable-bridge-only", counts)

    def test_the_old_comparison_name_is_counted_anywhere(self):
        for rel in (
            "src/deluge/model/planted.cpp",
            "sim/scenes_spec/planted_spec.cpp",
            "tests/integration/scenarios/planted.cpp",
        ):
            with self.subTest(rel=rel):
                counts = self.counts(
                    rel, "bool b = o->valueChangedEnoughToMatter(1, 2, k, 3);\n"
                )
                self.assertEqual(counts.get("mod-controllable-bridge-only"), 1)


class LegacyEffectsOwnerRows(unittest.TestCase):
    counts = ScenesRows.counts

    def test_each_tree_naming_a_deleted_owner_class_is_counted(self):
        for rel in (
            "src/deluge/model/fx/planted.cpp",
            "sim/scenes_spec/planted_spec.cpp",
            "tests/integration/scenarios/planted.cpp",
        ):
            with self.subTest(rel=rel):
                counts = self.counts(
                    rel,
                    "class ModControllableAudio;\nGlobalEffectableForClip* g = nullptr;\n",
                )
                self.assertEqual(counts.get("no-legacy-effects-owners"), 2)

    def test_a_comment_citing_one_is_not(self):
        counts = self.counts(
            "src/deluge/model/planted.cpp",
            "// main's GlobalEffectable::modButtonAction ends the stutter\n",
        )
        self.assertNotIn("no-legacy-effects-owners", counts)

    def test_the_param_table_library_is_not(self):
        counts = self.counts(
            "src/effects/host_chain/planted.cpp",
            "namespace deluge::host_chain { int x = plugin::kGlobalEffectable; }\n",
        )
        self.assertNotIn("no-legacy-effects-owners", counts)

    def test_the_param_table_library_by_its_old_name_is_counted(self):
        for rel in (
            "src/deluge/model/fx/planted.cpp",
            "sim/scenes_spec/planted_spec.cpp",
            "tests/unit/effects/planted_spec.cpp",
        ):
            with self.subTest(rel=rel):
                # An include of the old path can't build (the directory is gone); a symbol row skips include lines.
                counts = self.counts(
                    rel, "auto x = deluge::global_effectable::kParams;\n"
                )
                self.assertEqual(counts.get("no-global-effectable-table"), 1)

    def test_a_longer_name_holding_it_is_not(self):
        counts = self.counts(
            "src/deluge/bench/planted.h", "void f(int global_effectable_audio);\n"
        )
        self.assertNotIn("no-global-effectable-table", counts)

    def test_the_trees_hold_none(self):
        found = {
            key: n
            for key, n in clr.current().items()
            if key[1] == "no-legacy-effects-owners"
        }
        self.assertEqual(found, {})


class ViewKnobRows(unittest.TestCase):
    counts = ScenesRows.counts

    def test_a_legacy_file_naming_the_knob_stack_is_counted(self):
        counts = self.counts(
            "src/deluge/gui/views/planted.cpp",
            "void f() {\n\t(void)view.activeModControllableModelStack.modControllable;\n}\n",
        )
        self.assertEqual(counts.get("views-no-knob-stack"), 1)

    def test_the_tree_holds_neither(self):
        found = {
            key: n
            for key, n in clr.current().items()
            if key[1] == "views-no-knob-stack"
        }
        self.assertEqual(found, {})


class ScenesReadsOutsideBind(unittest.TestCase):
    counts = ScenesRows.counts

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
                    counts = self.counts("src/scenes/menu/nodes/planted.cpp", spelling)
                    self.assertEqual(counts.get("scenes-reads-outside-bind"), 1)

    def test_every_include_counts(self):
        counts = self.counts(
            "src/scenes/services/planted.h",
            '#include "model/song/song.h"\n'
            '#include "modulation/sync.h"\n'
            '#include "processing/dx7/dx_patch.h"\n',
        )
        self.assertEqual(counts.get("scenes-reads-outside-bind"), 3)

    def test_bind_is_not_counted(self):
        counts = self.counts(
            "src/scenes/bind/planted.cpp", '#include "model/song/song.h"\n'
        )
        self.assertNotIn("scenes-reads-outside-bind", counts)

    def test_other_families_are_not_counted(self):
        counts = self.counts(
            "src/scenes/menu/planted.cpp",
            '#include "scenes/bind/settings.h"\n'
            '#include "gui/l10n/strings.h"\n'
            '#include "deluge-project/project.h"\n'
            '#include "ui/oled/columns.h"\n',
        )
        self.assertNotIn("scenes-reads-outside-bind", counts)

    def run_main(self, root: Path, *args: str) -> int:
        """`check_layer_rules.main` with @p args over the tree at @p root and its baseline."""
        saved = clr.REPO, clr.BASELINE
        clr.REPO, clr.BASELINE = root, root / "baseline.txt"
        try:
            with (
                mock.patch.object(sys, "argv", ["check_layer_rules.py", *args]),
                contextlib.redirect_stdout(io.StringIO()),
                contextlib.redirect_stderr(io.StringIO()),
            ):
                return clr.main()
        finally:
            clr.REPO, clr.BASELINE = saved

    def plant(self, root: Path, includes: int, baseline: int) -> None:
        scene = root / "src/scenes/menu/planted.cpp"
        scene.parent.mkdir(parents=True)
        scene.write_text('#include "model/song/song.h"\n' * includes)
        (root / "baseline.txt").write_text(
            "# header\n"
            f"src/scenes/menu/planted.cpp\tscenes-reads-outside-bind\t{baseline}\n"
        )

    def test_a_count_above_the_baseline_fails_the_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.plant(root, includes=2, baseline=1)
            self.assertEqual(self.run_main(root), 1)

    def test_update_refuses_to_raise_a_count(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.plant(root, includes=2, baseline=1)
            before = (root / "baseline.txt").read_text()
            self.assertEqual(self.run_main(root, "--update"), 1)
            self.assertEqual((root / "baseline.txt").read_text(), before)

    def test_update_lowers_a_count(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.plant(root, includes=1, baseline=2)
            self.assertEqual(self.run_main(root, "--update"), 0)
            self.assertIn(
                "src/scenes/menu/planted.cpp\tscenes-reads-outside-bind\t1\n",
                (root / "baseline.txt").read_text(),
            )


class LegacyPortSeams(unittest.TestCase):
    def test_no_legacy_file_is_a_knob_focus_seam(self):
        """The views keep the gold knobs' focus as addresses: no legacy file builds one for the scenes."""
        self.assertEqual([p for p in clr.LEGACY_PORT_SEAMS if "knob_focus" in p], [])


if __name__ == "__main__":
    unittest.main()
