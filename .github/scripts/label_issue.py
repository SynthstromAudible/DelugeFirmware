#! /usr/bin/env python3
"""Apply area:/impact: labels to a new issue from its bug report form answers.

Run by .github/workflows/label-issues.yml on `issues: opened`. Reads the issue from the
event payload ($GITHUB_EVENT_PATH), maps the dropdown answers using
.github/issue-form-labels.yml, and adds the labels with the `gh` CLI. Only labels from
the mapping are ever added, and nothing is ever removed.

Requires PyYAML and, to apply labels, an authenticated `gh` (GH_TOKEN).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from collections.abc import Callable, Mapping
from pathlib import Path

import yaml

DEFAULT_MAPPING_FILE = Path(__file__).resolve().parents[1] / "issue-form-labels.yml"

# GitHub renders each form field as "### <label>" followed by the answer.
HEADING = re.compile(r"^### (.+?)[ \t\r]*$", re.MULTILINE)


def load_mapping(text: str) -> dict[str, dict[str, str | None]]:
    return yaml.safe_load(text)


def parse_form_answers(body: str | None) -> dict[str, str]:
    """Heading -> answer. The last occurrence of a heading wins.

    The mapped dropdowns are the last fields in the form, so a heading typed into an
    earlier free-text answer can never override the real answer.
    """
    parts = HEADING.split(
        body or ""
    )  # [preamble, heading, answer, heading, answer, ...]
    answers: dict[str, str] = {}
    for heading, text in zip(parts[1::2], parts[2::2]):
        answers[heading] = text.strip()
    return answers


def labels_for(
    answers: dict[str, str], mapping: dict[str, dict[str, str | None]]
) -> list[str]:
    """Labels for the answers, in mapping order, without duplicates."""
    if not all(field in answers for field in mapping):
        return []  # not submitted through the form: a form always renders every field
    labels: list[str] = []
    for field, options in mapping.items():
        label = options.get(answers.get(field))
        if label and label not in labels:
            labels.append(label)
    return labels


def label_command(repo: str, number: int, labels: list[str]) -> list[str]:
    command = ["api", "-X", "POST", f"repos/{repo}/issues/{number}/labels"]
    for label in labels:
        command += ["-f", f"labels[]={label}"]
    return command


def run_gh(args: list[str]) -> str:
    return subprocess.run(
        ["gh", *args], check=True, capture_output=True, text=True
    ).stdout


def main(
    argv: list[str] | None = None,
    run: Callable[[list[str]], str] = run_gh,
    environ: Mapping[str, str] = os.environ,
    out=sys.stdout,
) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--mapping-file", type=Path, default=DEFAULT_MAPPING_FILE)
    args = parser.parse_args(argv)

    event = json.loads(Path(environ["GITHUB_EVENT_PATH"]).read_text())
    issue = event["issue"]
    mapping = load_mapping(args.mapping_file.read_text())
    labels = labels_for(parse_form_answers(issue.get("body")), mapping)
    if not labels:
        print(f"#{issue['number']}: no labels to apply", file=out)
        return 0
    run(label_command(environ["GITHUB_REPOSITORY"], issue["number"], labels))
    print(f"#{issue['number']}: added {labels}", file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
