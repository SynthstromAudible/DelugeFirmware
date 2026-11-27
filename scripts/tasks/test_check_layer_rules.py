#! /usr/bin/env python3
"""Unit tests for check_layer_rules.py's scenes rows: the scenes name no Edit, no ModelStack and no engine object,
and read the model and the runtime only through `ui/scenes/bind`.

Run directly with:
    python3 scripts/tasks/test_check_layer_rules.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

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
                    "src/ui/scenes/menu/nodes/planted.cpp",
                    f"void f() {{\n\t{line}\n}}\n",
                )
                self.assertEqual(counts.get("scenes-no-engine-pointers"), 1, line)

    def test_a_comment_is_not(self):
        comment = self.counts(
            "src/ui/scenes/menu/nodes/planted.cpp",
            "/// Legacy's `Sound*` is not held here.\n",
        )
        self.assertNotIn("scenes-no-engine-pointers", comment)

    def test_no_scene_file_is_allowed_an_engine_pointer(self):
        plants = {
            "scenes-no-engine-pointers": "void f() {\n\tSound* sound = nullptr;\n}\n",
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

    def test_the_scenes_hold_none(self):
        found = {
            key: n
            for key, n in clr.current().items()
            if key[1] in ("scenes-no-engine-pointers",)
        }
        self.assertEqual(found, {})


class NoModControllable(unittest.TestCase):
    counts = ScenesRows.counts

    def test_the_untyped_owner_class_is_counted_anywhere(self):
        for rel in (
            "src/deluge/model/planted.h",
            "tests/integration/ui/scenes/planted_spec.cpp",
            "tests/integration/scenarios/planted.cpp",
        ):
            with self.subTest(rel=rel):
                counts = self.counts(
                    rel, "class Kit : public Instrument, public ModControllable {};\n"
                )
                self.assertEqual(counts.get("no-mod-controllable"), 1)

    def test_its_conversion_and_value_comparison_are_counted(self):
        counts = self.counts(
            "src/deluge/model/planted.cpp",
            "auto* o = output->toModControllable();\nbool b = o->valueChangedEnoughToMatter(1, 2, k, 3);\n",
        )
        self.assertEqual(counts.get("no-mod-controllable"), 2)

    def test_names_that_only_contain_it_are_not(self):
        counts = self.counts(
            "src/deluge/model/planted.cpp",
            "ModelStackWithModControllable* s = m->addModControllable(owner);\n"
            "view.setActiveModControllableTimelineCounter(clip);\n",
        )
        self.assertNotIn("no-mod-controllable", counts)


class NoNewModelStackInParams(unittest.TestCase):
    counts = ScenesRows.counts

    def test_each_stack_name_in_the_param_path_is_counted(self):
        counts = self.counts(
            "src/deluge/modulation/params/planted.cpp",
            "void f(ModelStackWithAutoParam const* a, ModelStackWithParamCollection* b);\nModelStack* c;\n",
        )
        self.assertEqual(counts.get("no-new-model-stack-in-params"), 2)

    def test_outside_the_param_path_they_are_not(self):
        counts = self.counts(
            "src/deluge/model/planted.cpp", "ModelStackWithAutoParam const* a;\n"
        )
        self.assertNotIn("no-new-model-stack-in-params", counts)

    def test_the_delay_hook_and_the_move_queue_are_counted(self):
        for rel in (
            "src/deluge/model/mod_controllable/delay_type.cpp",
            "src/deluge/playback/move_section.h",
        ):
            with self.subTest(rel=rel):
                counts = self.counts(rel, "ModelStackWithAutoParam stack;\n")
                self.assertEqual(counts.get("no-new-model-stack-in-params"), 1)

    def test_its_include_is_not(self):
        counts = self.counts(
            "src/deluge/modulation/params/planted.cpp",
            '#include "model/model_stack.h"\n',
        )
        self.assertNotIn("no-new-model-stack-in-params", counts)


class LegacyEffectsOwnerRows(unittest.TestCase):
    counts = ScenesRows.counts

    def test_each_tree_naming_a_deleted_owner_class_is_counted(self):
        for rel in (
            "src/deluge/model/fx/planted.cpp",
            "tests/integration/ui/scenes/planted_spec.cpp",
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
            "src/host/chains/planted.cpp",
            "namespace deluge::host::chains { int x = plugin::kGlobalEffectable; }\n",
        )
        self.assertNotIn("no-legacy-effects-owners", counts)

    def test_the_param_table_library_by_its_old_name_is_counted(self):
        for rel in (
            "src/deluge/model/fx/planted.cpp",
            "tests/integration/ui/scenes/planted_spec.cpp",
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


class LegacyPortSeams(unittest.TestCase):
    def test_no_legacy_file_is_a_knob_focus_seam(self):
        """The views keep the gold knobs' focus as addresses: no legacy file builds one for the scenes."""
        self.assertEqual([p for p in clr.LEGACY_PORT_SEAMS if "knob_focus" in p], [])


class ScenesLinkApp(unittest.TestCase):
    """`scenes-link-app`: no CMake file under src/ui/scenes names the app's target outside a comment."""

    def audit(self, text: str) -> int:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "src/ui/scenes/CMakeLists.txt"
            path.parent.mkdir(parents=True)
            path.write_text(text)
            saved = clr.REPO
            clr.REPO = root
            try:
                return clr.audit_scenes_link()
            finally:
                clr.REPO = saved

    def test_a_link_to_the_app_fails(self):
        self.assertEqual(
            self.audit(
                "target_link_libraries(deluge_ui_scenes PUBLIC $<COMPILE_ONLY:deluge_app>)\n"
            ),
            1,
        )

    def test_a_comment_naming_it_does_not(self):
        self.assertEqual(
            self.audit(
                "# deluge_ui_scenes links no part of deluge_app\nadd_library(x STATIC a.cpp)\n"
            ),
            0,
        )

    def test_a_longer_name_holding_it_does_not(self):
        self.assertEqual(
            self.audit("target_link_libraries(x PUBLIC deluge_app_gui_shim)\n"), 0
        )


if __name__ == "__main__":
    unittest.main()
