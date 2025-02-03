import argparse
import os
from pathlib import Path
import util
import re

COMMUNITY_FEATURES_DOC = Path("docs/community_features.md")


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="checkprdoc", description="Run deluge tests")
    parser.group = "Miscellaneous"
    return parser


def main() -> int:
    (args, unknown_args) = argparser().parse_known_args()

    os.chdir(util.get_git_root())

    with open(COMMUNITY_FEATURES_DOC, "r", encoding="utf-8") as file:
        content = file.read()

    # Find all PR references in the format [#1234]
    pr_numbers = re.findall(r"\[#(\d+)\][^:]", content)

    # Check if the corresponding PR links exist
    for pr_number in pr_numbers:
        if not re.search(rf"\[#{pr_number}\]: https", content):
            print(
                f"[#{pr_number}]: https://github.com/SynthstromAudible/DelugeFirmware/pull/{pr_number}"
            )

    exit(0)
