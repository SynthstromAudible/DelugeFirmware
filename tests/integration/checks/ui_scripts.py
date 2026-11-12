"""The level-3 UI scripts on golden_vt_render.

Every app script in tests/ui-scripts/app runs and each of its snapshots must match the golden in tests/golden/ui/app.
The menu script runs again on two cards that still carry a menu setting this firmware no longer has
(tests/fixtures/card-old-shell-menu says shellMenu=0, tests/fixtures/card-old-legacy-menu says legacyMenu=1): each is
an unknown setting, so every snapshot must equal the default leg's. The runner's own self-tests (tests/ui-scripts/selftest) pin what it does with a press due
in a snapshot's millisecond, a snapshot before the first frame and a script that never quits. A script with a `board`
step needs more panels than the Deluge has and is skipped, named.
"""

import re
import subprocess
import sys
from pathlib import Path

BOARD_STEP = re.compile(r"^\s*\d+\s+board\b", re.MULTILINE)


def _run(gate, script: Path, got: Path, card: Path):
    return gate.run(
        env={
            "UI_SCRIPT": str(script),
            "UI_SNAPSHOT_DIR": str(got),
            "UI_CARD": str(card),
        }
    )


def _diff(gate, got: Path, golden: Path, what: str = "") -> None:
    r = subprocess.run(
        [
            sys.executable,
            str(gate.root / "scripts/ui_snapshot_diff.py"),
            str(got),
            str(golden),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if r.returncode != 0:
        what = what or f"{golden.name}: snapshots differ from the golden"
        gate.fail(f"{what}\n{r.stdout}{r.stderr}")


def _app_scripts(gate, card: Path) -> None:
    for script in sorted((gate.root / "tests/ui-scripts/app").glob("*.txt")):
        name = script.stem
        if BOARD_STEP.search(script.read_text()):
            gate.say(f"{name}: skipped (its board step needs more than one panel)")
            continue
        got = gate.tmp / "got" / name
        run = _run(gate, script, got, card)
        if gate.require_standard(run):
            _diff(gate, got, gate.root / "tests/golden/ui/app" / name)
        else:
            gate.show(run)


def _stale_keys(gate) -> None:
    # Settings that once chose between two menus, still on a card: unknown now, so the default leg exactly.
    menu = gate.root / "tests/ui-scripts/app/menu.txt"
    for fixture, key in (
        ("card-old-shell-menu", "shellMenu=0"),
        ("card-old-legacy-menu", "legacyMenu=1"),
    ):
        got = gate.tmp / "got" / f"menu_{fixture.replace('-', '_')}"
        run = _run(gate, menu, got, gate.root / "tests/fixtures" / fixture)
        if gate.require_standard(run):
            _diff(
                gate,
                got,
                gate.tmp / "got" / "menu",
                f"{got.name}: a card that says {key} ran differently from the default card",
            )
        else:
            gate.show(run)


def _self_tests(gate, card: Path) -> None:
    selftest = gate.root / "tests/ui-scripts/selftest"

    got = gate.tmp / "got" / "selftest-press"
    run = _run(gate, selftest / "press-then-snapshot.txt", got, card)
    if gate.require_standard(run):
        same = (got / "same.state").read_text()
        later = (got / "later.state").read_text()
        if "led Play on" in same:
            gate.fail(
                "press-then-snapshot: a press due in the snapshot's own millisecond showed in it"
            )
        if "led Play on" not in later:
            gate.fail("press-then-snapshot: the Play press never reached the app")

    for script, expect in (
        ("early-snapshot.txt", "has never blitted"),
        ("no-quit.txt", "the deadline passed"),
    ):
        run = _run(gate, selftest / script, gate.tmp / "got" / script, card)
        if run.code != 2 or expect not in run.err:
            gate.fail(f"{script}: expected exit 2 with '{expect}', got exit {run.code}")
            gate.show(run)


def run(gate) -> None:
    card = gate.root / "tests/fixtures/card"
    _app_scripts(gate, card)
    _stale_keys(gate)
    _self_tests(gate, card)
