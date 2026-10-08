#! /usr/bin/env python3
"""Unit tests for label_pr.py and the PR template / labeler / labels consistency.

Run directly with:
    python3 .github/scripts/test_label_pr.py
"""

import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

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


class UnclosedCommentTests(unittest.TestCase):
    def test_unclosed_comment_is_not_content(self):
        sections = lp.parse_sections("## Summary\n\n<!-- oops\n\n## What to test\nhi")
        self.assertEqual(sections["Summary"], "")


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


def pr_event(body_text, draft=False, user_type="User", labels=()):
    return {
        "action": "opened",
        "pull_request": {
            "number": 9,
            "body": body_text,
            "draft": draft,
            "user": {"login": "someone", "type": user_type},
            "labels": [{"name": name} for name in labels],
        },
    }


class MainTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_main(
        self, event, existing_comment=None, current_labels=None, run_override=None
    ):
        path = self.dir / "event.json"
        path.write_text(json.dumps(event))
        calls = []
        if current_labels is None:
            current_labels = [x["name"] for x in event["pull_request"]["labels"]]

        def run(args):
            if run_override:
                return run_override(args)
            if args[1].endswith("/labels") and "-X" not in args:
                return "".join(f"{name}\n" for name in current_labels)
            if "--paginate" in args:
                if existing_comment is None:
                    return ""
                return json.dumps({"id": 55, "body": existing_comment}) + "\n"
            calls.append(args)
            return ""

        code = lp.main(
            [],
            run=run,
            environ={"GITHUB_EVENT_PATH": str(path), "GITHUB_REPOSITORY": "o/r"},
            out=io.StringIO(),
        )
        return code, calls

    def test_complete_pr_gets_type_label_and_no_comment(self):
        code, calls = self.run_main(pr_event(body()))
        self.assertEqual(code, 0)
        self.assertEqual(
            calls,
            [
                [
                    "api",
                    "-X",
                    "POST",
                    "repos/o/r/issues/9/labels",
                    "-f",
                    "labels[]=type: fix",
                ]
            ],
        )

    def test_incomplete_pr_gets_one_comment(self):
        code, calls = self.run_main(pr_event(body(ticked=(), summary="")))
        self.assertEqual(code, 0)
        self.assertEqual(len(calls), 1)
        self.assertEqual(
            calls[0][:4], ["api", "-X", "POST", "repos/o/r/issues/9/comments"]
        )
        self.assertIn("body=" + lp.MARKER, calls[0][-1])

    def test_existing_comment_is_edited_not_duplicated(self):
        _, calls = self.run_main(
            pr_event(body(ticked=())), existing_comment=lp.comment_body(["old"])
        )
        self.assertEqual(len(calls), 1)
        self.assertEqual(
            calls[0][:4], ["api", "-X", "PATCH", "repos/o/r/issues/comments/55"]
        )

    def test_unchanged_comment_makes_no_write(self):
        found = lp.problems(lp.parse_sections(body(ticked=())), has_type_label=False)
        _, calls = self.run_main(
            pr_event(body(ticked=())), existing_comment=lp.comment_body(found)
        )
        self.assertEqual(calls, [])

    def test_resolved_pr_edits_existing_comment_to_thanks(self):
        _, calls = self.run_main(
            pr_event(body()), existing_comment=lp.comment_body(["old"])
        )
        patches = [c for c in calls if c[2] == "PATCH"]
        self.assertEqual(len(patches), 1)
        self.assertIn("complete", patches[0][-1])

    def test_resolved_pr_with_no_comment_stays_quiet(self):
        _, calls = self.run_main(pr_event(body(), labels=["type: fix"]))
        self.assertEqual(calls, [])

    def test_several_boxes_add_no_label(self):
        _, calls = self.run_main(
            pr_event(body(ticked=("Bug fix", "Chore (build, CI, docs, tooling)")))
        )
        self.assertFalse(
            any(
                "labels" in c[3] and c[2] == "POST" and c[3].endswith("/labels")
                for c in calls
            )
        )
        self.assertEqual(len(calls), 1)  # the comment

    def test_existing_type_label_is_never_duplicated_or_contradicted(self):
        _, calls = self.run_main(
            pr_event(body(ticked=("Bug fix",)), labels=["type: deps"])
        )
        self.assertEqual(calls, [])

    def test_null_body_makes_one_comment_and_no_label(self):
        code, calls = self.run_main(pr_event(None))
        self.assertEqual(code, 0)
        self.assertEqual(len(calls), 1)
        self.assertTrue(calls[0][3].endswith("/comments"))

    def test_drafts_and_bots_are_skipped(self):
        for event in (
            pr_event(body(), draft=True),
            pr_event(body(), user_type="Bot"),
            pr_event(None, user_type="Bot"),
        ):
            code, calls = self.run_main(event)
            self.assertEqual((code, calls), (0, []))

    def test_label_added_since_the_event_is_respected(self):
        _, calls = self.run_main(
            pr_event(body(ticked=("Bug fix",))), current_labels=["type: deps"]
        )
        self.assertEqual(calls, [])

    def test_only_the_bot_comment_is_ever_edited(self):
        seen = []

        def run_override(args):
            seen.append(args)
            return ""

        self.run_main(pr_event(body()), run_override=run_override)
        lookup = next(a for a in seen if "--paginate" in a)
        self.assertIn("github-actions[bot]", lookup[-1])

    def test_gh_failure_is_reported_but_never_fails_the_job(self):
        def run_override(args):
            raise subprocess.CalledProcessError(1, ["gh", *args], stderr="HTTP 403")

        code, _ = self.run_main(pr_event(body()), run_override=run_override)
        self.assertEqual(code, 0)

    def test_comment_does_not_echo_the_pr_body(self):
        hostile = "## Summary\n\n@everyone $(rm -rf /) `x`\n\n## Type of change\n\n- [ ] Bug fix\n"
        _, calls = self.run_main(pr_event(hostile))
        self.assertNotIn("@everyone", calls[-1][-1])
        self.assertNotIn("rm -rf", calls[-1][-1])


class ConfigConsistencyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.label_names = {
            e["name"] for e in yaml.safe_load((GITHUB_DIR / "labels.yml").read_text())
        }
        cls.labeler = yaml.safe_load((GITHUB_DIR / "labeler.yml").read_text())

    def test_type_checkbox_labels_exist(self):
        for label in lp.TYPE_LABELS.values():
            with self.subTest(label=label):
                self.assertIn(label, self.label_names)

    def test_labeler_only_applies_defined_area_labels(self):
        for label in self.labeler:
            with self.subTest(label=label):
                self.assertIn(label, self.label_names)
                self.assertTrue(label.startswith("area: "))

    def test_every_rule_is_a_nonempty_changed_files_glob_list(self):
        for label, rules in self.labeler.items():
            with self.subTest(label=label):
                self.assertEqual(len(rules), 1)
                globs = rules[0]["changed-files"][0]["any-glob-to-any-file"]
                self.assertTrue(globs)
                self.assertTrue(all(isinstance(glob, str) for glob in globs))

    def test_every_area_label_has_a_rule(self):
        areas = {name for name in self.label_names if name.startswith("area: ")}
        self.assertEqual(areas - set(self.labeler), set())


class WorkflowSafetyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = GITHUB_DIR / "workflows" / "label-prs.yml"
        cls.text = cls.path.read_text()
        cls.workflow = yaml.safe_load(cls.text)

    def test_privileged_job_never_checks_out_pr_code(self):
        self.assertNotIn("pull_request.head", self.text)
        for job in self.workflow["jobs"].values():
            for step in job["steps"]:
                if step.get("uses", "").startswith("actions/checkout"):
                    self.assertNotIn("ref", step.get("with", {}))

    def test_privileged_trigger_and_least_permissions(self):
        self.assertIn(
            "pull_request_target", self.workflow[True]
        )  # PyYAML parses `on:` as True
        permissions = self.workflow["jobs"]["label"]["permissions"]
        self.assertEqual(
            permissions,
            {"contents": "read", "pull-requests": "write"},
        )

    def test_privileged_checkout_does_not_persist_credentials(self):
        steps = self.workflow["jobs"]["label"]["steps"]
        checkout = next(s for s in steps if s["uses"].startswith("actions/checkout"))
        self.assertIs(checkout["with"]["persist-credentials"], False)

    def test_labeler_is_add_only(self):
        steps = self.workflow["jobs"]["label"]["steps"]
        labeler = next(
            s for s in steps if s.get("uses", "").startswith("actions/labeler")
        )
        self.assertFalse(labeler["with"]["sync-labels"])

    def test_tests_run_only_on_plain_pull_request(self):
        self.assertEqual(
            self.workflow["jobs"]["test"]["if"], "github.event_name == 'pull_request'"
        )
        self.assertEqual(
            self.workflow["jobs"]["label"]["if"],
            "github.event_name == 'pull_request_target'",
        )


if __name__ == "__main__":
    unittest.main()
