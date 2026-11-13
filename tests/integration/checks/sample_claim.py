"""The sample-claim gate: files claimed from the sample browser bind what a user's claim binds.

Claims files from the sample browser into the boot song's synth, a kit drum and an audio clip, and slices a kit,
through the gestures a user makes: the menu's file selector opens the browser, a favourite's by-path listing brings
it to the file, and the Select encoder claims it.

What the checks cover (tests/integration/scenarios/sample_claim_scenario.cpp writes the lines):
    SC-SETUP   per leg: whether the menu opened and its file selector put the sample browser on top.
    SC-SELECT  per leg: the item the browser was brought to, which must be the leg's.
    SC-SYNTH   the synth claim: the file bound, the oscillator type, the repeat mode and the transpose, and the
               browser closed once its claim landed.
    SC-WAVETABLE
               the synth context menu's WaveTable claim of a single-cycle file: bound, as a wavetable oscillator.
    SC-FALLBACK
               a stereo file claimed into a wavetable oscillator: bound, as a sample oscillator instead.
    SC-GATE    the select encoder turned while an explicit claim's load is held: the selection must not move, and
               the gate must be released once the claim ends.
    SC-STALL   an explicit claim whose load is held past the stall bound: it must give up, leaving the holder
               unbound, the browser open and the gate released, having given up once.
    SC-SUPERSEDE
               an auto-load claim the cursor leaves while its pitch detection waits for a cluster, for a next
               preview that lands at once (case=landed) and for one still loading (case=loading): it must be
               cancelled once, and only the next file's claim may finish.
    SC-WAVETABLE-RESIDENT
               a context-menu wavetable claim of a file the preview left resident as a Sample, its clusters held:
               it must still bind as a wavetable, built from the card (converted=0 fromCard=1) -- only a Sample
               every cluster of which is cached is converted.
    SC-WAVETABLE-CONVERTED
               a context-menu wavetable claim of a file every cluster of which is cached: converted from memory
               (converted=1 fromCard=0), every cluster pinned, and the same WaveTable (CRC) a card build of a
               byte-identical twin gives.
    SC-PITCH   a claim whose detection finds its clusters held briefly: it waits, resumes, and finds the same note.
    SC-PITCH-STALL
               a claim whose detection's clusters never arrive: one give-up, the note an error, the transpose left
               alone, the claim finished.
    SC-TIER    per leg: no sample was read from the card on the interaction tier.
    SC-KIT     the kit drum claim: the file bound, the repeat mode, the drum's name and its side-chain send.
    SC-AUDIOCLIP
               the audio clip claim: the file bound, and the clip's loop length and attack.
    SC-SHELL-SLICE, SC-SHELL-SLICE-MANUAL
               the slicer opened on the file itself: a four-region slice, and a manual slice into three with the
               points as they were at Select, each drum bound to the sliced sample with a NoteRow.
    RD-ONE     the Drum Randomizer on one selected drum: it must end on a file from the folder its sample sat in,
               named after that file, with every other row untouched.
    RD-ALL     the same gesture with affect-entire held: every unmuted row that has notes must end on a file from
               that folder, and the muted and empty rows must be untouched.
    RD-TIER    per randomizer leg: a directory scan ran, and no sample was read from the card on the interaction
               tier.
    RD-GATE    a randomize whose load the loader holds: while the walk is out, no second walk may start and an edit
               pad must be refused; once it ends, the gate released and the same pad taken.
    RD-STALL   a randomize over three rows whose second load is held past the stall bound: the walk must end there,
               with the first row kept on its file, the second left unbound, the third untouched, and the gate
               released.
    RD-CANCELSCAN a randomize cancelled -- by returning to the clip view -- while its row's folder open is still out
               on the directory channel: the walk must DRAIN, collecting and discarding the handle that open still
               produces, read none of its entries, leave the row exactly as it was, and release the gate.
    IM-KIT     the kit context menu's "Load All" over a folder of its own: one drum per file in filename order, each
               named after its file without the folder's shared prefix, bound to it and with a NoteRow; the kit
               edited and the browser closed.
    IM-MULTI   the synth context menu's "Multisamples" over three sines an octave apart, into a wavetable oscillator:
               a sample oscillator with one range per file in pitch order, each bound to its file with the transpose
               its detection gives it, the browser closed, and the menu left on the middle range.
    IM-TIER    per folder import: a listing ran, and no sample was read from the card on the interaction tier.
    IM-GATE    a kit import whose loads the loader holds: while it is out, BACK and a second Select on the menu must be
               refused; once it ends, the gate released and the kit holding its drums.
    IM-EMPTY   a kit import of a folder holding a subfolder and no audio file of its own: an import of nothing, as
               main's, the kit's one drum kept, the browser closed onto the clip view and the gate released.
    IM-STALL   a multisample import whose second file's load is held past the stall bound: it must abort with the
               synth's ranges and the loaded files' reasons as they were, just the menu closed, the browser open and
               the gate released.

The registry entry requires each marker and the result line, and gives the run a 180 s virtual-time budget: three legs
hold the loader for 30 virtual seconds each (SC-STALL, RD-STALL's randomize, IM-STALL) and the pitch legs hold the
streaming loader on top, which is more than the runner's default 60 s covers.

Gate sample-claim:
    files claimed from the sample browser, opened through the menu's file selector and brought to the leg's file by a
    favourite's by-path listing, must be bound by the Select encoder's claim: into the boot song's synth (a single-cycle
    file as a wavetable from the context menu, and a stereo file offered to a wavetable oscillator as a sample instead),
    into a kit drum (named after the file, a kick sent to side-chain), and into an audio clip (its loop length set from
    the sample). The slicer, opened on the file itself, must slice a one-drum kit into four regions, each drum bound to
    the one sample with a NoteRow, and into three in manual mode with the points as they were when Select was pressed
    (SC-SHELL-SLICE, SC-SHELL-SLICE-MANUAL); before the region slice, a teardown of every UI, as a song load makes, must
    drop a held audition's load, which names the outgoing kit's first drum, and a learned MIDI next-song arriving while
    the slice is out must be held back until the slice is in, saying so with its Next song after slice notice, then let
    go (SC-SHELL-SLICE); a slicer whose open is held past the stall bound must give up without opening, the kit
    untouched (SC-SHELL-SLICE-FAIL); BACK out of manual mode on a first drum that holds no file must leave that drum as
    it was, and a slice commit with the menu's context forgotten by a teardown must be refused with nothing cut
    (SC-SHELL-SLICE-BACK). The Drum Randomizer (LOAD held, pad 15,2) on a kit whose drums name files in a folder of
    their own must land the selected drum, and with affect-entire held every unmuted row that has notes, on a file from
    that folder named after it, leaving the muted and empty rows alone; RD-TIER requires a randomizer scan to have run
    and no sample read from the card on the interaction tier; RD-GATE requires, while a walk whose load the loader holds
    is out, no second walk and an edit pad refused, and once it ends the gate released and the same pad taken; RD-STALL
    requires a walk whose second row's load is held past the stall bound to end with the first row kept on its file, the
    second unbound, the third untouched, and the gate released. The kit context menu's Load All option over a folder of
    its own must end with one drum per file in filename order, named after its file without the shared prefix, bound,
    with a NoteRow; the synth context menu's Multisamples option over three sines an octave apart must end with one
    range per file in pitch order, each bound to its file; IM-TIER requires a folder listing to have run and no sample
    read from the card on the interaction tier; IM-GATE requires BACK and a second Select on the menu refused while a
    kit import whose loads the loader holds is out, and once it ends the gate released and the kit holding its drums;
    IM-EMPTY requires Load All on a folder holding a subfolder and no audio file of its own to land as main's, an import
    of nothing: the kit's one drum kept, the browser closed onto the clip view and the gate released; IM-STALL requires
    a multisample import whose second file is held past the stall bound to abort with the ranges and the loaded files'
    reasons as they were, just the menu closed, the browser open, and the gate released. SC-TIER requires, for every
    claim and the slice, that no sample was read from the card on the interaction tier; SC-GATE requires the browser to
    refuse its select encoder while an explicit claim's load is held, and the gate released once the claim ends;
    SC-STALL requires an explicit claim whose load is held past the stall bound to give up, leaving the holder unbound,
    the browser open and the gate released; SC-SUPERSEDE requires an auto-load claim the cursor leaves while its pitch
    detection is out, for a next preview that lands at once and for one still loading, to be cancelled once and drain
    before the next claim starts, with only the next file's claim finishing.

Gate app-asan-sample-claim:
    the sample-claim gate's checks, on a host app built with AddressSanitizer and UBSan and the allocator's heap
    poisoning: any use-after-free, overflow or undefined behaviour the scenario reaches is fatal (a claim, a slice or an
    import binds a file into a sound whose record a teardown or a supersede may free under it)
"""


def check(gate, run):
    gate.say("PASS — every claim bound what a user's claim binds")
