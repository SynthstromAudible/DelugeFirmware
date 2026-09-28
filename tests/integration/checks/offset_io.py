"""The offset-io gate: a real multi-block SysEx file transfer and a whole-file copy.

    OI-TRANSFER  that THREE blocks written at ascending offsets are all still on the card. Three, not one: a write_at
                 that truncated would leave only the last block, and a single-block round trip would pass against
                 exactly that bug -- efatfs_core::create_context truncates unconditionally, and the correct open mode
                 looks like a mistake.
    OI-COPY      that a copy reproduces its source byte-for-byte, and that copying a missing source reports an error.
                 Without the error case, a handler replying before its operation finished reports the same err=0 a
                 correct one does.
    OI-DIR       that the directory listing verb answers.
    OI-CHANNEL   that the async arm was EXECUTED. Required non-zero.
    OI-ILLEGAL   that no SysEx operation above was refused for want of a legal place to run.
    OI-GIVEUP    that a copy whose channel operation is held far past the stall bound is answered with an error:
                 exactly one refused operation, and nothing copied.
    OI-DIR-STALL that a directory listing whose open is held far past the handler's stall bound is answered with the
                 timeout (15) and no entries, instead of holding the SysEx queue for good.
    OI-TIER      that SMSysex opened no directory synchronously on the interaction tier after boot. Required 0.

OI-TRANSFER re-reads the card file directly, never the bytes a channel block read returned; the block-read copy is
covered by song-save-channel's and document-save-channel's read-back checks. An abort (a tier assertion firing) exits
before OI-RESULT is printed, which is what turns a freeze into a gate failure rather than a silently truncated log.

The registry entry requires each marker and the result line; this module adds OI-CHANNEL's count.
"""

import re

CHANNEL = re.compile(r"OI-CHANNEL served=([0-9]*)")


def check(gate, run):
    counts = [m.group(1) for line in run.lines if (m := CHANNEL.fullmatch(line))]
    served = counts[0] if counts else ""
    if not served or int(served) == 0:
        gate.fail(
            f"OI-CHANNEL served={served or 'none'}: the byte channel served nothing, so the async arm never ran"
        )
        return
    gate.say(
        f"PASS — the transfer and the copy reached the card, and the byte channel served {served}"
    )
    gate.say("       operation(s)")
