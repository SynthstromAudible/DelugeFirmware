# Content-card parity scripts

Level-3 UI scripts for `tools/emu_parity.py` that need a card with content: presets, kits, samples and songs. Run them
with `--sd` on an image of such a card (the songs these load are on Kate's backup card). Each starts from a fresh boot.

A script meant for the ported UI (the ShellMenu card) must start from the clip view the boot leaves: a CLIP press there
switches to the automation view, where Shift+pad opens legacy's sample browser even with ShellMenu on. Check which UI a
script reaches before trusting a MATCH from it.

`c16-marker-synth` and `c17-marker-mute-exit` reach the sample marker editor with Shift + pad (0,7) after c10's BASIC
load: a marker is held by pressing its own column (the start marker is column 0 there), and a second pad tapped while
holding can end the hold, so BACK and the mute column are pressed before any such tap. `c18-synth-multisamples` imports
a folder as ranges; `c19-kit-load-all-empty` runs LOAD ALL on `SAMPLES/Artists`, which holds only folders (D54).

`c20` to `c22` build a multisample as c18 does (MULTISAMPLES on the drums folder, the import landing on the clip view)
and open the note-range picker with Shift + pad (2,1), OSC1's transpose: `c20` the picker, its memory of the played
range and the root's range highlight; `c21` the edge, insert and delete edits and SAVE; `c22` the keyboard screen,
entered before any menu. `c23` walks the sample source items on one sample loaded as c10 loads it, and the recorder's
return. Popups and console text last about a second, so their snapshots sit inside that window on both firmwares.

`c24` to `c27` run on the content card with `EnableDX7Engine` set in `SETTINGS/CommunityFeatures.XML` (with and
without `shellMenu`). The firmware's MOD7 is the emulator's `Mod6`: held with SYNTH it creates an FM synth, which opens
"DX7 source". `c24` sets oscillator 1's type to DX7 from its shortcut and from the vertical walk and enters "DX7
source"; one detent at a time, since main's horizontal page takes a multi-detent turn differently. `c25` is the new FM
synth and its browse row, `c26` the DX shortcut pad (4,0), whose modulator item does not apply to a DX7 sound, and
`c27` every DX7 parameter page layout, the horizontal wrap, Shift+horizontal and the engine version.
