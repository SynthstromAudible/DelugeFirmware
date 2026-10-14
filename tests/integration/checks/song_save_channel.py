"""The staged song-save channel gate's checks beyond its markers and result line.

The scenario saves a song whose document is far too big to fit the staging buffer, and requires that it was built in
memory, placed on the card, read back and verified, and that overwriting it left a song behind whichever way the save
went.

The song save builds its document in memory first and then places it -- and the operation that carries it is NOT
the one the rest of this flow uses. The byte channel's copying write stages through a fixed buffer and refuses
anything over 8 KiB; no song has ever fitted that, the smallest measured being 11.5 KB and the largest 1.10 MB. Its
write-from passes the pointer instead. A build that sent a song down the copying write failed every time and simply
produced no file. Every step rides the byte channel -- the document write as a write-from; the temp-name search, the
read-back, the unlink and the rename as its other operations -- and it has no feature switch.

What the scenario's own lines cover:
  SS-GREW       the song really did outgrow the staging buffer, at least twice. A fixture that fits the initial
                32 KiB buffer exercises the growth path not at all, and every check about it would then be a check
                that cannot fail.
  SS-SAVED      the document is on the card, with its length and checksum.
  SS-VERIFIED   the save COMPLETED, which it can only do after its read-back check -- length, head and tail -- has
                passed. A save that fails that check abandons AFTER writing its file, so SS-SAVED alone cannot tell
                success from a rejected write left on the card.
  SS-PARSES     it loads back as a SONG. Bytes of the right length are not the claim.
  SS-INTACT     after saving over an existing song, a song is still there and still loads. Nothing destructive runs
                until the replacement is written AND verified, so a save that fails up to that point leaves the
                original untouched. It does NOT hold across the unlink-then-rename pair at the end: a rename that
                fails after the unlink succeeded leaves the song gone and the new content stranded at the temp
                name. That window is the cost of unlinking before renaming, and this gate does not claim to close it.
  SS-HELD       a save whose write the channel holds far past the stall bound still completes, and its file is
                byte-identical to an unheld save of the same song made just before: a running write cannot be
                cancelled, so giving up on it only releases the serialiser it is still copying from. Its line carries
                heldUs, how long after the save began the write landed; the scenario fails the leg unless that is
                past the stall bound, since a write that was never held proves nothing.
  SS-UPGRADE    saving a song loaded from NAME.XML under the same name writes NAME.json beside it without asking to
                overwrite, leaves NAME.XML as it was, and leaves the song naming NAME.json -- the path a startup
                setting records.

What this module checks, from SS-SUMMARY, SS-GREW and the mid-save lines:
  SS-CHANNEL    the steps were SERVED by the channels, and the document write by the byte channel's write-from.
  SS-ACCOUNTED  the channels account for every card step, and none took the inline arm. Neither SS-CHANNEL nor
                SS-SAVED can see WHICH arm carried which step.
  SS-TICKED     the steps went through the channels after boot. The gate's actual claim.
  SS-GROWTH     the song doubled the buffer at least twice.
  SS-BACKMIDSAVE / SS-EXITMIDSAVE  a song save spans many ticks with the UI live. Leaving the save screen mid-save --
                BACK, or the long-press exit -- would close it, and the save's own close() would then index off the
                end of the UI stack. Both must be refused while the save runs, and both must actually be tried
                while it IS running, or the check proves nothing.

What this gate does NOT prove on its own: that the temp-file ordering is load-bearing. Only a neutering does that --
fail the read-back and the save must fail while SS-INTACT stays green, which is what says the original survives a
failure. Failing the RENAME instead turns SS-INTACT red, and that is not a defect in this gate: it is the inherited
unlink-then-rename window, reported rather than papered over.
"""

from scenario_gate import int_field


def _count(value: int | None) -> str:
    return "no" if value is None else str(value)


def check(gate, run):
    summary = run.first("SS-SUMMARY ")
    served = int_field(summary, "served")
    inline_served = int_field(summary, "inlineServed")
    channel_ticks = int_field(summary, "channelTicks")
    steps = int_field(summary, "steps")
    write = int_field(summary, "writeServed")
    doublings = int_field(run.first("SS-GREW "), "doublings")

    if not served:
        gate.fail(
            f"SS-CHANNEL — the save served {_count(served)} step(s); the async arm never ran"
        )
    if not write:
        gate.fail(
            f"SS-CHANNEL — the byte channel's write-from served {_count(write)} document write(s)"
        )

    if not steps:
        gate.fail("SS-ACCOUNTED — the save reported no card steps at all")
    if inline_served is None or inline_served != 0:
        gate.fail(
            f"SS-ACCOUNTED — the save ran {'' if inline_served is None else inline_served} step(s) through the "
            "inline arm; they should all go to a channel"
        )
    if (served or 0) != (steps or 0):
        gate.fail(
            f"SS-ACCOUNTED — the save took {_count(steps)} card step(s) but the channels served {_count(served)}"
        )

    # This counts ONLY ticks on which a document step went through a channel after boot.
    if not channel_ticks:
        gate.fail(
            "SS-TICKED — no document step ever ran through the channel across ticks"
        )

    if doublings is None or doublings < 2:
        gate.fail(
            f"SS-GROWTH — the song doubled the buffer {_count(doublings)} time(s); the growth path was not exercised"
        )

    for whole, tag in (
        ("SS-BACKMIDSAVE inFlight=1 back=1", "SS-BACKMIDSAVE"),
        ("SS-EXITMIDSAVE exit=1", "SS-EXITMIDSAVE"),
    ):
        if not run.has_line(whole):
            seen = "\n".join(run.starting(tag + " ")) or "no line"
            gate.fail(f"{tag} — {seen}")

    if not gate.failures:
        gate.say(
            "PASS — the song document was built in memory, placed on the card through the byte channel across"
        )
        gate.say("       ticks, read back, and loaded as a song")
