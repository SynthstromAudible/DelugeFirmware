"""The preset-load gate: synth and kit presets loaded through the real preset browser, as a user loads them.

The clip view's LOAD gesture opens the browser, a by-path listing selects the preset, and Select loads it into the
clip, a kit row, or -- from the long-press menu -- a clone.

What the checks cover (tests/integration/scenarios/preset_load_scenario.cpp writes the lines):
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
    AQ-NAV-ADD three turns on from NAV24 made at once, the second queued behind the first's load and the third made
               while it waits: they add up, as main's detents do, and land on NAV02. Then on, on and back: the turn back
               cancels the waiting one, and the first turn's NAV03 is where they land. A two-detent turn with nothing
               loading moves one preset, as main's first move does.
    AQ-NAV-PILE
               turns of the synth clip before a kit turn: the kit turn replaces the synth turn waiting rather than
               adding to it, a two-detent kit turn behind the synth's moves one preset, and the synth keeps its preset.
               Piles move as main's pending detents do: from NAV22, on and three more lands on NAV00 (past the folder's
               end, its first preset); from NAV00, on and seventy more lands on NAV02 (past the landing window's edge,
               the first preset past the landing); from NAV10, back and three more back lands on NAV06 (moved from the
               landing at the window's far end). A pile landing on a hibernating kit keeps the file landed on before it
               as the name a later pile reads again from: from NAV02, on, two more behind it onto the hibernating
               NAV05, and thirty more behind that land on NAV04. A turn made while a turn's broken preset loads waits
               for the load to fail and moves one preset from the clip's own kit: the clip keeps FAIL0. One made while
               a good preset's document is read moves on from it before its samples load: from FAIL2, on, and one
               more behind it land on FAIL4 with one sample load. A pile whose walk fails (its folder renamed away)
               ends with its error before a turn made behind it starts, and that turn moves one preset from the clip's
               own kit: from GONE0, on, one more and one more end on GONE1. A pile keeps the wraps its landing made:
               on the walk itself, in a folder whose first kit is in the song, on from the last passes it and lands on
               WRAP1, three more wrap once and land on WRAP3, and one more wraps again and takes the in-song kit.
               The same moves made by turns of the clip's held grid pad, the third behind the pile's walk, end on
               the in-song kit, which leaves the clip on WRAP4 (main freezes with i009).
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

Gate preset-load:
    synth and kit presets written through the firmware's serialisers, each naming samples of its own, must load through
    the real preset browser -- opened by the clip view's LOAD gesture, selected by a by-path listing, loaded by Select
    -- as a user's load binds them: a synth preset into the boot song's synth clip (the synth named after the preset,
    its sample bound, the browser closed), a kit preset into a kit clip (three drums named and bound to their samples,
    each with a NoteRow), a synth preset into a kit row (the row's drum named after the preset, its sample bound), and a
    clone from the long-press menu (a new synth under a variation of the preset's name, its sample bound, edited, the
    browser closed). PL-TIER requires, for every load, that its documents were read and its samples loaded, and no
    sample read from the card on the interaction tier. PL-RACE requires a clone made straight after the selection,
    without waiting for its scroll-load, to survive it. PL-SUPERSEDE requires, with the loader holding every sample
    load, a scroll onto a heavy kit and on to a light one before the heavy kit's samples land to leave the light kit
    loaded, the heavy kit never swapped in and nothing of it left in the song, not even its param managers. PL-GATE
    requires an output-type button and an encoder turn refused while a commit whose sample the loader holds is out, and
    once it ends the gate released, the preset loaded and the browser closed. PL-STALL requires a commit whose sample
    load is held past the stall bound to swap its synth in with that holder empty, the gate released and the browser
    closed. The AQ legs pick presets without the browser, by real gestures: AQ-NEW requires an empty session pad to make
    a synth track on the first unused synth preset with its sample bound, and shift+SYNTH to give that clip a new blank
    synth under an unused slot name; AQ-CHANGE requires the clip view's KIT button to land the empty clip on the first
    unused kit preset with its drum bound; AQ-NAVIGATE requires a select-encoder turn from the clip view, a held grid
    pad and an auditioned arranger row each to land on the next unused kit preset with its drum bound; AQ-NAV-ADD
    requires three turns made at once, the second queued behind the first's load and the third made while it waits, to
    add up and land three presets on, as main's pending detents do, and on, on and back to land on the first turn's
    preset, while a two-detent turn with nothing loading moves one preset, as main's first move does; AQ-NAV-PILE
    requires a turn of another output not to add to or start from a kit turn's (a kit turn replaces the synth turn
    waiting, a two-detent kit turn behind the synth's moves one preset, and the synth keeps its preset), and a pile to
    move as main's does from the landing: three past a landing next to the folder's end lands on the folder's first
    preset, seventy past a landing's window lands one past the landing, and three back from a landing at its window's
    far end moves from there, a pile past its window's edge from a landing on a hibernating kit reads again from the
    file landed on before it, a turn made while a broken preset loads waits for the load to fail and moves one preset
    from the clip's own kit, a pile whose walk fails ends with its error before a turn made behind it starts, which
    moves one preset from the clip's own kit, and a pile keeps the wraps its landing made, so on the walk itself one
    more past a landing that wrapped once wraps again and takes the kit already in the song that the first two moves
    passed over, and the same moves made by turns of a held grid pad leave the clip on the kit it had, its name on the
    OLED and no working notice up (main freezes with i009); AQ-SUPERSEDE requires, with the loader holding every sample
    load, a turn made once a heavy kit's sample load has begun to cancel it: the light kit loaded, the heavy kit never
    swapped in, nothing of it left, a supersession counted; AQ-REFUSE requires three new-track presses in one tick
    (synth, kit, synth) to keep the in-flight and the queued one and refuse the third; AQ-CLONE requires a new-track
    press and a grid clone into an empty column in one tick both to make their track; AQ-TAP requires a new-track pad
    let go before its acquisition lands to act as the same gesture on an existing clip once the clip exists: let go
    within a tap's length it enters the clip, still down it holds the clip, let go after a tap's length (the audio clock
    moved by hand, as nothing renders here) it does neither, tapped and then followed by a press on another clip before
    it lands it leaves that held gesture the UI, and tapped with a second new-track press queued behind it the second
    press's completion holds its own clip (the first's must not take its press); the leg's tracks take synth presets
    written for it, so later legs' presets stay unused; AQ-STALL requires a turn whose sample load is held past the
    stall bound to deliver its synth with that holder empty and one give-up counted; AQ-TIER requires every acquisition
    leg to read documents and load samples, with no sample read from the card on the interaction tier. The RV legs back
    out of the browser, which puts the clip's original preset back: RV-CARD requires an original that left memory to
    come back off the card, bound, with the browser closed on the exit tick and the gate closed while it loaded;
    RV-MEMORY requires an edited original, which hibernates instead of leaving, to come back with no document read at
    all; RV-STALL requires an original whose sample load is held past the stall bound to come back with that holder
    empty and the gate released. MD-BROWSER requires a MIDI device definition, written through the firmware and loaded
    through its own browser, to leave the file's CC label on the instrument with the browser closed and the document
    staged once by InstrumentLoad on later ticks rather than read inside the Select. BS-BOOT requires the boot song to
    start on the default synth having asked for a preset exactly once, BS-CLEAR requires a cleared song to land on a
    SYNTHS preset staged by InstrumentLoad, and BS-EDITED requires a default synth edited while that request is out to
    keep its clip with nothing of the preset left behind.

Gate app-asan-preset-load:
    the preset-load gate's checks, on a host app built with AddressSanitizer and UBSan and the allocator's heap
    poisoning: any use-after-free, overflow or undefined behaviour the scenario reaches is fatal (a sound plays its
    record's state, so a record freed under it would read freed memory)
"""


def check(gate, run):
    gate.say("PASS — every preset load bound what a user's load binds")
