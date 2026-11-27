# Content-card parity scripts

Level-3 UI scripts for `tools/emu_parity.py` that need a card with content: presets, kits, samples and songs. Each
script's `# card:` lines name its cards; `tools/emu_parity.py tests/ui-scripts/parity-card` runs them. Each starts from
a fresh boot.

Check which UI a script reaches before trusting a MATCH from it.

`c16-marker-synth` and `c17-marker-mute-exit` reach the sample marker editor with Shift + pad (0,7) after c10's BASIC
load: a marker is held by pressing its own column (the start marker is column 0 there), and a second pad tapped while
holding can end the hold, so BACK and the mute column are pressed before any such tap. `c18-synth-multisamples` imports
a folder as ranges; `c19-kit-load-all-empty` runs LOAD ALL on `SAMPLES/Artists`, which holds only folders.

`c41-marker-automation` opens the marker editor from the sound menu over the automation view (OSCILLATOR 1, two CROSS
SCREEN pages, then the SYNTH button on START): Shift + (0,6) is the editor's own press there, and Shift + (7,1),
POLYPHONY, jumps although it is no param.

`c20` to `c22` build a multisample as c18 does (MULTISAMPLES on the drums folder, the import landing on the clip view)
and open the note-range picker with Shift + pad (2,1), OSC1's transpose: `c20` the picker, its memory of the played
range and the root's range highlight; `c21` the edge, insert and delete edits and SAVE; `c22` the keyboard screen,
entered before any menu. `c23` walks the sample source items on one sample loaded as c10 loads it, and Shift + (0,4),
which lands on the SAMPLE RECORDER page. Popups and console text last about a second, so their snapshots sit inside that
window on both firmwares.

The firmware's MOD7 is the emulator's `Mod6`: held with SYNTH it creates an FM synth, which opens "DX7 source". `c24`
sets oscillator 1's type to DX7 from its shortcut and from the vertical walk and enters "DX7 source"; one detent at a
time, since main's horizontal page takes a multi-detent turn differently. `c25` is the new FM synth and its browse row,
`c26` the DX shortcut pad (4,0), whose modulator item does not apply to a DX7 sound, and `c27` every DX7 parameter page
layout, the horizontal wrap, Shift+horizontal and the engine version.

`c28` is the keyboard screen's DX column (on the sidebar's right column, x 17): a pad switches its operator off, and
Shift + a pad opens the DX7 parameter page on that operator or toggles it, with the edited operator's row flashing
white; its keyboard scaling breakpoint is raised onto the grid so its note's pad blinks. `c29` presses Shift + a DX
column pad with the menu already open over the keyboard. `c30` is the `.syx` browser and the cartridge page: BACK from
the cartridge page reopens the browser, and a second BACK returns to "DX7 source". `c31` is the parameter page's own pad
layout: Shift shows the DX colour map and Shift + a pad picks its parameter, since a bare pad leaves the menu for the
clip.

`c32-midi-linked` runs on the fixture card plus one MIDI device definition, `MIDI_DEVICES/DEFINITION/BLOFELD.XML`. It
turns the boot clip's track into a MIDI track, links that definition from DEVICE DEFINITION > LINKED (the file's name
then shows two rows under LINKED), and walks SETTINGS > MIDI > DEVICES > DIN PORTS > MPE to the zone page, which has no
title.

`c33-kit-affect-entire` covers AFFECT ENTIRE in a kit row's menu: the clip's toggle on an item that does not use it (the
root list, the kit's own root) and the fast-blinking hold on one that does (the row's LPF frequency), with a turn under
it.

`c35-sample-claim-wait` reads the sample browser while SELECT's load runs: c16's walk into `SAMPLES/AUDIO FILES`,
SELECT on `REC00003`, and a 25 ms window 90 ms later, while the working animation turns over the browser's list. The
load takes about 400 ms on the emulator, so the window sits well inside it on both firmwares.

`c36-drum-creator-refused` runs on the fixture card with `SAMPLES` a file, so no sample browser can list. SHIFT + KIT's
drum creator then falls back the way `main` does: the browser's error popup over the new kit's clip view.

`c42-dx-browser-no-folder` runs on the fixture card with `EnableDX7Engine` on (`main`'s feature, which the branch's built-in DX7 ignores) and no `DX7` folder. The new FM synth's
browse row creates `DX7` and opens the `.syx` browser on it, empty; a Select turn and SELECT on the empty list do
nothing, and BACK leaves it.

`c39-browser-record-audition` opens the sample browser with Shift + (0,5) and presses RECORD with the audition pad
(17,3) held, which the browser refuses, then without it, which opens the recorder.

`c43-multisample-import-range` builds c20's multisample, opens the note-range picker with Shift + (0,5) (the file
selector waits on it), turns it to the bottom range and opens the browser from it. A long press on the browser's file
imports its folder again (MULTISAMPLES), and the browser closes onto the picker, which `main` opens on the middle range
the import left current.

`c44-page-import-transpose` imports `DRUMS/Block` as MULTISAMPLES from the OSC1 page's file column (Shift + (2,2),
then KIT twice), lands back on the page, and presses MIDI, the transpose column's button. The page's begin drops
`main`'s selected range (`Submenu::beginSession`), so with several ranges the button opens the note-range picker.

`c46-page-switch-range` picks a range on the OSC1 page through MIDI's note-range picker (SELECT, transpose, BACK
twice) on c20's multisample, then switches pages with SHIFT + CROSS SCREEN and back with SHIFT + SCALE. The switch
begins the page, which drops `main`'s range, so MIDI opens the picker again.

`c47-file-column-select` presses SELECT on the OSC1 page's focused file column (Shift + (2,2), KIT) on c20's
multisample, with no range selected. `main` runs the range check and opens the note-range picker, heading to the file
selector.

`c37-recorder-threshold` sets the song's threshold to LOW with RECORD held and two Select turns (the first only
shows the mode) and opens the recorder from its page, which draws the level meter.

`c38-recorder-take` opens the recorder from that page and ends the take with RECORD, which lands on the source page
with the file selector focused.

`c34-drum-record` loads a kit and presses RECORD with row 0's audition pad held, which makes a drum and opens the
recorder on it; its (17,0) colour differs on ours as D11 says.
