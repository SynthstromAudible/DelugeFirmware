"""The cards a level-3 UI script runs on, read from its `# card:` header.

A script names each card it runs on, one line each, before its first step:

    # card: tests/fixtures/card                      a directory in this repository
    # card: tests/fixtures/card-old-shell-menu.card  a card manifest (src/bsp/rust/src/storage/card_manifest.rs)
    # card: content/sweep-bare.img                   a content card: a directory or FAT image in the content-card
                                                     directory (DELUGE_CONTENT_CARDS, default .superpowers/emu-card)

A runner plays the script once per card. On golden_vt_render (the ui-scripts gate) the first card's snapshots are
the golden's and every further card must reproduce them; tools/emu_parity.py compares the two images on each card.
Content cards hold presets, kits, samples and songs that are not in this repository, so only a developer's machine
has them; a script the ui-scripts gate runs (app/, selftest/) names repository cards only.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HEADER = re.compile(r"^# card: (?P<card>\S+)\s*$")
CONTENT = "content/"
CONTENT_DIR = ".superpowers/emu-card"
# The script directories under tests/ui-scripts whose scripts run on a card. shell/ is level 2: the shell alone, with
# no card, its scripts named by the specs that run them.
CARD_DIRS = ("app", "device", "parity", "parity-card", "parity-songs", "selftest")
# The ones the ui-scripts gate runs, in CI.
GATE_DIRS = ("app", "selftest")
NUMBERED = re.compile(r"^[a-z]{1,2}\d{2}-[a-z0-9]+(?:-[a-z0-9]+)*\.txt$")


def cards(script: Path) -> list[str]:
    """The cards `script`'s header names, in order: its `# card:` lines before its first step."""
    found = []
    for line in script.read_text().splitlines():
        if m := HEADER.match(line):
            found.append(m["card"])
        elif line.strip() and not line.lstrip().startswith("#"):
            break
    return found


def content_root(repo: Path = REPO) -> Path:
    """Where content cards are: DELUGE_CONTENT_CARDS, or CONTENT_DIR in the repository."""
    return Path(os.environ.get("DELUGE_CONTENT_CARDS") or repo / CONTENT_DIR)


def is_content(card: str) -> bool:
    return card.startswith(CONTENT)


def resolve(card: str, repo: Path = REPO) -> Path:
    """The path a header's card names."""
    if is_content(card):
        return content_root(repo) / card.removeprefix(CONTENT)
    return repo / card


def label(card: str) -> str:
    """A leg's name: the card's last component without `.card` or `.img`, `content-` before a content card's."""
    name = Path(card).name.removesuffix(".card").removesuffix(".img")
    return f"content-{name}" if is_content(card) else name


def tree(card: Path, out: Path, repo: Path = REPO) -> Path:
    """`card` as a directory of files: a directory as it stands, or a card manifest built into `out` (replacing
    whatever is there) by the BSP's `card-manifest` tool. cargo runs in src/bsp/rust, whose rust-toolchain.toml
    chooses the toolchain; its first run builds the BSP's host library."""
    if card.suffix != ".card":
        return card
    shutil.rmtree(out, ignore_errors=True)
    subprocess.run(
        [
            "cargo",
            "run",
            "--quiet",
            "--locked",
            "--bin",
            "card-manifest",
            "--",
            str(card.resolve()),
            str(out.resolve()),
        ],
        cwd=repo / "src/bsp/rust",
        check=True,
    )
    return out


def problems(repo: Path = REPO) -> list[str]:
    """Every script in CARD_DIRS without a numbered name or a `# card:` header, or naming a card it may not use or
    that is not in the repository."""
    found = []
    for directory in CARD_DIRS:
        for script in sorted((repo / "tests/ui-scripts" / directory).glob("*.txt")):
            where = script.relative_to(repo)
            if not NUMBERED.match(script.name):
                found.append(f"{where}: not named <letters><two digits>-<words>.txt")
            named = cards(script)
            if not named:
                found.append(f"{where}: no `# card:` header")
            for card in named:
                name = card.removeprefix(CONTENT)
                if is_content(card):
                    if directory in GATE_DIRS:
                        found.append(
                            f"{where}: {card}: the ui-scripts gate runs in CI, which has no content card"
                        )
                    elif not name or "/" in name:
                        found.append(
                            f"{where}: {card}: a content card is content/<name>"
                        )
                elif not (repo / card).is_dir() and not (
                    card.endswith(".card") and (repo / card).is_file()
                ):
                    found.append(f"{where}: {card}: no such directory or card manifest")
    return found
