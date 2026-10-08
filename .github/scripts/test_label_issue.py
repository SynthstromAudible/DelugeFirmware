#! /usr/bin/env python3
"""Unit tests for label_issue.py and the issue form / mapping / labels consistency.

Run directly with:
    python3 .github/scripts/test_label_issue.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import label_issue as li

GITHUB_DIR = Path(__file__).resolve().parents[1]
MAPPING_FILE = GITHUB_DIR / "issue-form-labels.yml"
MAPPING = li.load_mapping(MAPPING_FILE.read_text())

AREA = "Where does it happen?"
IMPACT = "How does it show up?"


def form_body(area="Sound / synth engine", impact="Crash or freeze", crlf=False):
    body = (
        "### Please describe the problem:\n\nIt broke\n\n"
        "### What is the expected behavior?\n\nIt works\n\n"
        f"### {AREA}\n\n{area}\n\n"
        f"### {IMPACT}\n\n{impact}\n\n"
        "### If possible provide the steps\n\n_No response_"
    )
    return body.replace("\n", "\r\n") if crlf else body


class ParseFormAnswersTests(unittest.TestCase):
    def test_parses_headings_and_answers(self):
        answers = li.parse_form_answers(form_body())
        self.assertEqual(answers[AREA], "Sound / synth engine")
        self.assertEqual(answers[IMPACT], "Crash or freeze")
        self.assertEqual(answers["If possible provide the steps"], "_No response_")

    def test_multiline_answers_are_kept(self):
        answers = li.parse_form_answers("### A\n\nline one\nline two\n\n### B\n\nx")
        self.assertEqual(answers["A"], "line one\nline two")

    def test_crlf_line_endings(self):
        answers = li.parse_form_answers(form_body(crlf=True))
        self.assertEqual(answers[AREA], "Sound / synth engine")
        self.assertEqual(answers[IMPACT], "Crash or freeze")

    def test_empty_and_missing_bodies(self):
        self.assertEqual(li.parse_form_answers(None), {})
        self.assertEqual(li.parse_form_answers(""), {})
        self.assertEqual(li.parse_form_answers("just some free text"), {})

    def test_first_heading_wins(self):
        answers = li.parse_form_answers("### A\n\nreal\n\n### A\n\nspoofed")
        self.assertEqual(answers["A"], "real")


class LabelsForTests(unittest.TestCase):
    def test_maps_both_fields(self):
        answers = li.parse_form_answers(form_body())
        self.assertEqual(
            li.labels_for(answers, MAPPING), ["area: audio", "impact: crash"]
        )

    def test_not_sure_and_no_response_apply_nothing(self):
        answers = li.parse_form_answers(
            form_body(area="Other / not sure", impact="_No response_")
        )
        self.assertEqual(li.labels_for(answers, MAPPING), [])

    def test_one_field_still_applies_when_other_is_unanswered(self):
        answers = li.parse_form_answers(form_body(area="MIDI", impact="_No response_"))
        self.assertEqual(li.labels_for(answers, MAPPING), ["area: midi"])

    def test_blank_issue_applies_nothing(self):
        self.assertEqual(li.labels_for(li.parse_form_answers("free text"), MAPPING), [])

    def test_unknown_answers_apply_nothing(self):
        answers = li.parse_form_answers(
            form_body(area="bug; rm -rf /", impact="`$(whoami)`")
        )
        self.assertEqual(li.labels_for(answers, MAPPING), [])

    def test_hostile_body_can_only_yield_mapped_labels(self):
        body = f"### Please describe the problem:\n\n### {AREA}\n\nMIDI\n\n### evil\n\n--label admin"
        allowed = {
            label for options in MAPPING.values() for label in options.values() if label
        }
        self.assertLessEqual(
            set(li.labels_for(li.parse_form_answers(body), MAPPING)), allowed
        )

    def test_duplicates_are_removed(self):
        mapping = {"A": {"x": "area: audio"}, "B": {"y": "area: audio"}}
        self.assertEqual(li.labels_for({"A": "x", "B": "y"}, mapping), ["area: audio"])


class LoadMappingTests(unittest.TestCase):
    def test_null_means_no_label(self):
        mapping = li.load_mapping(
            '"F":\n  "Other / not sure": null\n  "A": "area: ui"\n'
        )
        self.assertEqual(mapping, {"F": {"Other / not sure": None, "A": "area: ui"}})


if __name__ == "__main__":
    unittest.main()
