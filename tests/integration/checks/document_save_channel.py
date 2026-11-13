"""The document-save channel gate.

Saves a preset through the REAL ENTER gesture and requires that the document was written whole, read back through the
channel, and stored through the byte channel's write-from after boot. No other gate builds that whole-document write,
and a feature no gate builds is code that has never run. The byte channel has no feature switch, so there is one leg.

The registry entry requires each marker and the result line; this module judges the summary's counts and the lines'
fields.

What the checks cover:
  DS-CHANNEL    the document write was SERVED by the channel -- the async arm ran.
  DS-TICKED     the save's steps went through a channel after boot. The gate's actual claim.
  DS-HELD       a preset save whose write the channel holds far past the stall bound still completes and verifies: a
                running write cannot be cancelled, so giving up on it only releases the serialiser it is still copying
                from.
  DS-VERIFIED   every checked save COMPLETED a read-back -- counted where a save genuinely succeeds, because a rejected
                write has already left its file on the card.
  DS-VERIFY-ARM the read-back ran through the channel and never on the inline arm.
  DS-KITROW     a kit row was saved and verified. No other gate saves one.
  DS-GUARD-SONG a preset save dispatched while a song save's collection or document machine runs -- with the save guard
                already released -- is refused, and the song document still lands whole.
  DS-ACCOUNTED  a read-back the channel answered, and that then failed the save, still counts as served: the file is
                removed between the write landing and the read-back's first answer.
  DS-OVERWRITE  a confirmed overwrite writes a temp file beside its target, verifies it there, and swaps it into place:
                the name holds a whole document and no temp file is left.
  DS-OVERWRITE-FAIL
                a confirmed overwrite whose temp file is truncated before its read-back fails the save, leaves the
                document it was replacing byte-for-byte intact, and removes the temp file.

Gate document-save-channel:
    a preset saved through the real ENTER gesture must be written WHOLE, read back through the channel (DS-VERIFIED,
    DS-VERIFY-ARM), and stored through the byte channel's write-from after boot (DS-TICKED). One leg: the byte channel
    has no feature switch, so there is no second arm to run. DS-OCCUPIED saves onto a name that is already taken and
    requires both that the user is asked and that the existing document is left alone: silently replacing somebody's
    preset is the worst thing this code could do. DS-BACKMIDSAVE and DS-EXITMIDSAVE press BACK and fire the long-press
    exit while the save is still running, and require the save screen to stay open: a save spans ticks with the UI live,
    and leaving its screen mid-save would let the save's own close() read below the bottom of the UI stack. Both first
    require that the save really is still in flight, so a save that finished early fails the check instead of passing it
    for free. DS-GUARD-SONG dispatches a preset save while a song save's collection or document machine is still running
    with the save guard already released, and requires the dispatch refused and the song document whole: the shared
    serialiser holds the song's staged document for that whole window. DS-ACCOUNTED removes a preset's file between its
    write landing and its read-back's first answer, and requires the one answer the channel gave -- NOT_FOUND, which
    fails the save -- to be counted as served: counting only the ticks that advanced would undercount every failed
    read-back. DS-OVERWRITE saves onto the preset's own name again, confirms the prompt, and requires the document
    written to <target>.tmp, verified there and swapped into place, with no temp file left. DS-OVERWRITE-FAIL does the
    same and truncates that temp file between its write landing and its read-back, and requires the save failed, the
    document it was replacing byte-for-byte intact, and the temp file removed: rewriting the target in place would let a
    failed write or read-back destroy the document it replaces. Both run last, because DS-GUARD and DS-KITROW count
    staged saves absolutely. DS-HELD holds a preset save's write-from on the byte channel past the save's stall bound
    and requires the save to complete and verify: giving up would release the serialiser the write is still copying
    from.
"""

from scenario_gate import count_field


def _summary(run, key: str) -> int | None:
    """The counter `key` on the DS-SUMMARY line, or None when the line or the counter is absent or not a count."""
    return count_field(run.first("DS-SUMMARY "), key)


def _shown(count: int | None, otherwise: str) -> str:
    return otherwise if count is None else str(count)


def _require_line(gate, run, whole: str, tag: str, missing: str = "no line") -> None:
    """Fail `tag` unless `whole` is a line of the run, quoting the tag's lines."""
    if not run.has_line(whole):
        found = "\n".join(run.starting(tag + " ")) or missing
        gate.fail(f"{tag} — {found}")


def check(gate, run):
    served = _summary(run, "served")
    channel_ticks = _summary(run, "channelTicks")
    vserved = _summary(run, "verifyServed")
    vinline = _summary(run, "verifyInline")

    # DS-CHANNEL. The async arm actually RAN.
    if not served:
        gate.fail(
            f"DS-CHANNEL — the save served {_shown(served, 'no')} document write(s); the async arm never ran"
        )

    # DS-TICKED. The gate's actual claim: `channelTicks` counts ticks on which a staged step went through a channel.
    if not channel_ticks:
        gate.fail("DS-TICKED — the save never ran through the channel across ticks")

    # DS-VERIFIED. The preset, the kit row, the confirmed overwrite and the held save are checked, and all four
    # completed. The counters are taken where a save genuinely succeeds, so a save that wrote its file and was then
    # rejected by the read-back -- DS-ACCOUNTED's and DS-OVERWRITE-FAIL's -- cannot be counted, which a check on the
    # file's presence alone would miss.
    _require_line(
        gate,
        run,
        "DS-VERIFIED completedSaves=4 verified=4",
        "DS-VERIFIED",
        "no DS-VERIFIED line",
    )

    # DS-BACKMIDSAVE / DS-EXITMIDSAVE. The staged preset save spans ticks and ends in close(); leaving its screen
    # mid-save must be refused, and must be attempted while the save is genuinely running.
    _require_line(gate, run, "DS-BACKMIDSAVE inFlight=1 back=1", "DS-BACKMIDSAVE")
    _require_line(gate, run, "DS-EXITMIDSAVE exit=1", "DS-EXITMIDSAVE")

    # DS-FINISHED / DS-KITFINISHED / DS-GUARD. What a save does AFTER the document lands, and what it refuses BEFORE. A
    # staged save can skip all of it -- close, confirm, rename, and the check that refuses a slot another instrument in
    # the song already holds -- with every byte on the card still correct, so nothing that compares bytes can see it.
    _require_line(gate, run, "DS-FINISHED closed=1 named=1", "DS-FINISHED")
    _require_line(
        gate, run, "DS-KITFINISHED closed=1 named=1 path=SYNTHS", "DS-KITFINISHED"
    )
    _require_line(gate, run, "DS-GUARD refused=1 unchanged=1", "DS-GUARD")
    _require_line(
        gate,
        run,
        "DS-GUARD-SONG tried=1 refused=1 songSaved=1 presetWritten=0",
        "DS-GUARD-SONG",
    )
    _require_line(
        gate, run, "DS-ACCOUNTED tampered=1 failed=1 verifyServed=1", "DS-ACCOUNTED"
    )

    # DS-VERIFY-ARM. The read-back rides the channel, and none runs on the inline arm (`verifyInline`).
    if not vserved or vinline != 0:
        gate.fail(
            "DS-VERIFY-ARM — the read-back did not run through the channel "
            f"(verifyServed={_shown(vserved, 'none')} verifyInline={_shown(vinline, 'none')})"
        )

    if not gate.failures:
        gate.say(
            "PASS — the document was written whole and read back through the channel, a taken"
        )
        gate.say(
            "       name was refused rather than replaced, and a confirmed overwrite kept the old document until its"
        )
        gate.say("       replacement was verified")
