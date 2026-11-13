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

This module runs the scenario itself, after the entry's standard checks. Every leg is checked and every failing one
reported.
"""

from scenario_gate import field, int_field


def run(gate):
    result = gate.run(
        env={
            "GOLDEN_VIRTUAL_BUDGET_MS": gate.env.get(
                "GOLDEN_VIRTUAL_BUDGET_MS", "1800000"
            )
        }
    )
    if not gate.require_standard(result):
        return
    for line in result.starting("CR-"):
        gate.say(line)

    failed = False
    residents = result.last("CR-RESIDENTS ")
    walked = field(residents, "samples")
    count = int_field(residents, "samples")
    if not walked:
        gate.fail(
            "CR-RESIDENTS — no CR-RESIDENTS line; the scenario never reached the snapshot"
        )
        failed = True
    elif count is None or count <= 0:
        gate.fail(
            f"CR-RESIDENTS — the walk had {walked} resident sample(s): every other leg here would pass for free"
        )
        failed = True

    # Each leg's line is one of the entry's markers, so it is there; its verdict is ok=1.
    for leg in ("CR-SURVIVE", "CR-LOST", "CR-EVICT"):
        line = result.last(leg + " ")
        if field(line, "ok") != "1":
            gate.fail(f"{leg} — {line or 'no ' + leg + ' line'}")
            failed = True

    if not failed:
        for line in result.starting("TIER-SYNC ", "TIER-SYNC-AT "):
            gate.say(line)
        gate.say(
            f"PASS — the swap walked {walked} resident sample(s): the intact one survived, the removed one was"
            " marked unloadable, the unreferenced one was evicted"
        )
