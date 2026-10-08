#! /usr/bin/env python3
"""Apply area:/impact: labels to a new issue from its bug report form answers.

Run by .github/workflows/label-issues.yml on `issues: opened`. Reads the issue from the
event payload ($GITHUB_EVENT_PATH), maps the dropdown answers using
.github/issue-form-labels.yml, and adds the labels with the `gh` CLI. Only labels from
the mapping are ever added, and nothing is ever removed.

Requires PyYAML and, to apply labels, an authenticated `gh` (GH_TOKEN).
"""

from __future__ import annotations

import re
from pathlib import Path

import yaml

DEFAULT_MAPPING_FILE = Path(__file__).resolve().parents[1] / "issue-form-labels.yml"

# GitHub renders each form field as "### <label>" followed by the answer.
HEADING = re.compile(r"^### (.+?)[ \t\r]*$", re.MULTILINE)


def load_mapping(text: str) -> dict[str, dict[str, str | None]]:
    return yaml.safe_load(text)


def parse_form_answers(body: str | None) -> dict[str, str]:
    """Heading -> answer. The first occurrence of a heading wins."""
    parts = HEADING.split(
        body or ""
    )  # [preamble, heading, answer, heading, answer, ...]
    answers: dict[str, str] = {}
    for heading, text in zip(parts[1::2], parts[2::2]):
        answers.setdefault(heading, text.strip())
    return answers


def labels_for(
    answers: dict[str, str], mapping: dict[str, dict[str, str | None]]
) -> list[str]:
    """Labels for the answers, in mapping order, without duplicates."""
    labels: list[str] = []
    for field, options in mapping.items():
        label = options.get(answers.get(field))
        if label and label not in labels:
            labels.append(label)
    return labels
