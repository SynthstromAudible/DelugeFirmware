"""The card-reinsert gate: what a card swap does to the samples still in memory.

Drives a real ejection and re-insertion (AudioFileManager::setCardEjected() -> reinitEjectedCard() ->
deluge_efatfs_remount() -> cardReinserted()) with three residents present, one per fate that walk distinguishes:
    CR-RESIDENTS  the walk had samples to visit at all
    CR-SURVIVE    a referenced sample whose file is intact stays loadable
    CR-LOST       a referenced sample whose file was removed is marked unloadable
    CR-EVICT      a resident nothing references is pruned from memory

CR-RESIDENTS is not ceremony. `audio-load-ab` drives these same edges and reaches cardReinserted() twice with
residents=0 -- the edges fire and the loop body never runs. A gate for a walk must fail when the walk is empty, so
CR-RESIDENTS is checked first and hardest.

The card image is cached and shared between gates, and reused and mutated across runs, so this module runs the
scenario itself, starting cold: every cached scenario image is removed first. Every leg is checked and every failing
one reported.
"""

import re
from pathlib import Path

RESIDENTS = re.compile(r"^CR-RESIDENTS samples=([0-9-]*) ")
OK = re.compile(r"^[A-Z-]* ok=([0-9-]*)")


def _count(value: str) -> int | None:
    """A count, or None when the line does not carry a number."""
    try:
        return int(value)
    except ValueError:
        return None


def run(gate):
    tmp = Path(gate.env.get("TMPDIR") or "/tmp")
    for image in tmp.glob("deluge-streaming-scenario-*.img"):
        image.unlink(missing_ok=True)

    result = gate.run(
        env={
            "GOLDEN_VIRTUAL_BUDGET_MS": gate.env.get(
                "GOLDEN_VIRTUAL_BUDGET_MS", "1800000"
            )
        }
    )
    if result.code != 0:
        gate.fail(f"the run exited {result.code} (crash or wedge)")
        for line in [
            l for l in result.lines if l.startswith(("CR-", "[golden-scenario]"))
        ][-20:]:
            gate.say(line)
        return
    for line in result.starting("CR-"):
        gate.say(line)

    failed = False
    residents = [m.group(1) for line in result.lines if (m := RESIDENTS.match(line))]
    if not residents or not residents[-1]:
        gate.fail(
            "CR-RESIDENTS — no CR-RESIDENTS line; the scenario never reached the snapshot"
        )
        failed = True
    elif (count := _count(residents[-1])) is None or count <= 0:
        gate.fail(
            f"CR-RESIDENTS — the walk had {residents[-1]} resident sample(s): every other leg here would pass for free"
        )
        failed = True

    for leg in ("CR-SURVIVE", "CR-LOST", "CR-EVICT"):
        line = result.last(leg + " ")
        if line is None:
            gate.fail(f"{leg} — no {leg} line")
            failed = True
            continue
        m = OK.match(line)
        if not m or m.group(1) != "1":
            gate.fail(f"{leg} — {line}")
            failed = True

    if not result.has_line("CR-RESULT failures=0"):
        gate.fail("\n".join(result.starting("CR-RESULT")) or "no CR-RESULT line")
        failed = True

    if not failed:
        for line in result.lines:
            if re.match(r"TIER-SYNC(-AT)? ", line):
                gate.say(line)
        gate.say(
            f"PASS — the swap walked {residents[-1]} resident sample(s): the intact one survived, the removed one was"
            " marked unloadable, the unreferenced one was evicted"
        )
