#! /usr/bin/env python3
"""Unit tests for label_issue.py and the issue form / mapping / labels consistency.

Run directly with:
    python3 .github/scripts/test_label_issue.py
"""

import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

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

    def test_last_heading_wins(self):
        answers = li.parse_form_answers("### A\n\nspoofed\n\n### A\n\nreal")
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

    def test_spoofed_headings_in_free_text_lose_to_the_real_answers(self):
        spoof = f"### {AREA}\n\nMIDI\n\n### {IMPACT}\n\nSlow or laggy"
        body = (
            f"### Please describe the problem:\n\n{spoof}\n\n"
            f"### {AREA}\n\nMenus\n\n### {IMPACT}\n\n_No response_"
        )
        self.assertEqual(
            li.labels_for(li.parse_form_answers(body), MAPPING), ["area: menus"]
        )

    def test_issue_without_every_form_field_is_not_labeled(self):
        body = f"some text\n\n### {AREA}\n\nMIDI"
        self.assertEqual(li.labels_for(li.parse_form_answers(body), MAPPING), [])

    def test_duplicates_are_removed(self):
        mapping = {"A": {"x": "area: audio"}, "B": {"y": "area: audio"}}
        self.assertEqual(li.labels_for({"A": "x", "B": "y"}, mapping), ["area: audio"])


class LoadMappingTests(unittest.TestCase):
    def test_null_means_no_label(self):
        mapping = li.load_mapping(
            '"F":\n  "Other / not sure": null\n  "A": "area: ui"\n'
        )
        self.assertEqual(mapping, {"F": {"Other / not sure": None, "A": "area: ui"}})


class LabelCommandTests(unittest.TestCase):
    def test_builds_one_request_with_all_labels(self):
        self.assertEqual(
            li.label_command("o/r", 7, ["area: audio", "impact: crash"]),
            [
                "api",
                "-X",
                "POST",
                "repos/o/r/issues/7/labels",
                "-f",
                "labels[]=area: audio",
                "-f",
                "labels[]=impact: crash",
            ],
        )


class MainTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_main(self, body):
        event = self.dir / "event.json"
        event.write_text(
            json.dumps({"action": "opened", "issue": {"number": 7, "body": body}})
        )
        calls = []
        environ = {"GITHUB_EVENT_PATH": str(event), "GITHUB_REPOSITORY": "o/r"}
        out = io.StringIO()
        code = li.main(
            ["--mapping-file", str(MAPPING_FILE)],
            run=lambda args: calls.append(args) or "",
            environ=environ,
            out=out,
        )
        return code, calls, out.getvalue()

    def test_applies_mapped_labels(self):
        code, calls, _ = self.run_main(form_body())
        self.assertEqual(code, 0)
        self.assertEqual(
            calls, [li.label_command("o/r", 7, ["area: audio", "impact: crash"])]
        )

    def test_null_body_makes_no_call(self):
        code, calls, out = self.run_main(None)
        self.assertEqual(code, 0)
        self.assertEqual(calls, [])
        self.assertIn("no labels", out)

    def test_not_sure_makes_no_call(self):
        code, calls, _ = self.run_main(
            form_body(area="Other / not sure", impact="Other / not sure")
        )
        self.assertEqual((code, calls), (0, []))

    def test_hostile_answer_is_passed_as_data_not_a_shell_command(self):
        _, calls, _ = self.run_main(
            form_body(area="MIDI; rm -rf /", impact="Crash or freeze")
        )
        self.assertEqual(calls, [li.label_command("o/r", 7, ["impact: crash"])])


class FormConsistencyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.form = yaml.safe_load(
            (GITHUB_DIR / "ISSUE_TEMPLATE" / "bug_report.yml").read_text()
        )
        cls.dropdowns = {
            block["attributes"]["label"]: block
            for block in cls.form["body"]
            if block["type"] == "dropdown"
        }
        cls.label_names = {
            entry["name"]
            for entry in yaml.safe_load((GITHUB_DIR / "labels.yml").read_text())
        }

    def test_form_sets_issue_type_bug(self):
        self.assertEqual(self.form["type"], "Bug")

    def test_every_mapped_field_is_an_optional_single_select_dropdown(self):
        for field in MAPPING:
            with self.subTest(field=field):
                self.assertIn(field, self.dropdowns)
                block = self.dropdowns[field]
                self.assertFalse(block["validations"]["required"])
                self.assertFalse(block["attributes"].get("multiple", False))

    def test_mapping_options_match_form_options_exactly(self):
        for field, options in MAPPING.items():
            with self.subTest(field=field):
                self.assertEqual(
                    list(options), self.dropdowns[field]["attributes"]["options"]
                )

    def test_mapped_labels_exist_in_labels_yml(self):
        for field, options in MAPPING.items():
            for option, label in options.items():
                if label:
                    with self.subTest(field=field, option=option):
                        self.assertIn(label, self.label_names)

    def test_duplicate_markdown_templates_stay_deleted(self):
        for name in ["bug_report.md", "issue_report.md"]:
            with self.subTest(name=name):
                self.assertFalse((GITHUB_DIR / "ISSUE_TEMPLATE" / name).exists())

    def test_mapped_dropdowns_are_the_last_blocks_so_free_text_cannot_follow_them(self):
        tail = [
            block["attributes"]["label"] for block in self.form["body"][-len(MAPPING) :]
        ]
        self.assertEqual(tail, list(MAPPING))


if __name__ == "__main__":
    unittest.main()
