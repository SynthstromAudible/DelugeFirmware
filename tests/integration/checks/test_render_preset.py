# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for render_preset.py's staging: a preset's own MIDI learn is removed and the preset stays valid.

Gate render-preset-selftest:
    the golden preset render's staging removes a JSON preset's own MIDI learn and leaves the preset valid JSON, wherever
    the member stands; each render-song gate plays its own generated song, whatever the shell exports; a render-song
    gate that switches mod FX types, clears mod FX memory or switches the reverb's model or knobs fails unless the
    scenario's summary shows each applied as asked and reaching its path; a whole-corpus update drops the record of a
    case no longer in the corpus
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts" / "tasks"))

from render_preset import kept_records, without_learned_input_json


class LearnedInputJsonTest(unittest.TestCase):
    def test_a_member_before_others_is_removed_with_its_comma(self):
        text = '{"sound": {"inputMidiChannel": 1, "mode": "subtractive"}}'
        self.assertEqual(
            json.loads(without_learned_input_json(text)),
            {"sound": {"mode": "subtractive"}},
        )

    def test_the_last_member_is_removed_with_the_comma_before_it(self):
        text = '{"sound": {"mode": "subtractive",\n\t"inputMidiChannel": 1\n}}'
        self.assertEqual(
            json.loads(without_learned_input_json(text)),
            {"sound": {"mode": "subtractive"}},
        )

    def test_the_only_member_leaves_an_empty_object(self):
        text = '{"sound": {"inputMidiChannel": 1}}'
        self.assertEqual(json.loads(without_learned_input_json(text)), {"sound": {}})


class KeptRecordsTest(unittest.TestCase):
    def test_a_whole_corpus_update_drops_the_record_of_a_deleted_case(self):
        records = {"sample/a": {"sha256": "1"}, "sample/gone": {"sha256": "2"}}
        self.assertEqual(
            kept_records(records, ["sample/a", "sample/new"]),
            {"sample/a": {"sha256": "1"}},
        )


if __name__ == "__main__":
    unittest.main()
