"""The startup fail-safe gate: none of the fail-safe's file I/O begins on the interaction tier.

Drives the real startup fail-safe. What the checks cover (tests/integration/scenarios/startup_failsafe_scenario.cpp writes the
lines), and why each is separate:
    SF-ORDER    the machine never advances past the canary write without a terminal answer, and the canary is on the
                card before the load is allowed to proceed.
    SF-TIER     that no fail-safe file operation ran on the interaction tier. This is the evidence.
    SF-CHANNEL  that the async arm was executed, not merely compiled. Required non-zero: a channel that is compiled but
                never runs would otherwise leave this gate green.
    SF-ILLEGAL  that no fail-safe operation was refused for want of a legal place to run.
    SF-GIVEUP   with the channel holding the machine's first request far past the stall bound, the machine gives up:
                exactly one refused operation, no canary written, no interaction-tier I/O, finished within its bound.

The fail-safe's happy path ends in openUI(&loadSongUI), and this harness has no UI stack, so the scenario drives the
song-missing path instead: the canary creation, the existence checks and the cleanup unlink, then a return before the
UI. Which tier those run on does not depend on which branch reached them.

The registry entry requires each marker and the result line; this module adds SF-CHANNEL's non-zero count.
"""

import re

SERVED = re.compile(r"^SF-CHANNEL served=([0-9]*)$")


def check(gate, run):
    served = next((m.group(1) for line in run.lines if (m := SERVED.match(line))), "")
    if not served or int(served) == 0:
        gate.fail(
            f"SF-CHANNEL served={served or 'none'}: the byte channel served nothing, so the async arm never ran"
        )
        return
    gate.say(
        "PASS — no startup fail-safe I/O ran on the interaction tier, and the byte channel served"
    )
    gate.say(f"       {served} operation(s)")
