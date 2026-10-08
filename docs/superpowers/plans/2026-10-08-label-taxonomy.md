# Label Taxonomy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define the repo's label taxonomy in `.github/labels.yml`, sync it with a GitHub Actions workflow, and provide a one-shot migration script that moves existing issues/PRs onto it without losing label meaning.

**Architecture:** `.github/labels.yml` is the single source of truth, consumed by `crazy-max/ghaction-github-labeler@v6` (renames via `from_name`, `skip-delete: true`). A stdlib-plus-PyYAML Python script, `.github/scripts/migrate_labels.py`, handles what the action cannot: folding secondary labels into primaries before the rename (`pre`), moving type-like labels on issues to native Issue Types and deleting dead labels (`post`), and checking the result against a snapshot (`verify`). Pure planning functions are separated from a thin `gh` I/O layer so the logic is unit-testable without network access.

**Tech Stack:** YAML, GitHub Actions, Python ≥3.10 (`unittest`, `argparse`, `dataclasses`, `match`), PyYAML, `gh` CLI.

**Spec:** `docs/superpowers/specs/2026-10-08-label-taxonomy-design.md`

## Global Constraints

- Label groups and colors: `type:` `5319e7` (PRs only), `area:` `0075ca`, `status:` `fbca04`, `impact:` `d93f0b`.
- Unprefixed labels kept as-is: `release-blocker`, `beta-blocker`, `good first issue`, `help wanted`, `cherry-pick`, `cherry-picked`.
- Labels deleted by migration: `sound`, `new-feature`, `proof of concept`, `vscode`, `python` (merged first) and `bug`, `dependencies`, `javascript`, `spam`, `question` (dropped).
- Merges: `sound`→`sys-audio`, `new-feature`→`enhancement`, `proof of concept`→`enhancement`, `vscode`→`toolchain`, `python`→`toolchain`.
- Issue Type mapping, first match wins: `bug`→Bug, `type: fix`→Bug, `type: feature`→Feature, `type: refactor`→Task. Only set when the issue has no Issue Type.
- Sync workflow uses `skip-delete: true`; deletions happen only via the migration script.
- Migration script is dry-run by default; writes only with `--apply`.
- Label descriptions ≤ 100 characters (GitHub limit). Colors are 6 lowercase hex digits without `#`.
- Follow existing repo patterns: Python tests use stdlib `unittest` and are runnable directly (`python3 path/to/test_x.py`), like `scripts/tasks/test_annotate_sdram.py`; workflows use `actions/checkout@v7`; pre-commit runs `actionlint`, `ruff check --fix`, `ruff format`.
- If a commit fails because pre-commit reformatted files, re-run the task's tests, `git add` the same files, and commit again. Never use `--no-verify`.

## Review Focus

- **`post` run before `pre`, or before the sync merged:** a maintainer runs phases out of order. Expected: `post` refuses (exit 2) if any old `from_name` label still exists or any item carries a merge-source label without its target, instead of deleting `sound`/`vscode`/`python` and silently losing those labels. Tested in Task 3.
- **A rename target already exists:** someone hand-creates `area: audio` before the sync. Expected: `pre` refuses (exit 2) and names the conflict, since the action's rename would fail. Tested in Tasks 2 and 3.
- **Label names with spaces and colons in REST paths:** `type: fix`, `proof of concept`. Expected: URL-encoded in DELETE paths (`type%3A%20fix`), so the right label is removed. Tested in Task 3.
- **Issue with several type-like labels and/or an existing Issue Type:** e.g. `bug` + `enhancement`, or already typed Feature. Expected: existing Issue Type is never overwritten; otherwise first match in the mapping order wins; all type-like labels are removed. Tested in Task 2.
- **Re-running a phase after partial failure:** network error halfway through `--apply`. Expected: re-running plans only the remaining work (idempotent), never duplicate or contradictory writes. Tested in Task 2.

---

## File Structure

| File | Responsibility |
|---|---|
| `.github/labels.yml` (create) | Source of truth for every managed label |
| `.github/scripts/test_labels_yml.py` (create) | Structural and completeness checks on `labels.yml` and `dependabot.yml` |
| `.github/scripts/migrate_labels.py` (create) | Migration: pure planning functions + `gh` I/O + CLI |
| `.github/scripts/test_migrate_labels.py` (create) | Unit tests for planning, command building, parsing, CLI |
| `.github/workflows/sync-labels.yml` (create) | Runs tests on PRs, dry-runs/applies the label sync |
| `.github/dependabot.yml` (modify lines 4-6, 18-20, 28-29) | Use new label names |
| `docs/CONTRIBUTING.md` (modify, after line 68) | "Labels" section |
| `docs/superpowers/specs/2026-10-08-label-taxonomy-design.md` (modify) | Record PyYAML dependency and `migrate_labels.py` filename |

---

### Task 1: `labels.yml` and its structural tests

**Files:**
- Create: `.github/scripts/test_labels_yml.py`
- Create: `.github/labels.yml`

**Interfaces:**
- Consumes: nothing.
- Produces: `.github/labels.yml` (list of `{name, color, description, from_name?}`); module `test_labels_yml` exporting `CURRENT_LABELS: set[str]` (the 47 labels on the repo as of 2026-10-08), `KEPT_UNPREFIXED: set[str]` and `LABELS_FILE: Path`, both imported by Task 2's tests.

- [ ] **Step 1: Write the failing test**

Create `.github/scripts/test_labels_yml.py`:

```python
#! /usr/bin/env python3
"""Structural checks for .github/labels.yml.

Run directly with:
    python3 .github/scripts/test_labels_yml.py
"""

import re
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

GROUP_COLORS = {"type": "5319e7", "area": "0075ca", "status": "fbca04", "impact": "d93f0b"}


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
                self.assertLessEqual(set(entry), {"name", "color", "description", "from_name"})
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


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 .github/scripts/test_labels_yml.py`
Expected: ERROR in `setUpClass` with `FileNotFoundError: ... .github/labels.yml`

- [ ] **Step 3: Create `.github/labels.yml`**

```yaml
# Source of truth for repository labels.
# Synced by .github/workflows/sync-labels.yml (crazy-max/ghaction-github-labeler).
# `from_name` renames an existing label, keeping it on every issue and PR.
# Labels not listed here are left alone (skip-delete); delete them explicitly.
#
# Groups: type: (PRs only; issues use native Issue Type), area:, status:, impact:.

# ---- type: (PRs only) ----
- name: "type: fix"
  color: "5319e7"
  description: "Fixes a bug"
  from_name: "bugfix"
- name: "type: feature"
  color: "5319e7"
  description: "New or changed user-facing behavior"
  from_name: "enhancement"
- name: "type: refactor"
  color: "5319e7"
  description: "Code restructuring with no intended behavior change"
  from_name: "refactor"
- name: "type: deps"
  color: "5319e7"
  description: "Dependency updates"
  from_name: "pr-dependencies"
- name: "type: chore"
  color: "5319e7"
  description: "Build, CI, tooling, docs-only or housekeeping changes"

# ---- area: ----
- name: "area: audio"
  color: "0075ca"
  description: "Audio engine, DSP, voices and samples"
  from_name: "sys-audio"
- name: "area: sequencer"
  color: "0075ca"
  description: "Clips, notes, iterance, song structure and playback"
  from_name: "sys-sequencer"
- name: "area: automation"
  color: "0075ca"
  description: "Parameter automation"
  from_name: "sys-automation"
- name: "area: midi"
  color: "0075ca"
  description: "MIDI input, output and learn"
  from_name: "sys-midi"
- name: "area: cv-gate"
  color: "0075ca"
  description: "CV and gate outputs"
- name: "area: browser"
  color: "0075ca"
  description: "File browser UI"
  from_name: "sys-browser"
- name: "area: files"
  color: "0075ca"
  description: "Song and preset loading and saving"
- name: "area: menus"
  color: "0075ca"
  description: "Menus and context menus"
- name: "area: ui"
  color: "0075ca"
  description: "Pads, OLED and 7SEG UI and rendering"
  from_name: "ui-rendering"
- name: "area: system"
  color: "0075ca"
  description: "Memory, drivers, boot and crash handling"
- name: "area: toolchain"
  color: "0075ca"
  description: "DBT, build tooling, IDE configs and scripts"
  from_name: "toolchain"
- name: "area: ci"
  color: "0075ca"
  description: "GitHub Actions and repository automation"
  from_name: "github workflow"
- name: "area: website"
  color: "0075ca"
  description: "delugecommunity.com website"
  from_name: "website"
- name: "area: docs"
  color: "0075ca"
  description: "Documentation"
  from_name: "documentation"

# ---- status: ----
- name: "status: needs-info"
  color: "fbca04"
  description: "Waiting on the reporter for more details"
- name: "status: no-repro"
  color: "fbca04"
  description: "Cannot be reproduced with the information provided"
  from_name: "no-repro"
- name: "status: needs-testing"
  color: "fbca04"
  description: "PR needs testing on hardware"
  from_name: "needs testing"
- name: "status: needs-review"
  color: "fbca04"
  description: "PR is ready and wants a reviewer"
  from_name: "Code Review wanted"
- name: "status: quick-review"
  color: "fbca04"
  description: "Small PR; a quick review is welcome"
  from_name: "small-pr"
- name: "status: deferred"
  color: "fbca04"
  description: "Intentionally parked for now"
  from_name: "deferred"
- name: "status: stale"
  color: "fbca04"
  description: "No recent activity; will be closed soon"
  from_name: "stale"
- name: "status: merge-conflict"
  color: "fbca04"
  description: "PR has merge conflicts to resolve"
  from_name: "merge conflict"
- name: "status: manual-merge"
  color: "fbca04"
  description: "PR cannot be merged through the merge queue"
  from_name: "pr-needs manual merge"
- name: "status: wontfix"
  color: "fbca04"
  description: "Will not be worked on; PRs welcome"
  from_name: "wontfix"
- name: "status: duplicate"
  color: "fbca04"
  description: "Duplicate of another issue or PR"
  from_name: "duplicate"

# ---- impact: ----
- name: "impact: crash"
  color: "d93f0b"
  description: "Can hang, freeze or crash the Deluge"
  from_name: "crash"
- name: "impact: memory-corruption"
  color: "d93f0b"
  description: "Corrupts memory"
  from_name: "memory-corruption"
- name: "impact: performance"
  color: "d93f0b"
  description: "Performance is below expectations"
  from_name: "performance"
- name: "impact: official-firmware"
  color: "d93f0b"
  description: "Also present on the official firmware"
  from_name: "official"
- name: "impact: ux-inconsistent"
  color: "d93f0b"
  description: "Behavior is unexpected or inconsistent"
  from_name: "ui-inconsistent"
- name: "impact: ux-undesirable"
  color: "d93f0b"
  description: "Not a bug, but should be changed"
  from_name: "ui-undesireable"
- name: "impact: accessibility"
  color: "d93f0b"
  description: "Makes the Deluge difficult to use"
  from_name: "ui-accessibility"

# ---- unprefixed (release gates, GitHub-recognised, historical) ----
- name: "release-blocker"
  color: "cab996"
  description: "Must be fixed before a release moves from beta to release-candidate"
- name: "beta-blocker"
  color: "d37c3f"
  description: "Must be fixed before a release moves from alpha to beta"
- name: "good first issue"
  color: "7057ff"
  description: "Good for newcomers"
- name: "help wanted"
  color: "008672"
  description: "Extra attention is needed"
- name: "cherry-pick"
  color: "f57b73"
  description: "Commit to cherry-pick to a release branch"
- name: "cherry-picked"
  color: "8fdf7a"
  description: "PR generated by the cherry-pick action"
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 .github/scripts/test_labels_yml.py`
Expected: `OK` (9 tests)

- [ ] **Step 5: Commit**

```bash
git add .github/labels.yml .github/scripts/test_labels_yml.py
git commit -m "Add label taxonomy definition"
```

---

### Task 2: Migration planning core (pure functions)

**Files:**
- Create: `.github/scripts/migrate_labels.py`
- Create: `.github/scripts/test_migrate_labels.py`

**Interfaces:**
- Consumes: `.github/labels.yml` from Task 1; `test_labels_yml.CURRENT_LABELS`, `test_labels_yml.KEPT_UNPREFIXED`, `test_labels_yml.LABELS_FILE`.
- Produces (in `migrate_labels`), used by Task 3:
  - Constants `MERGES: dict[str, str]`, `DROPPED: set[str]`, `DELETE: set[str]`, `ISSUE_TYPE_FROM_LABEL: list[tuple[str, str]]`, `ISSUE_TYPE_LABELS: set[str]`, `DEFAULT_REPO: str`, `DEFAULT_LABELS_FILE: Path`.
  - `@dataclass(frozen=True) Item(kind: str, number: int, labels: frozenset[str], issue_type: str | None = None)` — `kind` is `"issue"` or `"pr"`.
  - Actions, each a frozen dataclass with `describe() -> str`: `AddLabel(number: int, label: str)`, `RemoveLabel(number: int, label: str)`, `SetIssueType(number: int, issue_type: str)`, `DeleteLabel(name: str)`.
  - `load_renames(yaml_text: str) -> dict[str, str]` (old → new)
  - `rename_conflicts(renames: dict[str, str], existing: set[str]) -> list[str]`
  - `plan_pre(items: list[Item]) -> list[AddLabel]`
  - `unmerged(items: list[Item], renames: dict[str, str]) -> list[Item]`
  - `choose_issue_type(labels: frozenset[str]) -> str | None`
  - `plan_post(items: list[Item], existing: set[str]) -> list[SetIssueType | RemoveLabel | DeleteLabel]`
  - `expected_labels(item: Item, renames: dict[str, str]) -> tuple[frozenset[str], bool]`
  - `verify(snapshot: list[Item], current: list[Item], renames: dict[str, str]) -> list[str]`
  - `items_to_json(items: list[Item]) -> str`, `items_from_json(text: str) -> list[Item]`

- [ ] **Step 1: Write the failing tests**

Create `.github/scripts/test_migrate_labels.py`:

```python
#! /usr/bin/env python3
"""Unit tests for migrate_labels.py.

Run directly with:
    python3 .github/scripts/test_migrate_labels.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import migrate_labels as ml  # noqa: E402
from test_labels_yml import CURRENT_LABELS, KEPT_UNPREFIXED, LABELS_FILE  # noqa: E402

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
        conflicts = ml.rename_conflicts({"sys-audio": "area: audio"}, {"sys-audio", "area: audio"})
        self.assertEqual(len(conflicts), 1)
        self.assertIn("area: audio", conflicts[0])

    def test_no_conflict_before_or_after_rename(self):
        self.assertEqual(ml.rename_conflicts({"sys-audio": "area: audio"}, {"sys-audio"}), [])
        self.assertEqual(ml.rename_conflicts({"sys-audio": "area: audio"}, {"area: audio"}), [])


class PlanPreTests(unittest.TestCase):
    def test_adds_primary_for_secondary(self):
        self.assertEqual(ml.plan_pre([issue(1, "sound")]), [ml.AddLabel(1, "sys-audio")])

    def test_skips_when_primary_present(self):
        self.assertEqual(ml.plan_pre([issue(1, "sound", "sys-audio")]), [])

    def test_two_secondaries_same_primary_add_once(self):
        self.assertEqual(ml.plan_pre([pr(2, "vscode", "python")]), [ml.AddLabel(2, "toolchain")])

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
        self.assertEqual(ml.choose_issue_type(frozenset({"type: feature", "bug"})), "Bug")
        self.assertEqual(ml.choose_issue_type(frozenset({"type: refactor", "type: feature"})), "Feature")


class PlanPostTests(unittest.TestCase):
    def test_sets_type_and_removes_label(self):
        actions = ml.plan_post([issue(1, "type: feature", "area: audio")], set())
        self.assertEqual(actions, [ml.SetIssueType(1, "Feature"), ml.RemoveLabel(1, "type: feature")])

    def test_never_overwrites_existing_issue_type(self):
        actions = ml.plan_post([issue(1, "type: feature", issue_type="Bug")], set())
        self.assertEqual(actions, [ml.RemoveLabel(1, "type: feature")])

    def test_removes_every_type_like_label(self):
        actions = ml.plan_post([issue(1, "bug", "type: feature")], set())
        self.assertEqual(
            actions,
            [ml.SetIssueType(1, "Bug"), ml.RemoveLabel(1, "bug"), ml.RemoveLabel(1, "type: feature")],
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
        self.assertEqual(ml.expected_labels(issue(1, "sound"), RENAMES), (frozenset({"area: audio"}), False))

    def test_issue_type_labels_become_issue_type(self):
        self.assertEqual(ml.expected_labels(issue(1, "enhancement"), RENAMES), (frozenset(), True))
        self.assertEqual(ml.expected_labels(issue(2, "bug"), RENAMES), (frozenset(), True))

    def test_pr_keeps_type_label_and_drops_bug(self):
        self.assertEqual(ml.expected_labels(pr(1, "enhancement", "bug"), RENAMES), (frozenset({"type: feature"}), False))

    def test_kept_labels_unchanged(self):
        self.assertEqual(ml.expected_labels(issue(1, "release-blocker"), RENAMES), (frozenset({"release-blocker"}), False))


class VerifyTests(unittest.TestCase):
    def test_clean_migration_has_no_problems(self):
        snapshot = [issue(1, "sound", "enhancement"), pr(2, "vscode")]
        current = [issue(1, "area: audio", issue_type="Feature"), pr(2, "area: toolchain")]
        self.assertEqual(ml.verify(snapshot, current, RENAMES), [])

    def test_reports_missing_label(self):
        problems = ml.verify([issue(1, "crash")], [issue(1)], RENAMES)
        self.assertEqual(len(problems), 1)
        self.assertIn("impact: crash", problems[0])

    def test_reports_missing_issue_type(self):
        problems = ml.verify([issue(1, "enhancement")], [issue(1)], RENAMES)
        self.assertIn("issue #1: has no Issue Type", problems)

    def test_reports_leftover_labels(self):
        problems = ml.verify([issue(1, "enhancement")], [issue(1, "type: feature", issue_type="Feature")], RENAMES)
        self.assertEqual(len(problems), 1)
        self.assertIn("type: feature", problems[0])

    def test_reports_missing_item(self):
        self.assertEqual(ml.verify([pr(9)], [], RENAMES), ["pr #9: missing from current state"])

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
        self.assertEqual(ml.SetIssueType(1, "Bug").describe(), "#1: set Issue Type 'Bug'")
        self.assertEqual(ml.DeleteLabel("x").describe(), "delete label 'x'")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python3 .github/scripts/test_migrate_labels.py`
Expected: `ModuleNotFoundError: No module named 'migrate_labels'`

- [ ] **Step 3: Implement the planning core**

Create `.github/scripts/migrate_labels.py`:

```python
#! /usr/bin/env python3
"""One-shot migration of repository labels to the taxonomy in .github/labels.yml.

See docs/superpowers/specs/2026-10-08-label-taxonomy-design.md.

Phases (dry run unless --apply is given):
    pre     snapshot every issue/PR's labels, then fold secondary labels into
            their primary so the sync workflow's rename carries them
    post    after the sync: move type-like labels on issues to native Issue
            Types, then delete merged-away and dead labels
    verify  compare current labels against a pre-phase snapshot

Requires an authenticated `gh` CLI and PyYAML.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import yaml

DEFAULT_REPO = "SynthstromAudible/DelugeFirmware"
DEFAULT_LABELS_FILE = Path(__file__).resolve().parents[1] / "labels.yml"

# Secondary label -> primary label it is folded into before the rename.
MERGES = {
    "sound": "sys-audio",
    "new-feature": "enhancement",
    "proof of concept": "enhancement",
    "vscode": "toolchain",
    "python": "toolchain",
}

# Labels removed with nothing to fold into.
DROPPED = {"bug", "dependencies", "javascript", "spam", "question"}

DELETE = set(MERGES) | DROPPED

# Issues carry these as native Issue Types instead. Checked in order; first match wins.
ISSUE_TYPE_FROM_LABEL = [
    ("bug", "Bug"),
    ("type: fix", "Bug"),
    ("type: feature", "Feature"),
    ("type: refactor", "Task"),
]
ISSUE_TYPE_LABELS = {label for label, _ in ISSUE_TYPE_FROM_LABEL}


@dataclass(frozen=True)
class Item:
    kind: str  # "issue" or "pr"
    number: int
    labels: frozenset[str]
    issue_type: str | None = None


@dataclass(frozen=True)
class AddLabel:
    number: int
    label: str

    def describe(self) -> str:
        return f"#{self.number}: add label {self.label!r}"


@dataclass(frozen=True)
class RemoveLabel:
    number: int
    label: str

    def describe(self) -> str:
        return f"#{self.number}: remove label {self.label!r}"


@dataclass(frozen=True)
class SetIssueType:
    number: int
    issue_type: str

    def describe(self) -> str:
        return f"#{self.number}: set Issue Type {self.issue_type!r}"


@dataclass(frozen=True)
class DeleteLabel:
    name: str

    def describe(self) -> str:
        return f"delete label {self.name!r}"


def load_renames(yaml_text: str) -> dict[str, str]:
    """Map each `from_name` in labels.yml to the label it becomes."""
    return {entry["from_name"]: entry["name"] for entry in yaml.safe_load(yaml_text) if "from_name" in entry}


def rename_conflicts(renames: dict[str, str], existing: set[str]) -> list[str]:
    """Renames that would fail because the new name already exists next to the old one."""
    return sorted(
        f"{new!r} already exists alongside {old!r}; the rename would fail"
        for old, new in renames.items()
        if old in existing and new in existing
    )


def plan_pre(items: list[Item]) -> list[AddLabel]:
    """Add each secondary label's primary so the rename carries the merged items."""
    actions = []
    for item in items:
        targets = {MERGES[label] for label in item.labels if label in MERGES} - item.labels
        actions += [AddLabel(item.number, target) for target in sorted(targets)]
    return actions


def unmerged(items: list[Item], renames: dict[str, str]) -> list[Item]:
    """Items still carrying a secondary label without its primary under either name."""
    result = []
    for item in items:
        for label in item.labels & MERGES.keys():
            target = MERGES[label]
            if target not in item.labels and renames.get(target, target) not in item.labels:
                result.append(item)
                break
    return result


def choose_issue_type(labels: frozenset[str]) -> str | None:
    for label, issue_type in ISSUE_TYPE_FROM_LABEL:
        if label in labels:
            return issue_type
    return None


def plan_post(items: list[Item], existing: set[str]) -> list[SetIssueType | RemoveLabel | DeleteLabel]:
    """Move type-like labels on issues to Issue Types, then delete dead labels."""
    actions: list[SetIssueType | RemoveLabel | DeleteLabel] = []
    for item in items:
        if item.kind != "issue":
            continue
        issue_type = choose_issue_type(item.labels)
        if issue_type and item.issue_type is None:
            actions.append(SetIssueType(item.number, issue_type))
        actions += [RemoveLabel(item.number, label) for label in sorted(item.labels & ISSUE_TYPE_LABELS)]
    actions += [DeleteLabel(name) for name in sorted(DELETE & existing)]
    return actions


def expected_labels(item: Item, renames: dict[str, str]) -> tuple[frozenset[str], bool]:
    """Labels a pre-migration item should carry afterwards, and whether it needs an Issue Type."""
    expected = set()
    needs_type = False
    for label in item.labels:
        label = MERGES.get(label, label)
        label = renames.get(label, label)
        if item.kind == "issue" and label in ISSUE_TYPE_LABELS:
            needs_type = True
        elif label not in DROPPED:
            expected.add(label)
    return frozenset(expected), needs_type


def verify(snapshot: list[Item], current: list[Item], renames: dict[str, str]) -> list[str]:
    by_key = {(item.kind, item.number): item for item in current}
    problems = []
    for old in snapshot:
        now = by_key.get((old.kind, old.number))
        if now is None:
            problems.append(f"{old.kind} #{old.number}: missing from current state")
            continue
        expected, needs_type = expected_labels(old, renames)
        missing = sorted(expected - now.labels)
        if missing:
            problems.append(f"{old.kind} #{old.number}: missing labels {missing}")
        if needs_type and now.issue_type is None:
            problems.append(f"issue #{old.number}: has no Issue Type")
        stale = DELETE | (ISSUE_TYPE_LABELS if old.kind == "issue" else set())
        leftover = sorted(now.labels & stale)
        if leftover:
            problems.append(f"{old.kind} #{old.number}: still has labels {leftover}")
    return problems


def items_to_json(items: list[Item]) -> str:
    return json.dumps(
        [
            {"kind": i.kind, "number": i.number, "labels": sorted(i.labels), "issue_type": i.issue_type}
            for i in items
        ],
        indent=1,
    )


def items_from_json(text: str) -> list[Item]:
    return [Item(d["kind"], d["number"], frozenset(d["labels"]), d["issue_type"]) for d in json.loads(text)]
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python3 .github/scripts/test_migrate_labels.py && python3 .github/scripts/test_labels_yml.py`
Expected: both `OK`

- [ ] **Step 5: Commit**

```bash
git add .github/scripts/migrate_labels.py .github/scripts/test_migrate_labels.py
git commit -m "Add label migration planning logic"
```

---

### Task 3: GitHub I/O layer and CLI

**Files:**
- Modify: `.github/scripts/migrate_labels.py` (append; add imports)
- Modify: `.github/scripts/test_migrate_labels.py` (append test classes before `if __name__`)

**Interfaces:**
- Consumes: everything Task 2 produces.
- Produces:
  - `command_for(repo: str, action) -> list[str]` — `gh` arguments for one action.
  - `parse_items(kind: str, output: str) -> list[Item]` — parses `gh api graphql --jq '... | @json'` output (one JSON object per line).
  - `class GitHub(repo: str, run: Callable[[list[str]], str] = run_gh, delay: float = 1.0)` with `fetch_items() -> list[Item]`, `fetch_label_names() -> set[str]`, `apply(action) -> None`.
  - `main(argv: list[str] | None = None, github_factory=GitHub, out=sys.stdout) -> int` — exit codes: 0 success, 1 verify found problems, 2 refused to run.

- [ ] **Step 1: Write the failing tests**

Append to `.github/scripts/test_migrate_labels.py`, above `if __name__ == "__main__":`. Also add `import io`, `import json`, `import tempfile` to the imports at the top of the file.

```python
class CommandForTests(unittest.TestCase):
    REPO = "o/r"

    def test_add_label(self):
        self.assertEqual(
            ml.command_for(self.REPO, ml.AddLabel(5, "sys-audio")),
            ["api", "-X", "POST", "repos/o/r/issues/5/labels", "-f", "labels[]=sys-audio"],
        )

    def test_remove_label_is_url_encoded(self):
        self.assertEqual(
            ml.command_for(self.REPO, ml.RemoveLabel(5, "type: fix")),
            ["api", "-X", "DELETE", "repos/o/r/issues/5/labels/type%3A%20fix"],
        )

    def test_set_issue_type(self):
        self.assertEqual(
            ml.command_for(self.REPO, ml.SetIssueType(5, "Feature")),
            ["api", "-X", "PATCH", "repos/o/r/issues/5", "-f", "type=Feature"],
        )

    def test_delete_label_is_url_encoded(self):
        self.assertEqual(
            ml.command_for(self.REPO, ml.DeleteLabel("proof of concept")),
            ["api", "-X", "DELETE", "repos/o/r/labels/proof%20of%20concept"],
        )


class ParseItemsTests(unittest.TestCase):
    def test_parses_issue_lines(self):
        output = (
            json.dumps({"number": 1, "issueType": {"name": "Bug"}, "labels": {"nodes": [{"name": "crash"}]}})
            + "\n"
            + json.dumps({"number": 2, "issueType": None, "labels": {"nodes": []}})
            + "\n\n"
        )
        self.assertEqual(ml.parse_items("issue", output), [issue(1, "crash", issue_type="Bug"), issue(2)])

    def test_parses_pr_lines_without_issue_type(self):
        output = json.dumps({"number": 3, "labels": {"nodes": [{"name": "refactor"}]}}) + "\n"
        self.assertEqual(ml.parse_items("pr", output), [pr(3, "refactor")])


class GitHubTests(unittest.TestCase):
    def test_fetch_items_queries_issues_then_prs(self):
        calls = []

        def run(args):
            calls.append(args)
            if "issues(first: 100" in " ".join(args):
                return json.dumps({"number": 1, "issueType": None, "labels": {"nodes": []}}) + "\n"
            return json.dumps({"number": 2, "labels": {"nodes": []}}) + "\n"

        items = ml.GitHub("o/r", run=run, delay=0).fetch_items()
        self.assertEqual(items, [issue(1), pr(2)])
        self.assertEqual(len(calls), 2)
        for args in calls:
            self.assertEqual(args[:3], ["api", "graphql", "--paginate"])
            self.assertIn("owner=o", args)
            self.assertIn("name=r", args)

    def test_fetch_label_names(self):
        github = ml.GitHub("o/r", run=lambda args: "a\nb c\n", delay=0)
        self.assertEqual(github.fetch_label_names(), {"a", "b c"})

    def test_apply_runs_command(self):
        calls = []
        ml.GitHub("o/r", run=lambda args: calls.append(args) or "", delay=0).apply(ml.DeleteLabel("spam"))
        self.assertEqual(calls, [["api", "-X", "DELETE", "repos/o/r/labels/spam"]])


class FakeGitHub:
    def __init__(self, items, labels):
        self.items = items
        self.labels = labels
        self.applied = []

    def fetch_items(self):
        return list(self.items)

    def fetch_label_names(self):
        return set(self.labels)

    def apply(self, action):
        self.applied.append(action)


class MainTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_main(self, fake, *argv):
        out = io.StringIO()
        code = ml.main([*argv], github_factory=lambda repo: fake, out=out)
        return code, out.getvalue()

    def test_pre_dry_run_writes_snapshot_and_applies_nothing(self):
        fake = FakeGitHub([issue(1, "sound")], {"sound", "sys-audio"})
        code, out = self.run_main(fake, "pre", "--snapshot-dir", str(self.dir))
        self.assertEqual(code, 0)
        self.assertEqual(fake.applied, [])
        self.assertIn("dry-run: #1: add label 'sys-audio'", out)
        snapshots = list(self.dir.glob("label-snapshot-*.json"))
        self.assertEqual(len(snapshots), 1)
        self.assertEqual(ml.items_from_json(snapshots[0].read_text()), [issue(1, "sound")])

    def test_pre_apply_applies(self):
        fake = FakeGitHub([issue(1, "sound")], {"sound", "sys-audio"})
        code, _ = self.run_main(fake, "--apply", "pre", "--snapshot-dir", str(self.dir))
        self.assertEqual(code, 0)
        self.assertEqual(fake.applied, [ml.AddLabel(1, "sys-audio")])

    def test_pre_refuses_on_rename_conflict(self):
        fake = FakeGitHub([], {"sys-audio", "area: audio"})
        code, out = self.run_main(fake, "--apply", "pre", "--snapshot-dir", str(self.dir))
        self.assertEqual(code, 2)
        self.assertIn("area: audio", out)
        self.assertEqual(fake.applied, [])

    def test_post_refuses_before_sync(self):
        fake = FakeGitHub([], {"enhancement", "sound"})
        code, out = self.run_main(fake, "--apply", "post")
        self.assertEqual(code, 2)
        self.assertIn("enhancement", out)
        self.assertEqual(fake.applied, [])

    def test_post_refuses_before_pre(self):
        fake = FakeGitHub([issue(1, "sound")], {"sound", "area: audio"})
        code, out = self.run_main(fake, "--apply", "post")
        self.assertEqual(code, 2)
        self.assertIn("#1", out)
        self.assertEqual(fake.applied, [])

    def test_post_apply(self):
        fake = FakeGitHub([issue(1, "type: feature")], {"type: feature", "spam"})
        code, _ = self.run_main(fake, "--apply", "post")
        self.assertEqual(code, 0)
        self.assertEqual(
            fake.applied,
            [ml.SetIssueType(1, "Feature"), ml.RemoveLabel(1, "type: feature"), ml.DeleteLabel("spam")],
        )

    def test_verify_exit_codes(self):
        snapshot = self.dir / "snap.json"
        snapshot.write_text(ml.items_to_json([issue(1, "crash")]))
        good = FakeGitHub([issue(1, "impact: crash")], set())
        bad = FakeGitHub([issue(1)], set())
        self.assertEqual(self.run_main(good, "verify", "--snapshot", str(snapshot))[0], 0)
        self.assertEqual(self.run_main(bad, "verify", "--snapshot", str(snapshot))[0], 1)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python3 .github/scripts/test_migrate_labels.py`
Expected: FAIL/ERROR with `AttributeError: module 'migrate_labels' has no attribute 'command_for'` (and similar for `parse_items`, `GitHub`, `main`); Task 2 tests still pass.

- [ ] **Step 3: Implement I/O and CLI**

In `.github/scripts/migrate_labels.py`, replace the import block with:

```python
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from collections.abc import Callable
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote

import yaml
```

Append to the end of the file:

```python
ITEMS_QUERY = """
query($owner: String!, $name: String!, $endCursor: String) {
  repository(owner: $owner, name: $name) {
    %s(first: 100, after: $endCursor) {
      pageInfo { hasNextPage endCursor }
      nodes { number %s labels(first: 100) { nodes { name } } }
    }
  }
}
"""


def run_gh(args: list[str]) -> str:
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True).stdout


def command_for(repo: str, action) -> list[str]:
    match action:
        case AddLabel(number, label):
            return ["api", "-X", "POST", f"repos/{repo}/issues/{number}/labels", "-f", f"labels[]={label}"]
        case RemoveLabel(number, label):
            return ["api", "-X", "DELETE", f"repos/{repo}/issues/{number}/labels/{quote(label, safe='')}"]
        case SetIssueType(number, issue_type):
            return ["api", "-X", "PATCH", f"repos/{repo}/issues/{number}", "-f", f"type={issue_type}"]
        case DeleteLabel(name):
            return ["api", "-X", "DELETE", f"repos/{repo}/labels/{quote(name, safe='')}"]
    raise TypeError(f"unknown action {action!r}")


def parse_items(kind: str, output: str) -> list[Item]:
    items = []
    for line in output.splitlines():
        if not line.strip():
            continue
        node = json.loads(line)
        labels = frozenset(label["name"] for label in node["labels"]["nodes"])
        issue_type = (node.get("issueType") or {}).get("name")
        items.append(Item(kind, node["number"], labels, issue_type))
    return items


class GitHub:
    def __init__(self, repo: str, run: Callable[[list[str]], str] = run_gh, delay: float = 1.0):
        self.repo = repo
        self.run = run
        self.delay = delay  # GitHub asks for >=1s between mutating requests

    def _fetch(self, connection: str, kind: str, extra_fields: str) -> list[Item]:
        owner, name = self.repo.split("/")
        output = self.run(
            [
                "api", "graphql", "--paginate",
                "-f", f"owner={owner}",
                "-f", f"name={name}",
                "-f", f"query={ITEMS_QUERY % (connection, extra_fields)}",
                "--jq", f".data.repository.{connection}.nodes[] | @json",
            ]
        )  # fmt: skip
        return parse_items(kind, output)

    def fetch_items(self) -> list[Item]:
        return self._fetch("issues", "issue", "issueType { name }") + self._fetch("pullRequests", "pr", "")

    def fetch_label_names(self) -> set[str]:
        output = self.run(["api", "--paginate", f"repos/{self.repo}/labels", "--jq", ".[].name"])
        return {line for line in output.splitlines() if line}

    def apply(self, action) -> None:
        self.run(command_for(self.repo, action))
        time.sleep(self.delay)


def main(argv: list[str] | None = None, github_factory=GitHub, out=sys.stdout) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", default=DEFAULT_REPO)
    parser.add_argument("--labels-file", type=Path, default=DEFAULT_LABELS_FILE)
    parser.add_argument("--apply", action="store_true", help="perform writes (default: dry run)")
    phases = parser.add_subparsers(dest="phase", required=True)
    pre = phases.add_parser("pre", help="snapshot labels and fold secondary labels into primaries")
    pre.add_argument("--snapshot-dir", type=Path, default=Path.cwd())
    phases.add_parser("post", help="move type labels to Issue Types and delete dead labels")
    check = phases.add_parser("verify", help="compare current labels against a snapshot")
    check.add_argument("--snapshot", type=Path, required=True)
    args = parser.parse_args(argv)

    def say(message: str) -> None:
        print(message, file=out)

    renames = load_renames(args.labels_file.read_text())
    github = github_factory(args.repo)
    items = github.fetch_items()

    if args.phase == "verify":
        problems = verify(items_from_json(args.snapshot.read_text()), items, renames)
        for problem in problems:
            say(problem)
        say(f"{len(problems)} problem(s)")
        return 1 if problems else 0

    existing = github.fetch_label_names()
    if args.phase == "pre":
        conflicts = rename_conflicts(renames, existing)
        if conflicts:
            for conflict in conflicts:
                say(conflict)
            return 2
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        snapshot = args.snapshot_dir / f"label-snapshot-{stamp}.json"
        snapshot.write_text(items_to_json(items))
        say(f"snapshot: {snapshot} ({len(items)} items)")
        actions = plan_pre(items)
    else:
        pending = sorted(set(renames) & existing)
        if pending:
            say(f"labels not yet renamed by the sync workflow: {pending}")
            return 2
        stragglers = unmerged(items, renames)
        if stragglers:
            say("run the pre phase first; unmerged items: " + ", ".join(f"#{i.number}" for i in stragglers))
            return 2
        actions = plan_post(items, existing)

    for action in actions:
        say(("apply: " if args.apply else "dry-run: ") + action.describe())
        if args.apply:
            github.apply(action)
    say(f"{len(actions)} action(s) {'applied' if args.apply else 'planned'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python3 .github/scripts/test_migrate_labels.py && python3 .github/scripts/test_labels_yml.py`
Expected: both `OK`

- [ ] **Step 5: Read-only smoke test against the real repo**

This only reads from GitHub (no `--apply`). Snapshot goes to the scratchpad, not the repo.

Run: `python3 .github/scripts/migrate_labels.py pre --snapshot-dir "$TMPDIR"` (use the session scratchpad directory for `$TMPDIR`)
Expected: `snapshot: ... (≈4580 items)`, then about 25 `dry-run: #N: add label ...` lines (13 `sys-audio`, ≤11 `toolchain`, ≤1 `enhancement`), then `N action(s) planned`; exit 0.

Run: `python3 .github/scripts/migrate_labels.py post`
Expected: `labels not yet renamed by the sync workflow: [...]`; exit 2 (the sync hasn't run — this confirms the guard).

- [ ] **Step 6: Commit**

```bash
git add .github/scripts/migrate_labels.py .github/scripts/test_migrate_labels.py
git commit -m "Add gh I/O and CLI to label migration script"
```

---

### Task 4: Sync workflow and dependabot labels

**Files:**
- Create: `.github/workflows/sync-labels.yml`
- Modify: `.github/dependabot.yml:4-6`, `:18-20`, `:28-29`
- Modify: `.github/scripts/test_labels_yml.py` (add one test)

**Interfaces:**
- Consumes: `.github/labels.yml`, both test scripts.
- Produces: workflow `Sync labels` with jobs `test` and `sync`.

- [ ] **Step 1: Write the failing test**

Add to `LabelsFileTests` in `.github/scripts/test_labels_yml.py`:

```python
    def test_dependabot_uses_defined_labels(self):
        dependabot = yaml.safe_load((LABELS_FILE.parent / "dependabot.yml").read_text())
        for update in dependabot["updates"]:
            for label in update.get("labels", []):
                with self.subTest(ecosystem=update["package-ecosystem"], label=label):
                    self.assertIn(label, self.names)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 .github/scripts/test_labels_yml.py`
Expected: FAIL in `test_dependabot_uses_defined_labels` — `'pr-dependencies' not found in [...]`

- [ ] **Step 3: Update `.github/dependabot.yml`**

Lines 4-6 become:

```yaml
    labels:
      - "type: deps"
      - "area: ci"
```

Lines 18-20 become:

```yaml
    labels:
      - "type: deps"
      - "area: toolchain"
```

Lines 28-29 become:

```yaml
    labels:
      - "type: deps"
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 .github/scripts/test_labels_yml.py`
Expected: `OK` (10 tests)

- [ ] **Step 5: Create `.github/workflows/sync-labels.yml`**

```yaml
name: Sync labels

on:
  push:
    branches:
      - main
    paths:
      - .github/labels.yml
      - .github/workflows/sync-labels.yml
  pull_request:
    paths:
      - .github/labels.yml
      - .github/dependabot.yml
      - .github/scripts/migrate_labels.py
      - .github/scripts/test_labels_yml.py
      - .github/scripts/test_migrate_labels.py
      - .github/workflows/sync-labels.yml
  workflow_dispatch:

permissions:
  contents: read

jobs:
  test:
    name: Test label config and migration script
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v7
      - uses: actions/setup-python@v7
        with:
          python-version: "3.12"
      - run: pip install pyyaml
      - run: python .github/scripts/test_labels_yml.py
      - run: python .github/scripts/test_migrate_labels.py

  sync:
    name: Sync labels (dry run on PRs)
    needs: test
    runs-on: ubuntu-latest
    permissions:
      contents: read
      issues: write
    steps:
      - uses: actions/checkout@v7
      - uses: crazy-max/ghaction-github-labeler@v6
        with:
          github-token: ${{ secrets.GITHUB_TOKEN }}
          yaml-file: .github/labels.yml
          skip-delete: true
          dry-run: ${{ github.event_name == 'pull_request' }}
```

- [ ] **Step 6: Lint the workflow**

Run: `pre-commit run actionlint --files .github/workflows/sync-labels.yml`
Expected: `Lint GitHub Actions workflow files....Passed`

- [ ] **Step 7: Commit**

```bash
git add .github/workflows/sync-labels.yml .github/dependabot.yml .github/scripts/test_labels_yml.py
git commit -m "Add label sync workflow and update dependabot labels"
```

---

### Task 5: Contributor docs and spec touch-ups

**Files:**
- Modify: `docs/CONTRIBUTING.md` (insert after line 68, before `## Adding a Runtime Feature Setting`)
- Modify: `docs/superpowers/specs/2026-10-08-label-taxonomy-design.md`

**Interfaces:**
- Consumes: final file names from Tasks 1-4.
- Produces: documentation only.

- [ ] **Step 1: Add the Labels section to `docs/CONTRIBUTING.md`**

Insert before `## Adding a Runtime Feature Setting (Community Feature)`:

```markdown
## Labels

Labels are grouped by prefix:

* `type:` (pull requests only): `fix`, `feature`, `refactor`, `deps`, `chore`. Issues use GitHub's
  native **Issue Type** (Bug, Feature, Task) instead.
* `area:` which part of the firmware or project is affected, e.g. `area: audio`, `area: midi`,
  `area: toolchain`.
* `status:` where an issue or PR stands, e.g. `status: needs-info`, `status: needs-testing`.
* `impact:` how a bug shows up, e.g. `impact: crash`, `impact: ux-inconsistent`.

`release-blocker` and `beta-blocker` mark items that must be fixed before a release advances.
Maintainers also set the **Priority** and **Effort** issue fields when triaging.

The full list lives in [`.github/labels.yml`](../.github/labels.yml). To add or change a label, edit that
file in a pull request; it is synced to GitHub automatically when merged.

```

- [ ] **Step 2: Update the spec to match the implementation**

In `docs/superpowers/specs/2026-10-08-label-taxonomy-design.md`:

Replace the heading `### 3. \`.github/scripts/migrate-labels.py\`` with `### 3. \`.github/scripts/migrate_labels.py\``.

Replace:

```markdown
One-shot migration, run locally by a maintainer with an authenticated `gh`.
Python standard library only; shells out to `gh api` / `gh api graphql`.
```

with:

```markdown
One-shot migration, run locally by a maintainer with an authenticated `gh`.
Python standard library plus PyYAML (to read `labels.yml` rather than duplicating
its rename map); shells out to `gh api` / `gh api graphql`. Underscore filename so
the test module can import it.
```

Replace every remaining `migrate-labels.py` in the spec with `migrate_labels.py`:

Run: `sed -i 's/migrate-labels\.py/migrate_labels.py/g' docs/superpowers/specs/2026-10-08-label-taxonomy-design.md && grep -c migrate-labels docs/superpowers/specs/2026-10-08-label-taxonomy-design.md`
Expected: `0`

- [ ] **Step 3: Run the full test suite once more**

Run: `python3 .github/scripts/test_labels_yml.py && python3 .github/scripts/test_migrate_labels.py`
Expected: both `OK`

- [ ] **Step 4: Commit**

```bash
git add docs/CONTRIBUTING.md docs/superpowers/specs/2026-10-08-label-taxonomy-design.md
git commit -m "Document label taxonomy for contributors"
```

---

## After the plan: rollout (maintainer, manual — not executed by agents)

These steps write to the live repository and are run by a maintainer, in order:

1. Push the branch and open the PR. Check the `Sync labels (dry run on PRs)` job log for the planned renames/creates.
2. `python3 .github/scripts/migrate_labels.py --apply pre --snapshot-dir ~/label-migration` — keep the snapshot file.
3. Merge the PR. Confirm the `Sync labels` run on `main` succeeded.
4. `python3 .github/scripts/migrate_labels.py post` (dry run), review, then `--apply post`.
5. `python3 .github/scripts/migrate_labels.py verify --snapshot ~/label-migration/label-snapshot-<stamp>.json` → expect `0 problem(s)`.

The REST `type` field used by `SetIssueType` has not been exercised against this repo yet; if step 4 reports an error on the first `SetIssueType`, stop and check the request before continuing (re-running is safe).
