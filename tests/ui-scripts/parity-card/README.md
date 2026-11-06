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
