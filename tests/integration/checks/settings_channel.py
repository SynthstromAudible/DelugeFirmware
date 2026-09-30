"""The settings-channel gate: settings written through the real consumer entry point survive, off the interaction tier.

Writes a settings file through the REAL consumer entry point, reads it back, and requires the value to survive --
while counting every settings operation that began on the interaction tier.

    SC-ROUNDTRIP  the settings survive. The safety half: it catches a change that moved the I/O and broke the
                  settings on the way.
    SC-TIER       that no settings operation ran on the interaction tier. The evidence half.
    SC-BURST      a burst of three writes, the first already in flight, lands the last value in exactly two card
                  writes: the waiting write is replaced, the in-flight one never is.
    SC-ORDER      a read queued behind a write to the same file sees that write.
    SC-HEAD       every settings file the channel writes begins with the XML declaration.
    SC-FITS       every settings document fits the channel's 8 KiB bound.
    SC-UNLINK     a MIDI device table with nothing worth keeping removes its file through the channel.
    SC-PERF       performance view's defaults, read after boot, are read through the channel.
    SC-OVERSIZE   a settings write over that bound is refused after boot: counted, not written, not dispatched.
    SC-BUSY       a settings write refused behind a long copy that keeps making progress -- refused on more ticks
                  than the stall bound -- waits for it and lands, rather than giving up on a channel that is busy.
    SC-FALLBACK   a busy channel queues -- nothing a burst submits is refused.
    SC-CHANNEL    the channel SERVED operations. The byte channel has no feature switch, so there is no second build
                  to compare against; this counter is what says the async arm ran rather than merely compiled -- a
                  harness that left the channel's module out of its own module list would report `served=0` with
                  every other check green.

An abort (a tier assertion firing) exits before SC-RESULT is printed, which is what turns a freeze into a gate failure
rather than a silently truncated log. The registry entry requires each marker and the result line; this module adds
SC-CHANNEL's count.
"""

import re

CHANNEL = re.compile(r"SC-CHANNEL served=([0-9]*)")


def check(gate, run):
    counts = [m.group(1) for line in run.lines if (m := CHANNEL.fullmatch(line))]
    served = counts[0] if counts else ""
    if not served or int(served) == 0:
        gate.fail(
            f"SC-CHANNEL served={served or 'none'}: the byte channel served nothing, so the async arm never ran"
        )
        return
    gate.say(
        f"PASS — the settings round-tripped, the byte channel served {served} operation(s), and no"
    )
    gate.say("       settings I/O ran on the interaction tier")
