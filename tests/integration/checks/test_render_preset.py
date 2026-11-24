# Copyright © 2026 Synthstrom Audible Limited
#
# This file is part of The Synthstrom Audible Deluge Firmware.
# Licensed under the GNU General Public License v3 or later; see LICENSE.
"""Tests for render_preset.py's staging: a preset's own MIDI learn is removed and the preset stays valid.

Gate render-preset-selftest:
    the golden preset render's staging removes a JSON preset's own MIDI learn and leaves the preset valid JSON, wherever
    the member stands; each render-song gate plays its own generated song, whatever the shell exports; a render-song
    gate that switches mod FX types, clears mod FX memory or switches the reverb's model or knobs fails unless the
    scenario's summary shows each applied as asked and reaching its path; a filtered update keeps every record it did not
    render, and a whole-corpus update drops the record of a case no longer in the corpus; each case's render
    allocations are echoed in the gate's log under its name
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts" / "tasks"))

from render_preset import (
    allocation_echoes,
    records_to_write,
    without_learned_input_json,
)


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


OLD_RECORDS = {
    "sample/a": {"sha256": "1"},
    "sample/b": {"sha256": "2"},
    "sample/gone": {"sha256": "3"},
}


class RecordsToWriteTest(unittest.TestCase):
    def test_a_filtered_update_keeps_the_records_it_did_not_render(self):
        written = records_to_write(
            True, ["sample/a"], OLD_RECORDS, {"sample/a": {"sha256": "9"}}, ["sample/a"]
        )
        self.assertEqual(written, {**OLD_RECORDS, "sample/a": {"sha256": "9"}})

    def test_a_whole_corpus_update_drops_the_record_of_a_deleted_case(self):
        written = records_to_write(
            True,
            [],
            OLD_RECORDS,
            {"sample/a": {"sha256": "9"}, "sample/new": {"sha256": "4"}},
            ["sample/a", "sample/b", "sample/new"],
        )
        self.assertEqual(
            written,
            {
                "sample/a": {"sha256": "9"},
                "sample/b": {"sha256": "2"},
                "sample/new": {"sha256": "4"},
            },
        )

    def test_a_comparing_run_writes_nothing(self):
        self.assertIsNone(records_to_write(False, [], OLD_RECORDS, {}, ["sample/a"]))


class AllocationEchoTest(unittest.TestCase):
    def test_each_case_s_allocations_are_echoed(self):
        runs = [
            ("a", "RP-ALLOC render_allocations=2"),
            ("b", "RP-ALLOC render_allocations=4"),
        ]
        self.assertEqual(
            allocation_echoes(runs),
            ["RP-ALLOC a render_allocations=2", "RP-ALLOC b render_allocations=4"],
        )

    def test_a_case_that_printed_no_count_is_left_out(self):
        self.assertEqual(allocation_echoes([("a", None)]), [])


if __name__ == "__main__":
    unittest.main()
