"""The favourites-channel gate: the favourites bank is never written with less than it held.

Drives the favourites bank through its real entry points and requires that no bank is ever written with less than it
held, while counting every favourites card operation that began on the interaction tier.

    FV-SEED       precondition: bank 2 was seeded, or the stale check proves nothing
    FV-ABSENT     opening a bank with no file leaves no file behind
    FV-REFUSE     an edit made while its bank is loading changes nothing and writes nothing
    FV-ROUNDTRIP  a favourite survives leaving its bank and coming back
    FV-ORDER      a save made just before switching away and back lands before the bank is read again
    FV-STALE      a bank's answer that arrives after another bank was selected is never applied
    FV-TIER       no favourites card I/O began on the interaction tier
    FV-CHANNEL    the bank's operations went through the byte channel
    FV-ACCOUNTED  every issued operation ended on exactly one arm
    FV-ILLEGAL    no operation was refused for want of a legal place to run

The registry entry requires each marker and the result line. This module adds FV-CHANNEL's count: the bank's
operations must have gone through the byte channel, because a channel that is compiled but never runs would otherwise
leave every other check green.
"""

import re

CHANNEL = re.compile(r"FV-CHANNEL served=[0-9]* favourites=([0-9]*)")


def check(gate, run):
    counts = [m.group(1) for line in run.lines if (m := CHANNEL.fullmatch(line))]
    if not counts or not counts[0] or int(counts[0]) == 0:
        line = run.first("FV-CHANNEL ") or "no line"
        gate.fail(
            f"FV-CHANNEL — no favourites operation went through the channel: {line}"
        )
        return
    gate.say(
        "PASS — the favourites bank round-tripped through the byte channel, never applied a stale bank,"
    )
    gate.say(
        "       never wrote a bank it had not loaded, and did no card I/O on the interaction tier"
    )
