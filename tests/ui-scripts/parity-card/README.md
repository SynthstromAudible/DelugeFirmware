# Content-card parity scripts

Level-3 UI scripts for `tools/emu_parity.py` that need a card with content: presets, kits, samples and songs. Run them
with `--sd` on an image of such a card (the songs these load are on Kate's backup card). Each starts from a fresh boot.

A script meant for the ported UI (the ShellMenu card) must start from the clip view the boot leaves: a CLIP press there
switches to the automation view, where Shift+pad opens legacy's sample browser even with ShellMenu on. Check which UI a
script reaches before trusting a MATCH from it.
