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


def already_renamed(renames: dict[str, str], existing: set[str]) -> list[str]:
    """Old names whose rename has already happened (new exists, old is gone)."""
    return sorted(
        old for old, new in renames.items() if new in existing and old not in existing
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
    return subprocess.run(
        ["gh", *args], check=True, capture_output=True, text=True
    ).stdout


def command_for(repo: str, action) -> list[str]:
    match action:
        case AddLabel(number, label):
            return [
                "api",
                "-X",
                "POST",
                f"repos/{repo}/issues/{number}/labels",
                "-f",
                f"labels[]={label}",
            ]
        case RemoveLabel(number, label):
            return [
                "api",
                "-X",
                "DELETE",
                f"repos/{repo}/issues/{number}/labels/{quote(label, safe='')}",
            ]
        case SetIssueType(number, issue_type):
            return [
                "api",
                "-X",
                "PATCH",
                f"repos/{repo}/issues/{number}",
                "-f",
                f"type={issue_type}",
                "--jq",
                ".type.name",
            ]
        case DeleteLabel(name):
            return [
                "api",
                "-X",
                "DELETE",
                f"repos/{repo}/labels/{quote(name, safe='')}",
            ]
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
    def __init__(
        self, repo: str, run: Callable[[list[str]], str] = run_gh, delay: float = 1.0
    ):
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
        return self._fetch("issues", "issue", "issueType { name }") + self._fetch(
            "pullRequests", "pr", ""
        )

    def fetch_label_names(self) -> set[str]:
        output = self.run(
            ["api", "--paginate", f"repos/{self.repo}/labels", "--jq", ".[].name"]
        )
        return {line for line in output.splitlines() if line}

    def apply(self, action) -> None:
        output = self.run(command_for(self.repo, action))
        if isinstance(action, SetIssueType) and output.strip() != action.issue_type:
            # GitHub answers 200 but silently drops type changes it did not accept.
            raise RuntimeError(
                f"#{action.number}: Issue Type {action.issue_type!r} did not stick; got {output.strip()!r}"
            )
        time.sleep(self.delay)


def main(argv: list[str] | None = None, github_factory=GitHub, out=sys.stdout) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--repo", default=DEFAULT_REPO)
    parser.add_argument("--labels-file", type=Path, default=DEFAULT_LABELS_FILE)
    parser.add_argument(
        "--apply", action="store_true", help="perform writes (default: dry run)"
    )
    phases = parser.add_subparsers(dest="phase", required=True)
    pre = phases.add_parser(
        "pre", help="snapshot labels and fold secondary labels into primaries"
    )
    pre.add_argument("--snapshot-dir", type=Path, default=Path.cwd())
    phases.add_parser(
        "post", help="move type labels to Issue Types and delete dead labels"
    )
    check = phases.add_parser(
        "verify", help="compare current labels against a snapshot"
    )
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
        renamed = already_renamed(renames, existing)
        if renamed:
            say(
                f"the sync has already renamed {renamed}; running pre now would recreate them. Use the post phase."
            )
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
            say(
                "run the pre phase first; unmerged items: "
                + ", ".join(f"#{i.number}" for i in stragglers)
            )
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
