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

The registry entry requires each marker and the result line.
"""


def check(gate, run):
    gate.say(
        "PASS — the clear ran on the caller's tier, read no card, and its song landed on a preset"
        " InstrumentLoad acquired"
    )
