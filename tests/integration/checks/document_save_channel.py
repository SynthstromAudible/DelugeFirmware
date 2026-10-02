"""The document-save channel gate.

Saves a preset through the REAL ENTER gesture and requires that the document was written whole, read back through the
channel, and stored through the byte channel's write-from after boot. No other gate builds that whole-document write,
and a feature no gate builds is code that has never run. The byte channel has no feature switch, so there is one leg.

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
"""

import re


def _summary(run, pattern: str) -> str | None:
    """The digits `pattern` captures on a DS-SUMMARY line, or None when no line matches ("" counts as absent)."""
    for line in run.lines:
        m = re.match(pattern, line)
        if m:
            return m.group(1) or None
    return None


def _require_line(gate, run, whole: str, tag: str, missing: str = "no line") -> None:
    """Fail `tag` unless `whole` is a line of the run, quoting the tag's lines."""
    if not run.has_line(whole):
        found = "\n".join(run.starting(tag + " ")) or missing
        gate.fail(f"{tag} — {found}")


def check(gate, run):
    if not run.has_line("DS-RESULT failures=0"):
        gate.fail("\n".join(run.starting("DS-RESULT")))

    served = _summary(run, r"^DS-SUMMARY served=([0-9]*) ")
    channel_ticks = _summary(run, r"^DS-SUMMARY .*channelTicks=([0-9]*)")
    vserved = _summary(run, r"^DS-SUMMARY .*verifyServed=([0-9]*)")
    vinline = _summary(run, r"^DS-SUMMARY .*verifyInline=([0-9]*)")

    # DS-CHANNEL. The async arm actually RAN.
    if served is None or int(served) == 0:
        gate.fail(
            f"DS-CHANNEL — the save served {served or 'no'} document write(s); the async arm never ran"
        )

    # DS-TICKED. The gate's actual claim: `channelTicks` counts ticks on which a staged step went through a channel.
    if channel_ticks is None or int(channel_ticks) == 0:
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
    if int(vserved or 0) == 0 or int(vinline or 1) != 0:
        gate.fail(
            "DS-VERIFY-ARM — the read-back did not run through the channel "
            f"(verifyServed={vserved or 'none'} verifyInline={vinline or 'none'})"
        )

    if not gate.failures:
        gate.say(
            "PASS — the document was written whole and read back through the channel, a taken"
        )
        gate.say(
            "       name was refused rather than replaced, and a confirmed overwrite kept the old document until its"
        )
        gate.say("       replacement was verified")
