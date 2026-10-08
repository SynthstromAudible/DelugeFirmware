#! /usr/bin/env python3
"""Label a pull request from its description and leave one self-updating checklist comment.

Run by .github/workflows/label-prs.yml. Reads the PR from the event payload
($GITHUB_EVENT_PATH), adds the `type:` label for the ticked "Type of change" box, and
creates or edits a single comment listing anything missing from the description.
Never blocks a PR, skips drafts and bots, and only adds labels from a fixed mapping.

Requires an authenticated `gh` (GH_TOKEN).
"""

from __future__ import annotations

import re

MARKER = "<!-- pr-checklist -->"

SUMMARY = "Summary"
TYPE_SECTION = "Type of change"
TEST_SECTION = "What to test"

# Checkbox text in .github/PULL_REQUEST_TEMPLATE.md -> label.
TYPE_LABELS = {
    "Bug fix": "type: fix",
    "New feature or changed behavior": "type: feature",
    "Refactor (no behavior change)": "type: refactor",
    "Chore (build, CI, docs, tooling)": "type: chore",
}

SECTION = re.compile(r"^## (.+?)[ \t\r]*$", re.MULTILINE)
HTML_COMMENT = re.compile(r"<!--.*?-->", re.DOTALL)
CHECKBOX = re.compile(r"^[ \t]*[-*][ \t]+\[([ xX])\][ \t]+(.+?)[ \t\r]*$", re.MULTILINE)


def parse_sections(body: str | None) -> dict[str, str]:
    """Heading -> text with HTML comments removed. The first occurrence of a heading wins."""
    parts = SECTION.split(body or "")  # [preamble, heading, text, heading, text, ...]
    sections: dict[str, str] = {}
    for heading, text in zip(parts[1::2], parts[2::2]):
        sections.setdefault(heading, HTML_COMMENT.sub("", text).strip())
    return sections


def checked_types(sections: dict[str, str]) -> list[str]:
    """`type:` labels for the ticked boxes, in template order."""
    ticked = {
        text
        for mark, text in CHECKBOX.findall(sections.get(TYPE_SECTION, ""))
        if mark.lower() == "x"
    }
    return [label for text, label in TYPE_LABELS.items() if text in ticked]


def problems(sections: dict[str, str], has_type_label: bool) -> list[str]:
    found = []
    if not has_type_label:
        count = len(checked_types(sections))
        if count == 0:
            found.append("Tick one box under **Type of change**.")
        elif count > 1:
            found.append(
                "More than one box is ticked under **Type of change**; please keep just one."
            )
    if not sections.get(SUMMARY):
        found.append("Add a short **Summary** of what changes and why.")
    if not sections.get(TEST_SECTION):
        found.append(
            "Add **What to test**: the areas touched and a short test manual (or N/A for docs-only changes)."
        )
    return found


def comment_body(found: list[str]) -> str:
    if not found:
        return f"{MARKER}\nThanks, the PR description looks complete."
    bullets = "\n".join(f"- {item}" for item in found)
    return (
        f"{MARKER}\nThanks for the PR! A few things would help reviewers:\n\n{bullets}\n\n"
        "This comment updates itself when you edit the description."
    )
