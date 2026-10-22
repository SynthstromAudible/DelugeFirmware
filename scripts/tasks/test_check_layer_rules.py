#! /usr/bin/env python3
"""Unit tests for check_layer_rules.py's scenes rows: the scenes name no Edit and no engine object.

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

    def test_a_scene_including_an_edit_is_counted(self):
        counts = self.counts(
            "src/scenes/menu/nodes/planted.cpp",
            '#include "model/edits/setting_edit.h"\n',
        )
        self.assertEqual(counts.get("scenes-no-edits"), 1)

    def test_the_gold_knob_s_address_in_the_context_is_not(self):
        counts = self.counts(
            "src/scenes/menu/context.cpp",
            '#include "model/edits/param_address_for.h"\n',
        )
        self.assertNotIn("scenes-no-edits", counts)

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

    def test_a_comment_and_an_allowed_file_are_not(self):
        comment = self.counts(
            "src/scenes/menu/nodes/planted.cpp",
            "/// Legacy's `Sound*` is not held here.\n",
        )
        self.assertNotIn("scenes-no-engine-pointers", comment)
        for rel in (
            "src/scenes/menu/context.cpp",
            "src/scenes/menu/context.h",
            "src/scenes/overlay/param_popup_scene.h",
            "src/scenes/services/mod_knobs.cpp",
            "src/scenes/services/mod_target.h",
        ):
            with self.subTest(rel=rel):
                self.assertNotIn(
                    "scenes-no-engine-pointers",
                    self.counts(rel, "Sound* sound = nullptr;\n"),
                )

    def test_the_scenes_hold_neither(self):
        found = {
            key: n
            for key, n in clr.current().items()
            if key[1] in ("scenes-no-edits", "scenes-no-engine-pointers")
        }
        self.assertEqual(found, {})


class ParamBackupRows(unittest.TestCase):
    counts = ScenesRows.counts

    def test_a_backup_call_keyed_by_a_cast_is_counted(self):
        for line in (
            "song->live.param_backup.getPreferablyWithClip((ModControllableAudio*)o, nullptr);",
            "song->live.param_backup.backUp(static_cast<ModControllableAudio*>(o->toModControllable()), c, &p);",
        ):
            with self.subTest(line=line):
                counts = self.counts(
                    "src/deluge/model/planted.cpp", f"void f() {{\n\t{line}\n}}\n"
                )
                self.assertEqual(counts.get("param-backup-by-address"), 1, line)

    def test_the_old_key_shape_is_counted(self):
        counts = self.counts(
            "src/deluge/model/project_bridge/param_backup.h",
            "deluge::fast_map<std::pair<ModControllableAudio*, Clip*>, ParamManager> map;\n",
        )
        self.assertEqual(counts.get("param-backup-by-address"), 1)

    def test_a_typed_owner_is_not(self):
        counts = self.counts(
            "src/deluge/model/planted.cpp",
            "void f() {\n\tsong->live.param_backup.backUp(*output, nullptr, &p);\n}\n",
        )
        self.assertNotIn("param-backup-by-address", counts)

    def test_the_tree_holds_neither(self):
        found = {
            key: n
            for key, n in clr.current().items()
            if key[1] == "param-backup-by-address"
        }
        self.assertEqual(found, {})


if __name__ == "__main__":
    unittest.main()
