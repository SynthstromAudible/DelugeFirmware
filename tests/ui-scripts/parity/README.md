# Parity scripts

Short level-3 UI scripts for `tools/emu_parity.py`, which plays each on a reference image (current `main`) and the
image under test in delugemu and requires identical panels at every snapshot. Each script starts from a fresh boot
and covers one surface, so a divergence in one cannot hide the others. Format: `tests/harness/README.md`; times are
milliseconds after boot settles, and a snapshot waits for both panels to settle.

Scripts named `sm*` are written for the ShellMenu card (`.superpowers/emu-card/shellmenu`): they enter the menus
through SelectEnc, which opens the ported menu when ShellMenu is on. They remain valid with ShellMenu off, where the
same presses walk the legacy menus.
