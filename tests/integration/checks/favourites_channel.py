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

Gate favourites-channel:
    the favourites bank must never be written with less than it held, and none of its card I/O may begin on the
    interaction tier. Every browser visit, favourite pad and bank pad reaches this bank, and before this gate existed a
    failed existence check read as absent and wrote an EMPTY bank over the real one, and an edit on a bank that never
    loaded saved one favourite over the other fifteen. FV-ABSENT requires opening a bank with no file to leave no file;
    FV-REFUSE requires an edit made while the bank is loading to change and write nothing; FV-ORDER requires a save made
    just before switching away and back to land before the bank is read again; FV-STALE requires an answer that arrives
    after another bank was selected never to be applied, and first requires that answer to have been issued at all, or
    it proves nothing. One leg: the byte channel has no feature switch, and FV-CHANNEL requires the bank's operations to
    have gone through it. Every wait is on the bank going idle, never on a file appearing.
"""

from scenario_gate import count_field


def check(gate, run):
    if not count_field(run.first("FV-CHANNEL "), "favourites"):
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
