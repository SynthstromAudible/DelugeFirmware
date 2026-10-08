#! /usr/bin/env python3
"""Structural checks for .github/labels.yml.

Run directly with:
    python3 .github/scripts/test_labels_yml.py
"""

import unittest
from pathlib import Path

import yaml

LABELS_FILE = Path(__file__).resolve().parents[1] / "labels.yml"

# Every label on the repo as of 2026-10-08, before migration.
CURRENT_LABELS = {
    "documentation", "duplicate", "enhancement", "good first issue", "help wanted",
    "question", "wontfix", "spam", "toolchain", "github workflow", "vscode",
    "refactor", "proof of concept", "merge conflict", "Code Review wanted",
    "ui-inconsistent", "performance", "ui-undesireable", "stale",
    "pr-needs manual merge", "cherry-pick", "no-repro", "pr-dependencies",
    "official", "crash", "ui-accessibility", "sys-browser", "ui-rendering",
    "python", "sound", "cherry-picked", "sys-automation", "sys-audio",
    "sys-sequencer", "sys-midi", "needs testing", "deferred", "small-pr",
    "website", "beta-blocker", "release-blocker", "dependencies", "javascript",
    "memory-corruption", "bug", "bugfix", "new-feature",
}  # fmt: skip

KEPT_UNPREFIXED = {
    "release-blocker",
    "beta-blocker",
    "good first issue",
    "help wanted",
    "cherry-pick",
    "cherry-picked",
}

REMOVED = {
    "sound",
    "new-feature",
    "proof of concept",
    "vscode",
    "python",
    "bug",
    "dependencies",
    "javascript",
    "spam",
    "question",
}

GROUP_COLORS = {
    "type": "5319e7",
    "area": "0075ca",
    "status": "fbca04",
    "impact": "d93f0b",
}


class LabelsFileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entries = yaml.safe_load(LABELS_FILE.read_text())
        cls.names = [e["name"] for e in cls.entries]
        cls.from_names = [e["from_name"] for e in cls.entries if "from_name" in e]

    def test_current_labels_fixture_is_complete(self):
        self.assertEqual(len(CURRENT_LABELS), 47)

    def test_entries_are_well_formed(self):
        for entry in self.entries:
            with self.subTest(entry=entry.get("name")):
                self.assertLessEqual(
                    set(entry), {"name", "color", "description", "from_name"}
                )
                self.assertRegex(entry["color"], r"^[0-9a-f]{6}$")
                self.assertTrue(entry["description"])
                self.assertLessEqual(len(entry["description"]), 100)

    def test_names_are_unique(self):
        self.assertEqual(len(self.names), len(set(self.names)))

    def test_from_names_are_unique_current_labels(self):
        self.assertEqual(len(self.from_names), len(set(self.from_names)))
        self.assertLessEqual(set(self.from_names), CURRENT_LABELS)

    def test_every_label_is_prefixed_or_kept(self):
        for name in self.names:
            with self.subTest(name=name):
                prefix = name.split(": ", 1)[0] if ": " in name else None
                self.assertTrue(prefix in GROUP_COLORS or name in KEPT_UNPREFIXED)

    def test_groups_share_one_color(self):
        for entry in self.entries:
            prefix = entry["name"].split(": ", 1)[0]
            if prefix in GROUP_COLORS:
                with self.subTest(name=entry["name"]):
                    self.assertEqual(entry["color"], GROUP_COLORS[prefix])

    def test_every_current_label_is_accounted_for_exactly_once(self):
        kept = set(self.names) & CURRENT_LABELS
        self.assertEqual(kept, KEPT_UNPREFIXED)
        renamed = set(self.from_names)
        for label in CURRENT_LABELS:
            with self.subTest(label=label):
                homes = [label in kept, label in renamed, label in REMOVED]
                self.assertEqual(homes.count(True), 1)

    def test_known_renames(self):
        renames = {e["from_name"]: e["name"] for e in self.entries if "from_name" in e}
        self.assertEqual(renames["sys-audio"], "area: audio")
        self.assertEqual(renames["enhancement"], "type: feature")
        self.assertEqual(renames["pr-dependencies"], "type: deps")
        self.assertEqual(renames["github workflow"], "area: ci")
        self.assertEqual(renames["ui-undesireable"], "impact: ux-undesirable")
        self.assertEqual(renames["Code Review wanted"], "status: needs-review")

    def test_new_labels_exist(self):
        for name in [
            "type: chore",
            "area: cv-gate",
            "area: files",
            "area: menus",
            "area: system",
            "status: needs-info",
        ]:
            with self.subTest(name=name):
                self.assertIn(name, self.names)

    def test_dependabot_uses_defined_labels(self):
        dependabot = yaml.safe_load((LABELS_FILE.parent / "dependabot.yml").read_text())
        for update in dependabot["updates"]:
            for label in update.get("labels", []):
                with self.subTest(ecosystem=update["package-ecosystem"], label=label):
                    self.assertIn(label, self.names)


if __name__ == "__main__":
    unittest.main()
