"""The offset-io gate: a real multi-block SysEx file transfer and a whole-file copy.

    OI-TRANSFER  that THREE blocks written at ascending offsets are all still on the card. Three, not one: a write_at
                 that truncated would leave only the last block, and a single-block round trip would pass against
                 exactly that bug -- efatfs::portable::create_context truncates unconditionally, and the correct
                 open mode looks like a mistake.
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

Gate offset-io:
    three blocks written at ascending offsets through the real SysEx transfer verbs must all still be on the card, a
    copy must reproduce its source byte-for-byte, and copying a missing source must report an error. THREE blocks and
    not one is the whole point: efatfs::portable::create_context truncates unconditionally, so a write_at that re-opened
    that way would leave only the last block, and a single-block round trip would pass against exactly that bug. The
    missing-source case is load-bearing for the same class of reason -- without a case whose expected answer is an
    error, a handler replying before its operation completed reports the same err=0 a correct one does. OI-CHANNEL going
    non-zero is the evidence that the SysEx operations took the channel. OI-DIR-STALL holds a directory listing's open
    far past the handler's 600-tick bound and requires a timeout reply (15) with no entries, and OI-TIER requires that
    SMSysex opened no directory synchronously on the interaction tier after boot.
"""

from scenario_gate import count_field, field


def check(gate, run):
    served = count_field(run.first("OI-CHANNEL "), "served")
    if not served:
        gate.fail(
            f"OI-CHANNEL served={field(run.first('OI-CHANNEL '), 'served') or 'none'}: "
            "the byte channel served nothing, so the async arm never ran"
        )
        return
    gate.say(
        f"PASS — the transfer and the copy reached the card, and the byte channel served {served}"
    )
    gate.say("       operation(s)")
