"""The cards a level-3 UI script runs on, read from its `# card:` header.

A script names each card it runs on, one line each, before its first step:

    # card: tests/fixtures/card                      a directory in this repository
    # card: tests/fixtures/card-dx-no-folder.card    a card manifest (src/bsp/rust/src/storage/card_manifest.rs)
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
# The script directories under tests/ui-scripts whose scripts run on a card.
CARD_DIRS = ("app", "device", "parity", "parity-card", "parity-songs", "selftest")
# Level-2 scripts: the shell alone, with no card, named by the specs that run them.
SHELL_DIRS = ("shell",)
# The ones the ui-scripts gate runs, in CI.
GATE_DIRS = ("app", "selftest")
# Any line that tries to be a card line, so a misspelt one is reported rather than read as a comment.
CARD_LIKE = re.compile(r"^\s*#\s*card\s*:", re.IGNORECASE)
NUMBERED = re.compile(r"^[a-z]{1,2}\d{2}-[a-z0-9]+(?:-[a-z0-9]+)*\.txt$")


def header(script: Path) -> list[str]:
    """The lines of `script` before its first step."""
    found = []
    for line in script.read_text().splitlines():
        if line.strip() and not line.lstrip().startswith("#"):
            break
        found.append(line)
    return found


def cards(script: Path) -> list[str]:
    """The cards `script`'s header names, in order: its `# card:` lines before its first step."""
    return [m["card"] for line in header(script) if (m := HEADER.match(line))]


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
    """`card` as files: a card manifest built into `out` (replacing whatever is there) by the BSP's `card-manifest`
    tool; anything else, a directory or an image, as it stands. cargo runs in src/bsp/rust, whose rust-toolchain.toml
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
    """Every script in CARD_DIRS without a numbered name or a `# card:` header, with a malformed or repeated card line,
    or naming a card it may not use, that is not in the repository or that shares a label with another card; and
    every script directory that is in neither CARD_DIRS nor SHELL_DIRS."""
    found = []
    base = repo / "tests/ui-scripts"
    labels: dict[str, set[str]] = {}
    for directory in CARD_DIRS:
        for script in sorted((base / directory).glob("*.txt")):
            where = script.relative_to(repo)
            if not NUMBERED.match(script.name):
                found.append(f"{where}: not named <letters><two digits>-<words>.txt")
            found += [
                f"{where}: malformed `# card:` line: {line!r}"
                for line in header(script)
                if CARD_LIKE.match(line) and not HEADER.match(line)
            ]
            named = cards(script)
            if not named:
                found.append(f"{where}: no `# card:` header")
            found += [
                f"{where}: {card}: named twice"
                for card in sorted({c for c in named if named.count(c) > 1})
            ]
            for card in named:
                labels.setdefault(label(card), set()).add(card)
                found += _card_problems(card, directory, repo, where)
    found += [
        f"{name}: one label for several cards: {', '.join(sorted(named))}"
        for name, named in sorted(labels.items())
        if len(named) > 1
    ]
    if base.is_dir():
        known = {*CARD_DIRS, *SHELL_DIRS, "__pycache__"}
        found += [
            f"{d.relative_to(repo)}: not a known script directory: add it to CARD_DIRS or SHELL_DIRS"
            for d in sorted(base.iterdir())
            if d.is_dir() and d.name not in known
        ]
    return found


def _card_problems(card: str, directory: str, repo: Path, where: Path) -> list[str]:
    """What is wrong with one card a script in `directory` names."""
    if is_content(card):
        name = card.removeprefix(CONTENT)
        if directory in GATE_DIRS:
            return [
                f"{where}: {card}: the ui-scripts gate runs in CI, which has no content card"
            ]
        if not name or "/" in name:
            return [f"{where}: {card}: a content card is content/<name>"]
        return []
    path = Path(card)
    tests = (repo / "tests").resolve()
    if (
        path.is_absolute()
        or ".." in path.parts
        or not (repo / card).resolve().is_relative_to(tests)
    ):
        return [f"{where}: {card}: a repository card is a path under tests/"]
    if not (repo / card).is_dir() and not (
        card.endswith(".card") and (repo / card).is_file()
    ):
        return [f"{where}: {card}: no such directory or card manifest"]
    return []
