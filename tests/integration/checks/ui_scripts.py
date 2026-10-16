"""The level-3 UI scripts on golden_vt_render.

Every app script in tests/ui-scripts/app runs and each of its snapshots must match the golden in tests/golden/ui/app.
The menu script runs a second time with ShellMenu on (tests/fixtures/card-shell-menu): its panels must match the
ShellMenu-off leg's and at least one .state must differ. The runner's own self-tests (tests/ui-scripts/selftest) pin
what it does with a press due in a snapshot's millisecond, a snapshot before the first frame and a script that never
quits. A script with a `board` step needs more panels than the Deluge has and is skipped, named.
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


def _diff(gate, got: Path, golden: Path) -> None:
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
        gate.fail(
            f"{golden.name}: snapshots differ from the golden\n{r.stdout}{r.stderr}"
        )


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


def _menu_ab(gate, card: Path) -> None:
    on_card = gate.root / "tests/fixtures/card-shell-menu"
    got_on = gate.tmp / "got" / "menu_shell"
    run = _run(gate, gate.root / "tests/ui-scripts/app/menu.txt", got_on, on_card)
    if not gate.require_standard(run):
        gate.show(run)
        return
    _diff(gate, got_on, gate.root / "tests/golden/ui/app/menu_shell")
    r = subprocess.run(
        [
            sys.executable,
            str(gate.root / "scripts/ui_app_ab_check.py"),
            str(gate.tmp / "got" / "menu"),
            str(got_on),
            "--fixture-off",
            str(card),
            "--fixture-on",
            str(on_card),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if r.returncode != 0:
        gate.fail(f"menu A/B: {r.stdout}{r.stderr}")


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
    _menu_ab(gate, card)
    _self_tests(gate, card)
