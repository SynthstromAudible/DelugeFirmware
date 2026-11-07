"""Stamp a parity card's pre-4.1 presets as official 4.1.3, so `main` reads them as our line does.

`main` converts a preset older than official 4.1 as it loads it (resonance volume compensation, the default
expression cables); our line leaves both to `tools/deluge-upgrade`. A card both images read must hold no such
preset, or every comparison that touches one shows the conversion instead of the UI. Only `SYNTHS/` and `KITS/` are
stamped: the cards' songs are current already. A community stamp (`c1.x`) is left alone.

Usage: stamp_preset_versions.py DIR_OR_IMAGE [DIR_OR_IMAGE ...]   (an image is rewritten in place, through mtools)
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

STAMP = b"4.1.3"
PRESET_DIRS = ("SYNTHS", "KITS")
VERSION = re.compile(
    rb'<firmwareVersion>([^<]*)</firmwareVersion>|firmwareVersion="([^"]*)"'
)
DECLARATION = re.compile(rb"\s*<\?xml[^>]*\?>\s*")


def is_old(version: bytes | None) -> bool:
    """True for no stamp, or an official stamp below 4.1."""
    if version is None:
        return True
    if version.startswith(b"c"):
        return False
    m = re.match(rb"(\d+)\.(\d+)", version)
    return m is None or (int(m[1]), int(m[2])) < (4, 1)


def stamp(data: bytes) -> bytes | None:
    """The file stamped 4.1.3, or None when it needs no stamp."""
    m = VERSION.search(data)
    group = None if m is None else (1 if m[1] is not None else 2)
    if not is_old(None if m is None else m[group]):
        return None
    if m is None:
        decl = DECLARATION.match(data)
        at = decl.end() if decl else 0
        return (
            data[:at]
            + b"<firmwareVersion>"
            + STAMP
            + b"</firmwareVersion>\n"
            + data[at:]
        )
    return data[: m.start(group)] + STAMP + data[m.end(group) :]


def stamp_dir(root: Path) -> int:
    count = 0
    for sub in PRESET_DIRS:
        for path in sorted((root / sub).rglob("*")) if (root / sub).is_dir() else []:
            if path.suffix.upper() != ".XML":
                continue
            out = stamp(path.read_bytes())
            if out is not None:
                path.write_bytes(out)
                count += 1
    return count


def stamp_image(img: Path) -> int:
    with tempfile.TemporaryDirectory() as d:
        work = Path(d)
        listing = subprocess.run(
            ["mdir", "-b", "-i", str(img), "::/"],
            check=True,
            capture_output=True,
            text=True,
        )
        present = [s for s in PRESET_DIRS if f"::/{s}/" in listing.stdout.split()]
        for sub in present:
            subprocess.run(
                ["mcopy", "-s", "-n", "-i", str(img), f"::/{sub}", str(work)],
                check=True,
            )
        count = stamp_dir(work)
        for sub in present:
            subprocess.run(
                ["mcopy", "-s", "-o", "-i", str(img), str(work / sub), "::/"],
                check=True,
            )
        return count


def main(argv: list[str]) -> int:
    for arg in argv:
        target = Path(arg)
        count = stamp_dir(target) if target.is_dir() else stamp_image(target)
        print(f"{target}: {count} preset(s) stamped {STAMP.decode()}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
