"""The level-3 UI scripts on golden_vt_render.

Every app script in tests/ui-scripts/app runs and each of its snapshots must match the golden in tests/golden/ui/app.
The menu script runs a second time on the Legacy Menu (tests/fixtures/card-legacy-menu): its panels must match the
default leg's and at least one .state must differ; and a third time on a card that still says shellMenu=0
(tests/fixtures/card-old-shell-menu), the setting the Legacy Menu replaced, which must change nothing: every snapshot
equals the default leg's. The runner's own self-tests (tests/ui-scripts/selftest) pin what it does with a press due
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


def _menu_ab(gate, card: Path) -> None:
    menu = gate.root / "tests/ui-scripts/app/menu.txt"
    legacy_card = gate.root / "tests/fixtures/card-legacy-menu"
    got_legacy = gate.tmp / "got" / "menu_legacy"
    run = _run(gate, menu, got_legacy, legacy_card)
    if not gate.require_standard(run):
        gate.show(run)
        return
    _diff(gate, got_legacy, gate.root / "tests/golden/ui/app/menu_legacy")
    r = subprocess.run(
        [
            sys.executable,
            str(gate.root / "scripts/ui_app_ab_check.py"),
            str(got_legacy),
            str(gate.tmp / "got" / "menu"),
            "--fixture-off",
            str(legacy_card),
            "--fixture-on",
            str(card),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if r.returncode != 0:
        gate.fail(f"menu A/B: {r.stdout}{r.stderr}")

    # The setting the Legacy Menu replaced, still on a card: unknown, so the default leg exactly.
    old_card = gate.root / "tests/fixtures/card-old-shell-menu"
    got_old = gate.tmp / "got" / "menu_old_shell_menu"
    run = _run(gate, menu, got_old, old_card)
    if gate.require_standard(run):
        _diff(
            gate,
            got_old,
            gate.tmp / "got" / "menu",
            "menu_old_shell_menu: a card with the replaced shellMenu key ran differently from the default card",
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
    _menu_ab(gate, card)
    _self_tests(gate, card)
