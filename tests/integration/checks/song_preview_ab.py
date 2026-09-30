"""The song-preview gate: the prefix channel against the file read.

Draws the fixture card's song artwork through the REAL `LoadSongUI` over the prefix channel, and holds every song to the
same file decoded synchronously, inline, in the same process. The channel has no switch, so there is no second build to
compare against: the reference decode stands in for one.

What the run checks (tests/integration/scenarios/song_preview_ab_scenario.cpp writes the lines):
  SP-CASE   one per song: the decode's `SongPreviewOutcome`, the `Error` it put on screen, and a CRC of the rendered pad
            image. Held to SP-REFERENCE and to its absolute expectation.

            The OUTCOME is compared, not just the image, and that is the point rather than a detail: an unreadable
            song, a song with no artwork and a preview that falls past the read window all draw an IDENTICAL blank
            screen. A gate comparing only pixels would pass while one backing showed the user an error popup and the
            other silently showed nothing.
  SP-EJECT  the card-swap property: a prefix is read and left uncollected, the card is ejected, a DIFFERENT song is put
            at the same path, and the preview is asked for again. Held to a reference decode of the file now at that
            path, which holds no card-derived state to go stale. The scenario ALSO asserts the post-swap artwork
            absolutely, against the known second image: an inequality check ("not the first image") passes when both
            reads fail.
  SP-WINDOW the ONE song the browser must NOT draw as the file read does: its `preview` sits past
            `kSongPreviewWindow`. The reference walks the whole document and renders it; the prefix channel runs out
            of bytes and reports Truncated, drawing blank WITHOUT claiming the song has no artwork. Both halves are
            asserted absolutely.
  SP-REFERENCE
            one per song, beside its SP-CASE: the same file decoded synchronously, inline. The run requires the
            browser's outcome, error and image CRC to equal it; for the read-window song the reference must render the
            late artwork instead. Not diffed.
  SP-DEFER / SP-SUMMARY / SP-SWAP-SUMMARY
            how many times each song's preview deferred, and the swap's outcome. Read, not compared.
  SP-LISTING / SP-PREDICT / SP-NEWNAME / SP-SLOT / SP-BYPATH / SP-TIER
            the browser's listings, typing prediction, brand-new slot name (in an empty folder, and in a numbered one)
            and by-path opens, each driven through the directory channel as steps of the scenario's tick machine and
            checked in-process against what its fixture implies, with exactly one channel request per open (a listing
            that restarted would ask again). SP-TIER requires no synchronous browser directory operation on the
            interaction tier after boot.
  SP-LISTING-STALL
            a listing whose directory open is held far past the browser's 600-tick bound gives up with the card error:
            begun inside the open browser, it leaves the browser open on what it had; opening the browser, it closes
            it.
  SP-RELOAD / SP-RELOAD-STALL
            the scroll past a culled window's edge, which re-reads the folder: forward, wrapping up from the first
            song, and wrapping down from the last. It must run once, resume rather than restart, keep the old window on
            screen until it lands, and land on the right song; held past the bound it must give up with SD_CARD and put
            the window back.
  SP-PREVIEW-STALL
            a song preview whose prefix read is held far past the preview's 600-tick bound gives up with the card
            error, draws blank, and releases the scroll-in it owed.
  SP-DX     one DX7 bank picked in the DX browser, read through the prefix channel and parsed. The run requires the
            fixture's file, 32 patches and first patch name. SP-DX-WAIT prints the ticks it took.
  SP-DX-STALL
            the same bank with its read held far past the load's 600-tick bound: Enter gives up with the read-error
            popup, clears the load's flag and leaves the browser open.

DELUGE_SONG_CORPUS, a directory of real .XML songs, is optional: with it set, the fixture also stages up to 16 of them
as extra cases, which is the only part of this gate that sees documents nobody here wrote. Without it the synthetic
cases still run and the gate still gates.
"""

import re

# A floor on the case count, so a future edit that shrinks the fixture cannot keep printing PASS over a comparison that
# no longer covers the shapes this gate was built for. Counts the SYNTHETIC cases only: the corpus is the maintainer's
# and may legitimately be absent. Bump it deliberately when cases are added; never lower it to make a run go green.
MIN_CASES = 6

SYNTHETIC = re.compile(r"^SP-CASE (render|json|noprev|missing|corrupt|empty) ")
SUMMARY = re.compile(
    r"^(SP-SUMMARY|SP-DEFER|SP-WINDOW|SP-SWAP-SUMMARY|SP-PREVIEW-STALL) "
)
STAGES = re.compile(
    r"^(SP-LISTING|SP-NEWNAME|SP-PREDICT|SP-SLOT|SP-BYPATH|SP-DX|SP-TIER|SP-RELOAD)"
)


def check(gate, run):
    if not run.has("SP-SUMMARY "):
        gate.fail("no SP-SUMMARY line; the scenario did not reach its summary")
        return

    cases = len(run.starting("SP-CASE "))
    synthetic = sum(1 for line in run.lines if SYNTHETIC.match(line))
    if synthetic < MIN_CASES:
        gate.fail(
            f"only {synthetic} synthetic case(s) ran, fewer than the {MIN_CASES} this gate requires; kCases was shrunk"
        )
        return

    # Every song drawn has its reference beside it: each SP-CASE, the read-window song and the swapped song.
    references = len(run.starting("SP-REFERENCE "))
    if references != cases + 2:
        gate.fail(
            f"{references} reference decode(s) for {cases} case(s), the read-window song and the swap; "
            f"wanted {cases + 2}"
        )
        return

    gate.say(
        f"PASS — the prefix channel drew all {cases} song(s) as the file read does, agreed on every outcome, held "
        "across a card swap, and parsed the DX7 bank"
    )
    for line in run.lines:
        if SUMMARY.match(line):
            gate.say(line)
    for line in run.lines:
        if STAGES.match(line):
            gate.say(" run     " + line)
