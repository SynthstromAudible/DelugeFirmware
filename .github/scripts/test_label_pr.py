#! /usr/bin/env python3
"""Unit tests for label_pr.py and the PR template / labeler / labels consistency.

Run directly with:
    python3 .github/scripts/test_label_pr.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import label_pr as lp

GITHUB_DIR = Path(__file__).resolve().parents[1]
TEMPLATE = GITHUB_DIR / "PULL_REQUEST_TEMPLATE.md"


def body(
    summary="Fixes the thing", ticked=("Bug fix",), test="Play a clip", crlf=False
):
    boxes = "\n".join(
        f"- [{'x' if label in ticked else ' '}] {label}" for label in lp.TYPE_LABELS
    )
    text = (
        f"## Summary\n\n<!-- placeholder -->\n{summary}\n\n"
        f"## Type of change\n\n<!-- Tick one -->\n\n{boxes}\n\n"
        f"## What to test\n\n<!-- placeholder -->\n{test}\n\n"
        "## Checklist\n\n- [ ] I tested this on hardware\n"
    )
    return text.replace("\n", "\r\n") if crlf else text


class ParseSectionsTests(unittest.TestCase):
    def test_parses_sections_and_strips_comments(self):
        sections = lp.parse_sections(body())
        self.assertEqual(sections[lp.SUMMARY], "Fixes the thing")
        self.assertEqual(sections[lp.TEST_SECTION], "Play a clip")

    def test_placeholder_only_sections_are_empty(self):
        sections = lp.parse_sections(body(summary="", test=""))
        self.assertEqual(sections[lp.SUMMARY], "")
        self.assertEqual(sections[lp.TEST_SECTION], "")

    def test_multiline_comment_is_stripped(self):
        sections = lp.parse_sections("## Summary\n\n<!-- a\nb -->\ntext")
        self.assertEqual(sections[lp.SUMMARY], "text")

    def test_crlf(self):
        sections = lp.parse_sections(body(crlf=True))
        self.assertEqual(sections[lp.SUMMARY], "Fixes the thing")
        self.assertEqual(lp.checked_types(sections), ["type: fix"])

    def test_missing_and_empty_bodies(self):
        self.assertEqual(lp.parse_sections(None), {})
        self.assertEqual(lp.parse_sections(""), {})
        self.assertEqual(lp.parse_sections("just text"), {})


class CheckedTypesTests(unittest.TestCase):
    def test_one_box(self):
        self.assertEqual(
            lp.checked_types(
                lp.parse_sections(body(ticked=("Refactor (no behavior change)",)))
            ),
            ["type: refactor"],
        )

    def test_no_boxes(self):
        self.assertEqual(lp.checked_types(lp.parse_sections(body(ticked=()))), [])

    def test_several_boxes_in_template_order(self):
        sections = lp.parse_sections(
            body(ticked=("Chore (build, CI, docs, tooling)", "Bug fix"))
        )
        self.assertEqual(lp.checked_types(sections), ["type: fix", "type: chore"])

    def test_uppercase_x_and_unknown_boxes(self):
        sections = {lp.TYPE_SECTION: "- [X] Bug fix\n- [x] Something else"}
        self.assertEqual(lp.checked_types(sections), ["type: fix"])


class ProblemsTests(unittest.TestCase):
    def test_complete_description_has_no_problems(self):
        self.assertEqual(
            lp.problems(lp.parse_sections(body()), has_type_label=True), []
        )

    def test_missing_type(self):
        found = lp.problems(lp.parse_sections(body(ticked=())), has_type_label=False)
        self.assertEqual(len(found), 1)
        self.assertIn("Type of change", found[0])

    def test_several_types(self):
        found = lp.problems(
            lp.parse_sections(
                body(ticked=("Bug fix", "Chore (build, CI, docs, tooling)"))
            ),
            has_type_label=False,
        )
        self.assertEqual(len(found), 1)
        self.assertIn("more than one", found[0].lower())

    def test_existing_type_label_satisfies_the_type_requirement(self):
        self.assertEqual(
            lp.problems(lp.parse_sections(body(ticked=())), has_type_label=True), []
        )

    def test_empty_sections(self):
        found = lp.problems(
            lp.parse_sections(body(summary="", test="")), has_type_label=True
        )
        self.assertEqual(len(found), 2)

    def test_no_template_at_all_reports_everything(self):
        self.assertEqual(len(lp.problems({}, has_type_label=False)), 3)


class CommentBodyTests(unittest.TestCase):
    def test_marker_is_first_line(self):
        self.assertTrue(lp.comment_body(["x"]).startswith(lp.MARKER + "\n"))
        self.assertTrue(lp.comment_body([]).startswith(lp.MARKER + "\n"))

    def test_lists_each_problem(self):
        text = lp.comment_body(["first thing", "second thing"])
        self.assertIn("- first thing", text)
        self.assertIn("- second thing", text)

    def test_resolved_message(self):
        self.assertIn("complete", lp.comment_body([]))


class TemplateTests(unittest.TestCase):
    def test_template_has_every_section_and_type_box(self):
        text = TEMPLATE.read_text()
        sections = lp.parse_sections(text)
        for heading in (lp.SUMMARY, lp.TYPE_SECTION, lp.TEST_SECTION, "Checklist"):
            self.assertIn(heading, sections)
        for label in lp.TYPE_LABELS:
            self.assertIn(f"- [ ] {label}", text)

    def test_fresh_template_has_nothing_ticked_and_empty_sections(self):
        sections = lp.parse_sections(TEMPLATE.read_text())
        self.assertEqual(lp.checked_types(sections), [])
        self.assertEqual(sections[lp.SUMMARY], "")
        self.assertEqual(sections[lp.TEST_SECTION], "")


if __name__ == "__main__":
    unittest.main()
