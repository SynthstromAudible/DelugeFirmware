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
    return {
        entry["from_name"]: entry["name"]
        for entry in yaml.safe_load(yaml_text)
        if "from_name" in entry
    }


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
        targets = {
            MERGES[label] for label in item.labels if label in MERGES
        } - item.labels
        actions += [AddLabel(item.number, target) for target in sorted(targets)]
    return actions


def unmerged(items: list[Item], renames: dict[str, str]) -> list[Item]:
    """Items still carrying a secondary label without its primary under either name."""
    result = []
    for item in items:
        for label in item.labels & MERGES.keys():
            target = MERGES[label]
            if (
                target not in item.labels
                and renames.get(target, target) not in item.labels
            ):
                result.append(item)
                break
    return result


def choose_issue_type(labels: frozenset[str]) -> str | None:
    for label, issue_type in ISSUE_TYPE_FROM_LABEL:
        if label in labels:
            return issue_type
    return None


def plan_post(
    items: list[Item], existing: set[str]
) -> list[SetIssueType | RemoveLabel | DeleteLabel]:
    """Move type-like labels on issues to Issue Types, then delete dead labels."""
    actions: list[SetIssueType | RemoveLabel | DeleteLabel] = []
    for item in items:
        if item.kind != "issue":
            continue
        issue_type = choose_issue_type(item.labels)
        if issue_type and item.issue_type is None:
            actions.append(SetIssueType(item.number, issue_type))
        actions += [
            RemoveLabel(item.number, label)
            for label in sorted(item.labels & ISSUE_TYPE_LABELS)
        ]
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


def verify(
    snapshot: list[Item], current: list[Item], renames: dict[str, str]
) -> list[str]:
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
            {
                "kind": i.kind,
                "number": i.number,
                "labels": sorted(i.labels),
                "issue_type": i.issue_type,
            }
            for i in items
        ],
        indent=1,
    )


def items_from_json(text: str) -> list[Item]:
    return [
        Item(d["kind"], d["number"], frozenset(d["labels"]), d["issue_type"])
        for d in json.loads(text)
    ]
