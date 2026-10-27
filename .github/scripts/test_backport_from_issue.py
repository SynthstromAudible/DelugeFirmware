#! /usr/bin/env python3
"""Unit tests for backport_from_issue.py and its label definitions.

Run directly with:
    python3 .github/scripts/test_backport_from_issue.py
"""

import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))

import backport_from_issue as bi

LABELS_FILE = Path(__file__).resolve().parents[1] / "labels.yml"


def graphql(*issues):
    nodes = [
        {"number": n, "labels": {"nodes": [{"name": l} for l in ls]}}
        for n, ls in issues
    ]
    return json.dumps(
        {
            "data": {
                "repository": {
                    "pullRequest": {"closingIssuesReferences": {"nodes": nodes}}
                }
            }
        }
    )


def run_main(pr_labels, issues):
    calls = []

    def run(args):
        calls.append(args)
        if args[:2] == ["api", "graphql"]:
            return graphql(*issues)
        if args[-2:] == ["--jq", ".[].name"]:
            return "\n".join(pr_labels)
        return ""

    with tempfile.TemporaryDirectory() as tmp:
        event = Path(tmp) / "event.json"
        event.write_text(json.dumps({"pull_request": {"number": 7}}))
        env = {"GITHUB_EVENT_PATH": str(event), "GITHUB_REPOSITORY": "o/r"}
        out = io.StringIO()
        self_status = bi.main([], run=run, environ=env, out=out)
    added = [c[-1] for c in calls if "POST" in c]
    return self_status, added


class BackportLabelsTests(unittest.TestCase):
    def test_maps_affects_to_backport(self):
        self.assertEqual(
            bi.backport_labels([["affects: release/1.3", "area: audio"], []]),
            ["backport: release/1.3"],
        )

    def test_deduplicates_across_issues(self):
        self.assertEqual(
            bi.backport_labels([["affects: release/1.3"], ["affects: release/1.3"]]),
            ["backport: release/1.3"],
        )

    def test_ignores_other_labels(self):
        self.assertEqual(
            bi.backport_labels([["affects: 1.3", "backport: release/1.3"]]), []
        )


class MainTests(unittest.TestCase):
    def test_adds_label_for_closing_pr(self):
        status, added = run_main([], [(1, ["affects: release/1.3"])])
        self.assertEqual((status, added), (0, ["labels[]=backport: release/1.3"]))

    def test_respects_declined(self):
        labels = ["backport: declined"]
        self.assertEqual(run_main(labels, [(1, ["affects: release/1.3"])])[1], [])

    def test_does_not_readd_existing(self):
        labels = ["backport: release/1.3"]
        self.assertEqual(run_main(labels, [(1, ["affects: release/1.3"])])[1], [])

    def test_no_closing_issues(self):
        self.assertEqual(run_main([], [])[1], [])


class LabelsTests(unittest.TestCase):
    def test_each_affects_label_has_a_backport_label(self):
        names = {e["name"] for e in yaml.safe_load(LABELS_FILE.read_text())}
        affects = {n for n in names if n.startswith("affects: ")}
        self.assertTrue(affects)
        for name in affects:
            with self.subTest(name=name):
                self.assertIn(name.replace("affects: ", "backport: "), names)


if __name__ == "__main__":
    unittest.main()
