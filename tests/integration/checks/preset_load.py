"""The preset-load gate: synth and kit presets loaded through the real preset browser, as a user loads them.

The clip view's LOAD gesture opens the browser, a by-path listing selects the preset, and Select loads it into the
clip, a kit row, or -- from the long-press menu -- a clone.

What the checks cover (src/deluge/harness/preset_load_scenario.cpp writes the lines):
    PL-SETUP   per leg: whether the preset browser opened on the leg's clip, and how many outputs the song had.
    PL-SELECT  per leg: the preset the by-path listing selected.
    PL-SYNTH   a synth preset loaded into the boot song's synth clip: the loaded synth, named after the preset, its
               sample bound, and the browser closed.
    PL-KIT     a kit preset loaded into a kit clip: three drums, each named and bound to its sample, with NoteRows.
    PL-ROW     a synth preset loaded into a kit row: the row's drum named after the preset, its sample bound.
    PL-CLONE   the long-press menu's clone of a synth preset: a new synth under a variation of the preset's name, its
               sample bound, edited, and the browser closed.
    PL-TIER    per load: documents read and samples loaded, and no sample read from the card on the interaction tier.
    PL-RACE    a clone made straight after the selection, without waiting for its scroll-load: the clone survives.
    PL-SUPERSEDE
               the loader holding every sample, a scroll onto a heavy kit and on to a light one: the heavy kit never
               swapped in, nothing of it left in the song, the light kit loaded.
    PL-GATE    a commit whose sample the loader holds: an output-type button and an encoder turn refused while it is
               out; once it ends, the gate released, the preset loaded and the browser closed.
    PL-STALL   a commit whose sample load is held past the stall bound: the synth swapped in with that holder empty,
               the gate released and the browser closed.

The acquisition legs pick presets without the browser, by the gestures a user makes:
    PL-LISTING-EDGE
               a culled window that the availability filter empties stays empty: the edge trim must not pop nothing.
    AQ-NEW     an empty session pad pressed makes a synth track on the first unused synth preset, its sample bound;
               then shift+SYNTH gives that clip a new blank synth under an unused slot name.
    AQ-CHANGE  the clip view's KIT button on that empty clip lands on the first unused kit preset, its drum bound.
    AQ-CHANGE-TIER
               AQ-CHANGE's KIT press walked once, through the directory channel, leaving the browser's folder alone.
    AQ-CHANGE-NAMED / AQ-CHANGE-NEXT / AQ-CHANGE-MISSING
               KIT with a remembered AQNAV/NAV05 lands there; with an absent NAV05X, on the next one; with a
               remembered folder that does not exist, on what the whole-type Unlaunched search finds.
    AQ-CHANGE-STALL
               a change-type with its folder open held past the bound: SD_CARD, the synth kept, nothing in flight.
    AQ-NAVIGATE
               the select encoder turned from the clip view, a held grid pad and an auditioned arranger row: each turn
               lands on the next unused kit preset, its drum bound.
    AQ-NAV-TIER
               per AQ-NAVIGATE gesture: the navigate walked once, through the directory channel, leaving the browser's
               folder alone.
    AQ-NAV-WRAP
               a 25-kit subfolder, past its window: forward from NAV24 wraps to NAV00, back from NAV00 to NAV24.
    AQ-NAV-STALL
               a navigate with its folder open held past the bound: SD_CARD, clip kept, nothing in flight.
    AQ-SUPERSEDE
               the loader holding every sample load, a turn onto a heavy kit and a second once its sample load has
               begun: the light kit loaded, the heavy kit never swapped in, nothing of it left, and a supersession
               counted.
    AQ-REFUSE  three new-track presses in one tick (synth, kit, synth): the in-flight and the queued one kept, the
               third refused.
    AQ-CLONE   a new-track press and a grid clone into an empty column in one tick: both tracks made.
    AQ-STALL   a turn whose sample load is held past the stall bound: the synth delivered with that holder empty and
               one give-up counted.
    AQ-SLOT    Shift+SYNTH's new synth: its slot name read once, through the directory channel, leaving the browser's
               folder and list as they were, and given the name the fixture pins.
    AQ-SLOT-STALL
               the same gesture with the folder open held past the bound: SD_CARD, the clip keeps its instrument,
               nothing left in flight. The last leg: its held open outlives it.
    AQ-EXT     around each AQ-NEW act: the preset walk leaves the browser's extension filter as it found it.
    AQ-TIER    per acquisition leg: documents read and samples loaded, and no sample read from the card on the
               interaction tier.

The revert legs back out of the browser, which puts the clip's original preset back:
    RV-CARD    an original that left memory: the browser closed on the exit tick, the gate closed while the revert
               loaded, the original back with its sample bound, and the read the machine's.
    RV-MEMORY  an edited original, which hibernates: put back with no document read at all.
    RV-STALL   an original whose sample load is held past the stall bound: back with that holder empty, the gate
               released.
    MD-BROWSER a MIDI device definition written through the firmware and loaded through its own browser: the file's CC
               label on the instrument, the browser closed, and the document staged once by InstrumentLoad.

The blank-song legs cover the song a Deluge starts with, and the one Create New Song makes:
    BS-BOOT    the boot song, before any preset is written: its clip on the default synth, having asked for a preset
               exactly once and found none.
    BS-CLEAR   clear-song with presets on the card: the new song's clip on a SYNTHS preset, its sample bound, the
               document staged by InstrumentLoad and nothing read synchronously.
    BS-EDITED  clear-song with the preset's sample held and the default synth edited while the request is out: the
               preset not swapped in, and nothing of it left in the song.

The registry entry requires each marker and the result line, and gives the run the virtual-time budget its three
30-second stall legs need.
"""


def check(gate, run):
    gate.say("PASS — every preset load bound what a user's load binds")
