#! /usr/bin/env python3
"""Request backports on a fix PR from the issues it closes.

Run by .github/workflows/backport-from-issue.yml. For a PR to main, finds the issues it
closes ("Fixes #123" or the sidebar link) and, for each `affects: release/X.Y` label on
them, adds `backport: release/X.Y` to the PR. Labels are only ever added, and a PR labelled `backport: declined` is left alone, so a maintainer can
always override. The merge-time workflow (backport.yml) does the actual cherry-pick.

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

AFFECTS = re.compile(r"^affects: (release/\d+\.\d+)$")
DECLINED = "backport: declined"

QUERY = """
query($owner: String!, $repo: String!, $number: Int!) {
  repository(owner: $owner, name: $repo) {
    pullRequest(number: $number) {
      closingIssuesReferences(first: 20) {
        nodes { number labels(first: 50) { nodes { name } } }
      }
    }
  }
}
"""


def run_gh(args: list[str]) -> str:
    return subprocess.run(
        ["gh", *args], check=True, capture_output=True, text=True
    ).stdout


def backport_labels(issue_labels: list[list[str]]) -> list[str]:
    """`backport:` labels for the `affects:` labels across the closed issues, deduplicated."""
    wanted: list[str] = []
    for labels in issue_labels:
        for name in labels:
            match = AFFECTS.match(name)
            if match and f"backport: {match[1]}" not in wanted:
                wanted.append(f"backport: {match[1]}")
    return wanted


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
    owner, name = repo.split("/")
    try:
        # The payload can be stale after a quick edit, so ask for the current labels.
        current = set(
            run(
                ["api", f"repos/{repo}/issues/{number}/labels", "--jq", ".[].name"]
            ).split("\n")
        )
        if DECLINED in current:
            print(f"#{number}: backport declined, skipping", file=out)
            return 0
        data = json.loads(
            run(
                [
                    "api",
                    "graphql",
                    "-f",
                    f"query={QUERY}",
                    "-f",
                    f"owner={owner}",
                    "-f",
                    f"repo={name}",
                    "-F",
                    f"number={number}",
                ]
            )
        )
        issues = data["data"]["repository"]["pullRequest"]["closingIssuesReferences"][
            "nodes"
        ]
        wanted = [
            label
            for label in backport_labels(
                [[l["name"] for l in issue["labels"]["nodes"]] for issue in issues]
            )
            if label not in current
        ]
        for label in wanted:
            run(
                [
                    "api",
                    "-X",
                    "POST",
                    f"repos/{repo}/issues/{number}/labels",
                    "-f",
                    f"labels[]={label}",
                ]
            )
            print(f"#{number}: added {label}", file=out)
    except subprocess.CalledProcessError as error:
        # Never fail the job over labeling; the log has the details.
        print(f"::warning::#{number}: {error} {error.stderr or ''}".strip(), file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
