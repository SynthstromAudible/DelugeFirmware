"""The level-3 UI scripts on golden_vt_render.

Every app script in tests/ui-scripts/app runs on each card its `# card:` header names (tools/ui_script_cards.py). On
the first, each of its snapshots must match its golden in tests/golden/ui/app; on every further card it must
reproduce the first card's snapshots. The runner's own self-tests (tests/ui-scripts/selftest) pin what it does with a
press due in a snapshot's millisecond, a snapshot before the first frame and a script that never quits. A script with
a `board` step needs more panels than the Deluge has and is skipped, named. Under `./dbt test --update ui-scripts`
each app script's snapshots on its first card are recorded as its golden instead.

Gate ui-scripts:
    the level-3 UI scripts drive the whole app through the BSP's own input path on the virtual clock. Every script in
    tests/ui-scripts/app runs on each card its `# card:` header names: on the first, each snapshot (the OLED panel as
    .pbm; the LEDs, pads and knob rings as .state) must match its golden in tests/golden/ui/app; on every further card
    it must reproduce the first card's snapshots, which is how a02-menu shows that two cards still saying a menu
    setting this firmware does not have, shellMenu=0 and legacyMenu=1, boot exactly as the fixture card does. A
    script without a card header fails. The runner's self-tests pin a press due in a snapshot's own millisecond
    (shown in the next snapshot, not that one), a snapshot before the first frame (exit 2) and a script that never
    reaches its quit (exit 2 at its last step plus five seconds). A script with a board step needs more than one
    panel and is skipped by name.
"""

import re
import subprocess
import sys
from pathlib import Path

import golden_update

# tools/ holds the header reader the parity runners share.
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import ui_script_cards

BOARD_STEP = re.compile(r"^\s*\d+\s+board\b", re.MULTILINE)


def _run(gate, script: Path, got: Path, card: Path):
    return gate.run(
        env={
            "UI_SCRIPT": str(script),
            "UI_SNAPSHOT_DIR": str(got),
            "UI_CARD": str(card),
        }
    )


def _diff(gate, got: Path, golden: Path, what: str = "", record: bool = False) -> None:
    """Compare `got` with `golden` through ui_snapshot_diff.py; with `record`, under an update, record it instead."""
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
        env=gate.env if record else golden_update.without(gate.env),
    )
    if r.returncode != 0:
        what = what or f"{golden.name}: snapshots differ from the golden"
        gate.fail(f"{what}\n{r.stdout}{r.stderr}")


def _cards(gate, script: Path) -> list[tuple[str, Path]]:
    """The cards `script`'s header names, each as written and resolved; a script without one fails the gate."""
    named = ui_script_cards.cards(script)
    if not named:
        gate.fail(f"{script.name}: no `# card:` header names the card it runs on")
    return [(card, ui_script_cards.resolve(card, gate.root)) for card in named]


def _app_scripts(gate) -> None:
    for script in sorted((gate.root / "tests/ui-scripts/app").glob("*.txt")):
        name = script.stem
        if BOARD_STEP.search(script.read_text()):
            gate.say(f"{name}: skipped (its board step needs more than one panel)")
            continue
        cards = _cards(gate, script)
        if not cards:
            continue
        (first, first_path), *others = cards
        got = gate.tmp / "got" / name
        run = _run(gate, script, got, first_path)
        if not gate.require_standard(run):
            gate.show(run)
            continue
        _diff(gate, got, gate.root / "tests/golden/ui/app" / name, record=True)
        for card, path in others:
            leg = gate.tmp / "got" / f"{name}@{ui_script_cards.label(card)}"
            run = _run(gate, script, leg, path)
            if gate.require_standard(run):
                _diff(
                    gate,
                    leg,
                    got,
                    f"{leg.name}: on {card} the script ran differently from on {first}",
                )
            else:
                gate.show(run)


def _self_tests(gate) -> None:
    selftest = gate.root / "tests/ui-scripts/selftest"

    def card(script: Path) -> Path | None:
        cards = _cards(gate, script)
        return cards[0][1] if cards else None

    script = selftest / "t01-press-then-snapshot.txt"
    if (path := card(script)) is not None:
        got = gate.tmp / "got" / "selftest-press"
        run = _run(gate, script, got, path)
        if gate.require_standard(run):
            same = (got / "same.state").read_text()
            later = (got / "later.state").read_text()
            if "led Play on" in same:
                gate.fail(
                    f"{script.name}: a press due in the snapshot's own millisecond showed in it"
                )
            if "led Play on" not in later:
                gate.fail(f"{script.name}: the Play press never reached the app")

    for name, expect in (
        ("t02-early-snapshot.txt", "has never blitted"),
        ("t03-no-quit.txt", "the deadline passed"),
    ):
        if (path := card(selftest / name)) is None:
            continue
        run = _run(gate, selftest / name, gate.tmp / "got" / name, path)
        if run.code != 2 or expect not in run.err:
            gate.fail(f"{name}: expected exit 2 with '{expect}', got exit {run.code}")
            gate.show(run)


def run(gate) -> None:
    _app_scripts(gate)
    _self_tests(gate)
