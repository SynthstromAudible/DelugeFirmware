#! /usr/bin/env python3
"""Unit tests for migrate_labels.py.

Run directly with:
    python3 .github/scripts/test_migrate_labels.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import migrate_labels as ml
from test_labels_yml import CURRENT_LABELS, KEPT_UNPREFIXED, LABELS_FILE

RENAMES = ml.load_renames(LABELS_FILE.read_text())


def issue(number, *labels, issue_type=None):
    return ml.Item("issue", number, frozenset(labels), issue_type)


def pr(number, *labels):
    return ml.Item("pr", number, frozenset(labels))


class TaxonomyConsistencyTests(unittest.TestCase):
    def test_delete_set_is_exactly_the_labels_without_a_home(self):
        self.assertEqual(CURRENT_LABELS - set(RENAMES) - KEPT_UNPREFIXED, ml.DELETE)

    def test_merge_targets_are_renamed_labels(self):
        for target in ml.MERGES.values():
            self.assertIn(target, RENAMES)

    def test_issue_type_labels_are_rename_targets_or_dropped(self):
        for label in ml.ISSUE_TYPE_LABELS:
            self.assertTrue(label in RENAMES.values() or label in ml.DROPPED, label)


class LoadRenamesTests(unittest.TestCase):
    def test_reads_from_name_pairs_only(self):
        text = '- name: "a: x"\n  color: "000000"\n  description: "d"\n  from_name: "x"\n- name: "b"\n  color: "000000"\n  description: "d"\n'
        self.assertEqual(ml.load_renames(text), {"x": "a: x"})


class RenameConflictTests(unittest.TestCase):
    def test_conflict_when_old_and_new_both_exist(self):
        conflicts = ml.rename_conflicts(
            {"sys-audio": "area: audio"}, {"sys-audio", "area: audio"}
        )
        self.assertEqual(len(conflicts), 1)
        self.assertIn("area: audio", conflicts[0])

    def test_no_conflict_before_or_after_rename(self):
        self.assertEqual(
            ml.rename_conflicts({"sys-audio": "area: audio"}, {"sys-audio"}), []
        )
        self.assertEqual(
            ml.rename_conflicts({"sys-audio": "area: audio"}, {"area: audio"}), []
        )


class PlanPreTests(unittest.TestCase):
    def test_adds_primary_for_secondary(self):
        self.assertEqual(
            ml.plan_pre([issue(1, "sound")]), [ml.AddLabel(1, "sys-audio")]
        )

    def test_skips_when_primary_present(self):
        self.assertEqual(ml.plan_pre([issue(1, "sound", "sys-audio")]), [])

    def test_two_secondaries_same_primary_add_once(self):
        self.assertEqual(
            ml.plan_pre([pr(2, "vscode", "python")]), [ml.AddLabel(2, "toolchain")]
        )

    def test_unrelated_labels_untouched(self):
        self.assertEqual(ml.plan_pre([issue(3, "crash"), pr(4)]), [])


class UnmergedTests(unittest.TestCase):
    def test_flags_secondary_without_target_under_either_name(self):
        self.assertEqual(ml.unmerged([issue(1, "sound")], RENAMES), [issue(1, "sound")])

    def test_target_under_old_or_new_name_is_fine(self):
        items = [issue(1, "sound", "sys-audio"), issue(2, "sound", "area: audio")]
        self.assertEqual(ml.unmerged(items, RENAMES), [])


class ChooseIssueTypeTests(unittest.TestCase):
    def test_mapping(self):
        self.assertEqual(ml.choose_issue_type(frozenset({"type: feature"})), "Feature")
        self.assertEqual(ml.choose_issue_type(frozenset({"type: fix"})), "Bug")
        self.assertEqual(ml.choose_issue_type(frozenset({"type: refactor"})), "Task")
        self.assertEqual(ml.choose_issue_type(frozenset({"bug"})), "Bug")
        self.assertIsNone(ml.choose_issue_type(frozenset({"area: audio"})))

    def test_first_match_wins(self):
        self.assertEqual(
            ml.choose_issue_type(frozenset({"type: feature", "bug"})), "Bug"
        )
        self.assertEqual(
            ml.choose_issue_type(frozenset({"type: refactor", "type: feature"})),
            "Feature",
        )


class PlanPostTests(unittest.TestCase):
    def test_sets_type_and_removes_label(self):
        actions = ml.plan_post([issue(1, "type: feature", "area: audio")], set())
        self.assertEqual(
            actions, [ml.SetIssueType(1, "Feature"), ml.RemoveLabel(1, "type: feature")]
        )

    def test_never_overwrites_existing_issue_type(self):
        actions = ml.plan_post([issue(1, "type: feature", issue_type="Bug")], set())
        self.assertEqual(actions, [ml.RemoveLabel(1, "type: feature")])

    def test_removes_every_type_like_label(self):
        actions = ml.plan_post([issue(1, "bug", "type: feature")], set())
        self.assertEqual(
            actions,
            [
                ml.SetIssueType(1, "Bug"),
                ml.RemoveLabel(1, "bug"),
                ml.RemoveLabel(1, "type: feature"),
            ],
        )

    def test_prs_keep_type_labels(self):
        self.assertEqual(ml.plan_post([pr(5, "type: feature")], set()), [])

    def test_deletes_only_existing_delete_labels(self):
        actions = ml.plan_post([], {"sound", "spam", "area: audio"})
        self.assertEqual(actions, [ml.DeleteLabel("sound"), ml.DeleteLabel("spam")])

    def test_idempotent_after_apply(self):
        after = [issue(1, "area: audio", issue_type="Feature")]
        self.assertEqual(ml.plan_post(after, {"area: audio"}), [])


class ExpectedLabelsTests(unittest.TestCase):
    def test_merge_then_rename(self):
        self.assertEqual(
            ml.expected_labels(issue(1, "sound"), RENAMES),
            (frozenset({"area: audio"}), False),
        )

    def test_issue_type_labels_become_issue_type(self):
        self.assertEqual(
            ml.expected_labels(issue(1, "enhancement"), RENAMES), (frozenset(), True)
        )
        self.assertEqual(
            ml.expected_labels(issue(2, "bug"), RENAMES), (frozenset(), True)
        )

    def test_pr_keeps_type_label_and_drops_bug(self):
        self.assertEqual(
            ml.expected_labels(pr(1, "enhancement", "bug"), RENAMES),
            (frozenset({"type: feature"}), False),
        )

    def test_kept_labels_unchanged(self):
        self.assertEqual(
            ml.expected_labels(issue(1, "release-blocker"), RENAMES),
            (frozenset({"release-blocker"}), False),
        )


class VerifyTests(unittest.TestCase):
    def test_clean_migration_has_no_problems(self):
        snapshot = [issue(1, "sound", "enhancement"), pr(2, "vscode")]
        current = [
            issue(1, "area: audio", issue_type="Feature"),
            pr(2, "area: toolchain"),
        ]
        self.assertEqual(ml.verify(snapshot, current, RENAMES), [])

    def test_reports_missing_label(self):
        problems = ml.verify([issue(1, "crash")], [issue(1)], RENAMES)
        self.assertEqual(len(problems), 1)
        self.assertIn("impact: crash", problems[0])

    def test_reports_missing_issue_type(self):
        problems = ml.verify([issue(1, "enhancement")], [issue(1)], RENAMES)
        self.assertIn("issue #1: has no Issue Type", problems)

    def test_reports_leftover_labels(self):
        problems = ml.verify(
            [issue(1, "enhancement")],
            [issue(1, "type: feature", issue_type="Feature")],
            RENAMES,
        )
        self.assertEqual(len(problems), 1)
        self.assertIn("type: feature", problems[0])

    def test_reports_missing_item(self):
        self.assertEqual(
            ml.verify([pr(9)], [], RENAMES), ["pr #9: missing from current state"]
        )

    def test_extra_labels_added_since_are_fine(self):
        self.assertEqual(ml.verify([issue(1)], [issue(1, "area: midi")], RENAMES), [])


class SnapshotJsonTests(unittest.TestCase):
    def test_round_trip(self):
        items = [issue(1, "b", "a", issue_type="Bug"), pr(2)]
        self.assertEqual(ml.items_from_json(ml.items_to_json(items)), items)


class DescribeTests(unittest.TestCase):
    def test_descriptions(self):
        self.assertEqual(ml.AddLabel(1, "x").describe(), "#1: add label 'x'")
        self.assertEqual(ml.RemoveLabel(1, "x").describe(), "#1: remove label 'x'")
        self.assertEqual(
            ml.SetIssueType(1, "Bug").describe(), "#1: set Issue Type 'Bug'"
        )
        self.assertEqual(ml.DeleteLabel("x").describe(), "delete label 'x'")


if __name__ == "__main__":
    unittest.main()
