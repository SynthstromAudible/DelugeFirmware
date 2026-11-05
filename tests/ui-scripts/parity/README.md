# Parity scripts

Short level-3 UI scripts for `tools/emu_parity.py`, which plays each on a reference image (current `main`) and the
image under test in delugemu and requires identical panels at every snapshot. Each script starts from a fresh boot
and covers one surface, so a divergence in one cannot hide the others. Format: `tests/harness/README.md`; times are
milliseconds after boot settles, and a snapshot waits for both panels to settle.

Scripts named `sm*` are written for the ShellMenu card (`.superpowers/emu-card/shellmenu`): they enter the menus
through SelectEnc, which opens the ported menu when ShellMenu is on. They remain valid with ShellMenu off, where the
same presses walk the legacy menus.

`sm00-select-turn` checks the emulator's encoder input before anything leans on it: five separate single Select
detents each move the sound menu's list one row on both images, ShellMenu on and off. `sm10-patched-blink` covers the
patched-param and patching-source blinks; its snapshots have 1 s windows because a source patched to a param flashes
once every 800 ms, and a shorter window decides by phase alone whether it sees the flash.

`sm13-midi-selftest` checks the emulator's DIN MIDI input the same way: a note and then a CC learned to RECORD each
toggle its LED, once per message, on both images.

`sm14-learn-commands` and `sm15-learn-params` cover MIDI learn in the menus, ShellMenu on and off: the MIDI commands
(from the list and on an item, FILL's refusal), a param's gold-knob, CC and pitch-bend learn and unlearn, and the MIDI
follow channel.

`sm16-automation-retarget`, `sm17-automation-menu-view` and `sm18-automation-kit` cover the automation view under the
menu: the param it follows as the menu moves (SELECT, BACK, column buttons, shortcuts), the pads, knobs and encoders
that reach it, CLIP's "menu view" and KEYBOARD from it, a patch cable's page, and a kit's rows and affect-entire. Every
snapshot has a 1 s window, for the interpolation and shortcut blinks, except the boot and two snapshots over the
scrolling preset name. Their walks rest on how `main`'s menus move: a single large Select turn is capped, so long
moves are several turns; the sound menu reopens on the row it last showed; BACK from a page a shortcut opened leaves
the menu.
