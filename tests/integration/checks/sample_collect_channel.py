"""The sample-collection channel gate.

Saves a song with collect-media on and requires that every sample the song references arrived in the song's own folder
byte-identical to its source, that the audio-file list survived the process intact, and that the copying went through
the byte channel after boot.

Collecting media is the one save path that walks a container the audio engine and the UI both read, mutating a key the
container is SORTED BY as it goes. The copying spans ticks, so the walk can be observed half-done. The collection
answers that by deciding every path first, transferring with nothing shared held, and applying the decisions in one
pass -- and SC-ORDERED is what says the answer works. The transfers and the save around them ride the byte channel,
which has no feature switch, so there is one leg.

What the checks cover:
  SC-COPIED     every referenced sample is AT its collected destination, byte-identical to the source.
  SC-RENAMED    the fixture's recording really was renamed. Required, because a collect-media save rewrites filePath
                for recordings and NOTHING else -- so if the rename stops happening, nothing moves in the audio-file
                vector and SC-ORDERED becomes a check that cannot fail.
  SC-ORDERED    the audio-file list is still sorted and every resident file still findable, once the save is over.
  SC-ATOMIC     the same invariant, checked on EVERY tick while the collection is in flight. This is the one the design
                is actually for: the hazard is another tier bisecting the vector BETWEEN two transfers, and SC-ORDERED
                cannot see that window because the apply pass tidies it away before the end. A path mutated during the
                planning walk passes SC-ORDERED and fails this.
  SC-CHANNEL    the transfers were SERVED by the byte channel -- the async arm ran.
  SC-ACCOUNTED  the channel served every collected file and the inline arm served none.
  SC-TICKED     the transfers went through the channel after boot (`channelTicks`).

Two things keep the ordering checks from becoming checks that cannot fail. The fixture carries a recording --
SC-RENAMED proves it is still being renamed -- and a second file positioned so the rename must move the recording
ACROSS it in the sort order. And the neutering: mutate filePath during the PLAN walk instead of the APPLY pass, and
SC-ATOMIC must go red. SC-ORDERED does not catch that; the end state is repaired by the apply pass, so only the
in-flight check can see it.
"""

import re


def _summary(run, pattern: str) -> str | None:
    """The digits `pattern` captures on an SC-SUMMARY line, or None when no line matches ("" counts as absent)."""
    for line in run.lines:
        m = re.match(pattern, line)
        if m:
            return m.group(1) or None
    return None


def check(gate, run):
    if not run.has_line("SC-RESULT failures=0"):
        gate.fail("\n".join(run.starting("SC-RESULT")))

    served = _summary(run, r"^SC-SUMMARY served=([0-9]*) ")
    inline_served = _summary(run, r"^SC-SUMMARY .*inlineServed=([0-9]*)")
    channel_ticks = _summary(run, r"^SC-SUMMARY .*channelTicks=([0-9]*)")
    copies = len(run.starting("SC-COPIED to="))

    # SC-CHANNEL, SC-ACCOUNTED and SC-TICKED: no file may take the inline arm, and the channel must have served exactly
    # the files that arrived.
    if served is None or int(served) == 0:
        gate.fail(
            f"SC-CHANNEL — the collection served {served or 'no'} transfer(s); the async arm never ran"
        )
    if int(inline_served or 1) != 0:
        gate.fail(
            "SC-ACCOUNTED — the collection sent "
            f"{inline_served or 'an unknown number of'} transfer(s) through the inline arm"
        )
    if int(served or 0) != copies:
        gate.fail(
            f"SC-ACCOUNTED — the collection copied {copies} file(s) but the channel served {served or 'no'}"
        )
    if channel_ticks is None or int(channel_ticks) == 0:
        gate.fail("SC-TICKED — no transfer ever ran through the channel across ticks")

    if not gate.failures:
        gate.say(
            "PASS — the samples were collected into the song's folder, the audio-file list came through"
        )
        gate.say(
            "       ordered and intact, and the byte channel did the copying across ticks"
        )
