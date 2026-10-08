#! /usr/bin/env python3
"""Label a pull request from its description and leave one self-updating checklist comment.

Run by .github/workflows/label-prs.yml. Reads the PR from the event payload
($GITHUB_EVENT_PATH), adds the `type:` label for the ticked "Type of change" box, and
creates or edits a single comment listing anything missing from the description.
Never blocks a PR, skips drafts and bots, and only adds labels from a fixed mapping.

Requires an authenticated `gh` (GH_TOKEN).
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


def run_gh(args: list[str]) -> str:
    return subprocess.run(
        ["gh", *args], check=True, capture_output=True, text=True
    ).stdout


def find_comment(
    run: Callable[[list[str]], str], repo: str, number: int
) -> tuple[int, str] | None:
    """The id and body of this script's checklist comment, if it already exists."""
    output = run(
        [
            "api",
            "--paginate",
            f"repos/{repo}/issues/{number}/comments",
            "--jq",
            f'.[] | select(.body | startswith("{MARKER}")) | {{id, body}} | @json',
        ]
    )
    for line in output.splitlines():
        if line.strip():
            found = json.loads(line)
            return found["id"], found["body"]
    return None


def main(
    argv: list[str] | None = None,
    run: Callable[[list[str]], str] = run_gh,
    environ: Mapping[str, str] = os.environ,
    out=sys.stdout,
) -> int:
    argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    ).parse_args(argv)

    pr = json.loads(Path(environ["GITHUB_EVENT_PATH"]).read_text())["pull_request"]
    number, repo = pr["number"], environ["GITHUB_REPOSITORY"]
    if pr.get("draft") or pr["user"]["type"] == "Bot":
        print(f"#{number}: draft or bot PR, skipping", file=out)
        return 0

    sections = parse_sections(pr.get("body"))
    has_type_label = any(
        label["name"].startswith("type: ") for label in pr.get("labels", [])
    )
    chosen = checked_types(sections)
    if not has_type_label and len(chosen) == 1:
        run(
            [
                "api",
                "-X",
                "POST",
                f"repos/{repo}/issues/{number}/labels",
                "-f",
                f"labels[]={chosen[0]}",
            ]
        )
        print(f"#{number}: added {chosen[0]}", file=out)
        has_type_label = True

    found = problems(sections, has_type_label)
    text = comment_body(found)
    existing = find_comment(run, repo, number)
    if existing is None:
        if found:  # never open with a thank-you
            run(
                [
                    "api",
                    "-X",
                    "POST",
                    f"repos/{repo}/issues/{number}/comments",
                    "-f",
                    f"body={text}",
                ]
            )
            print(f"#{number}: posted checklist comment", file=out)
    elif existing[1] != text:
        run(
            [
                "api",
                "-X",
                "PATCH",
                f"repos/{repo}/issues/comments/{existing[0]}",
                "-f",
                f"body={text}",
            ]
        )
        print(f"#{number}: updated checklist comment", file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
