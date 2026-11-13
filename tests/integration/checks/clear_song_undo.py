"""The clear-song/undo gate: Create New Song runs where its caller stands, reading no card of its own.

Drives Create New Song the way the context menu does -- clearSong.acceptCurrentOption() -- and requires the whole
operation to run where the caller stands, reading no card of its own, with the new song's first preset arriving
afterwards through InstrumentLoad on the interaction tier.

Clear-song reads no card of its own: ensureAtLeastOneSessionClip() builds its clip on the built-in default synth and
asks InstrumentLoad for a preset afterwards, and a loadAllSamples() after it could only ever load nothing. What it does
is model and UI work that belongs on the interaction tier, and this gate is what measures that it stays there.

What the checks cover (tests/integration/scenarios/clear_song_undo_scenario.cpp writes the lines):
    CS-PROBE   per stage: the fixture written, the boot song settled, and what the clear was seen to do.
    CS-TIER    the clear ran to completion before acceptCurrentOption() returned -- a dispatched body has not run yet
               -- and read no document and loaded no sample of its own, on either tier.
    CS-SONG    the cleared song holds one clip whose preset came from SYNTHS through exactly one InstrumentLoad
               acquisition, with its sample bound.
    CS-GATE    the clear left no UI-mode gate closed behind it.
    CS-NEXT    a next song held behind a take or a slice did not outlive the clear.

The registry entry requires each marker and the result line.

Gate clear-song-undo:
    Create New Song must run to completion on the tier its caller stands on, reading no card of its own. CS-TIER
    requires the clear to be finished when clearSong.acceptCurrentOption() returns -- a body deferred to a later tick
    has not run yet when it does -- and requires no document read and no sample load attributable to the clear itself,
    on either tier. CS-SONG requires the cleared song to hold one clip whose preset came from SYNTHS through exactly one
    InstrumentLoad acquisition, with its sample bound: that is what proves the clear needs no loadAllSamples() call:
    there is nothing for one to load. CS-GATE requires the UI-mode gate the clear closes around the song swap to be
    released again, because nullifyUIs() drops numUIsOpen to zero for that window and a press arriving inside it would
    index the UI stack out of bounds. CS-NEXT requires a next song a MIDI NEXT_SONG left held behind a take or a slice
    to be dropped by the clear, as main loads a NEXT_SONG at once and so never one after the song it was to follow is
    gone. The undo legs cover the other half. UN-PLAIN requires an undo with no sample in it to land INSIDE the press
    that asked for it, which a reversion deferred to a later tick cannot do; UN-REFUSE requires a redo pressed while an
    undo is still out to be refused rather than queued, because undo and redo are steps and dropping either loses an
    edit. UN-SAMPLE, UN-REDO and UN-MISSING pin what must not change: the sample comes back with a file behind it, redo
    puts the second one back, and a file deleted from the card still leaves the revert done with the holder empty.
    UN-MIDI requires a MIDI UNDO that tryGlobalMIDICommands() leaves waiting, with nothing in the scenario running the
    playback slow routine, to be undone by that routine's own job, which the waiting command asks for.
"""


def check(gate, run):
    gate.say(
        "PASS — the clear ran on the caller's tier, read no card, and its song landed on a preset"
        " InstrumentLoad acquired"
    )
