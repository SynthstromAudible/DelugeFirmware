"""The settings-channel gate: settings written through the real consumer entry point survive, off the interaction tier.

Writes a settings file through the REAL consumer entry point, reads it back, and requires the value to survive --
while counting every settings operation that began on the interaction tier.

    SC-ROUNDTRIP  the settings survive. The safety half: it catches a change that moved the I/O and broke the
                  settings on the way.
    SC-UNKNOWN    a key no setting has any more (a stale `legacyMenu=1`) is kept as an unknown setting through the same
                  round trip, once.
    SC-TIER       that no settings operation ran on the interaction tier. The evidence half.
    SC-BURST      a burst of three writes, the first already in flight, lands the last value in exactly two card
                  writes: the waiting write is replaced, the in-flight one never is.
    SC-ORDER      a read queued behind a write to the same file sees that write.
    SC-HEAD       every settings file the channel writes begins with the XML declaration.
    SC-FITS       every settings document fits the channel's 8 KiB bound.
    SC-UNLINK     a MIDI device table with nothing worth keeping removes its file through the channel.
    SC-PERF       performance view's defaults, read after boot, are read through the channel.
    SC-OVERSIZE   a settings write over that bound is refused after boot: counted, not written, not dispatched.
    SC-RESET      a community-features factory reset forgets the unknown settings it read, so a file written after it
                  does not carry them.
    SC-BUSY       a settings write refused behind a long copy that keeps making progress -- held for longer than
                  the stall bound -- waits for it and lands, rather than giving up on a channel that is busy.
    SC-FALLBACK   a busy channel queues -- nothing a burst submits is refused.
    SC-CHANNEL    the channel SERVED operations. The byte channel has no feature switch, so there is no second build
                  to compare against; this counter is what says the async arm ran rather than merely compiled -- a
                  harness that left the channel's module out of its own module list would report `served=0` with
                  every other check green.

An abort (a tier assertion firing) exits before SC-RESULT is printed, which is what turns a freeze into a gate failure
rather than a silently truncated log. The registry entry requires each marker and the result line; this module adds
SC-CHANNEL's count.

Gate settings-channel:
    a settings value written through the real consumer entry point must survive a read back, and no settings file
    operation may begin on the interaction tier. Two halves on purpose: the round trip catches a change that moved the
    I/O and broke the settings on the way, while SC-TIER, required zero, is the only check that shows where the I/O ran
    -- a round trip passes the same whichever tier did the I/O, so a gate with only a round trip would report success
    with every settings operation still on the interaction tier. The count is taken inside the consumer's own I/O path
    rather than at the point of dispatch, so it cannot report success for an operation that dispatched and then ran
    inline anyway.
"""

from scenario_gate import count_field, field


def check(gate, run):
    served = count_field(run.first("SC-CHANNEL "), "served")
    if not served:
        gate.fail(
            f"SC-CHANNEL served={field(run.first('SC-CHANNEL '), 'served') or 'none'}: "
            "the byte channel served nothing, so the async arm never ran"
        )
        return
    gate.say(
        f"PASS — the settings round-tripped, the byte channel served {served} operation(s), and no"
    )
    gate.say("       settings I/O ran on the interaction tier")
