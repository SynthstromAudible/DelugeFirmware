#!/usr/bin/env python3
"""Compare the two legs of the level-3 A/B: the same script run with ShellMenu off and on.

`scripts/ui_snapshot_diff.py` compares a run against its own golden, which pins each leg but says
nothing about the two legs' *relationship*. This says the thing the branch actually claims:

  1. **The two legs ran the same script.** Same snapshot file names, or the comparison is between
     two different things.
  2. **Every OLED panel is byte-identical.** This is the branch's headline result -- the ported
     `MenuScene` draws `SoundEditor`'s pixels -- and it is the assertion that goes red if a port
     change moves a pixel.
  3. **At least one `.state` file differs.** Without this the suite would pass forever on an A/B
     whose two legs were the same code path: a setting that never took, a fixture whose
     `CommunityFeatures.XML` stopped parsing, or a golden re-recorded twice from one leg. A script
     that presses nothing passes points 1 and 2 with `shellMenuEnabled()` forced either way.
  4. **Optionally, that the two SD fixtures differ by exactly one named file.** The ShellMenu-on leg
     is the default card, with no settings file; the ShellMenu-off leg is the same card plus the
     file that turns the Legacy Menu on. The legs are only an A/B on that setting if their inputs
     are otherwise identical, and the two fixture trees are committed copies that can drift apart
     silently.

The expected `.state` differences are NOT enumerated here. They are recorded in the `menu` and
`menu_legacy` goldens (`tests/golden/ui/app`), which pin each leg byte-for-byte. Restating them would
be a third copy that goes stale.

Usage:
    ui_app_ab_check.py <off-tree> <on-tree>
                       [--fixture-off DIR --fixture-on DIR --toggle-file RELPATH]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def _tree(root: Path) -> dict[str, bytes]:
    """Every file under `root`, keyed by its path relative to `root`."""
    return {
        str(p.relative_to(root)): p.read_bytes()
        for p in sorted(root.rglob("*"))
        if p.is_file()
    }


def _check_fixtures(off: Path, on: Path, toggle: str, fail) -> None:
    off_files = _tree(off)
    on_files = _tree(on)

    if toggle in on_files:
        fail(
            f"the ShellMenu-on fixture {on} contains {toggle}; the default card has none"
        )
    if toggle not in off_files:
        fail(
            f"the ShellMenu-off fixture {off} is missing {toggle}; it is what selects the Legacy Menu"
        )
    off_files.pop(toggle, None)

    only_off = sorted(set(off_files) - set(on_files))
    only_on = sorted(set(on_files) - set(off_files))
    for name in only_off:
        fail(f"fixture drift: {name} exists only in {off}")
    for name in only_on:
        fail(f"fixture drift: {name} exists only in {on}")
    for name in sorted(set(off_files) & set(on_files)):
        if off_files[name] != on_files[name]:
            fail(f"fixture drift: {name} differs between the two SD roots")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("off", type=Path, help="snapshot tree from the ShellMenu-off run")
    ap.add_argument("on", type=Path, help="snapshot tree from the ShellMenu-on run")
    ap.add_argument("--fixture-off", type=Path, help="SD root the off leg booted from")
    ap.add_argument("--fixture-on", type=Path, help="SD root the on leg booted from")
    ap.add_argument(
        "--toggle-file",
        default="SETTINGS/CommunityFeatures.XML",
        help="the one file the two SD roots may differ by",
    )
    args = ap.parse_args()

    errors: list[str] = []

    def fail(msg: str) -> None:
        errors.append(msg)

    for tree in (args.off, args.on):
        if not tree.is_dir():
            print(f"ui_app_ab_check: no such directory: {tree}", file=sys.stderr)
            return 2

    if args.fixture_off is not None or args.fixture_on is not None:
        if args.fixture_off is None or args.fixture_on is None:
            print(
                "ui_app_ab_check: --fixture-off and --fixture-on go together",
                file=sys.stderr,
            )
            return 2
        _check_fixtures(args.fixture_off, args.fixture_on, args.toggle_file, fail)

    off_files = _tree(args.off)
    on_files = _tree(args.on)

    # (1) the same script, or nothing below means anything.
    for name in sorted(set(off_files) - set(on_files)):
        fail(f"{name}: written by the OFF leg only")
    for name in sorted(set(on_files) - set(off_files)):
        fail(f"{name}: written by the ON leg only")

    shared = sorted(set(off_files) & set(on_files))
    if not shared:
        fail("the two legs share no snapshot at all")

    # (2) and (3). `pbm_same` is counted, not inferred from `len(...)`: an earlier version printed
    # the number of `.pbm` files under the label "identical .pbm panels", so a run that FAILED for
    # differing pixels still reported all five as identical. A summary line that cannot contradict
    # the failure above it is worse than no summary line.
    state_differs: list[str] = []
    pbm_total = 0
    pbm_same = 0
    for name in shared:
        same = off_files[name] == on_files[name]
        if name.endswith(".pbm"):
            pbm_total += 1
            pbm_same += 1 if same else 0
            if not same:
                fail(f"{name}: the two legs render different pixels")
        if name.endswith(".state") and not same:
            state_differs.append(name)

    if not state_differs:
        fail(
            "the two legs are identical in every .state file -- the setting did nothing, so this "
            "A/B proves nothing (see this file's docstring, point 3)"
        )

    print(f"ui_app_ab_check: {len(shared)} snapshot files compared")
    print(f"  identical .pbm panels: {pbm_same} of {pbm_total}")
    print(f"  .state files that differ: {len(state_differs)}")
    for name in state_differs:
        print(f"    {name}")

    for msg in errors:
        print(f"ui_app_ab_check: FAIL: {msg}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
