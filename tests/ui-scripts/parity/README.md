# Parity scripts

Short level-3 UI scripts for `tools/emu_parity.py`, which plays each on a reference image (current `main`) and the
image under test in delugemu and requires identical panels at every snapshot. Each script starts from a fresh boot
and covers one surface, so a divergence in one cannot hide the others. Format: `tests/harness/README.md`; times are
milliseconds after boot settles, and a snapshot waits for both panels to settle.

Scripts named `sm*` are written for the ShellMenu card (`.superpowers/emu-card/shellmenu`): they enter the menus
through SelectEnc, which opens the ported menu when ShellMenu is on. They remain valid with ShellMenu off, where the
same presses walk the legacy menus.

`sm00-select-turn` checks the emulator's encoder input before anything leans on it: of five separate single Select
detents, the first moves nothing and each of the others moves the sound menu's list one row, on both images, ShellMenu
on and off. A black encoder drops the first detent of its first turn after boot and of any turn that reverses its
direction (each decoder starts at its detent's resting state): count one extra detent for those turns. `sm10-patched-blink` covers the
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

`sm28-menu-view-buttons` covers the sound editor's view buttons under the menu: KEYBOARD over the clip view, the
keyboard screen and the automation view, SESSION, and SYNC-SCALING. Run it on the ShellMenu card and on
`.superpowers/emu-card/shellmenu-fill` (the ShellMenu card with `syncScalingAction` 1, Fill), where SYNC-SCALING
holds fill and lights its LED.

`sm29-audition-shortcut` covers the shortcut pads with an audition held inside the sound menu, on a synth and on a kit
row (the edit stays on the row the menu opened for, and so does the kit's selection), and the sequence-direction pad's
note-row editor. It runs on the content card with ShellMenu on (`sweep-shellmenu.img`), in the sweep's `browser`
group. It stops on the note-row editor: BACK there sticks on `main` (D92).

`sm19-sample-browser-preview`, `sm20-sample-browser-waveform`, `sm21-sample-browser-buttons` and
`sm22-sample-browser-favourites` cover the sample browser that SHIFT + BROWSE opens from the synth clip: the greyed
root view and its BROWSE blink, the preview and its waveform, the horizontal scroll-in as the selection moves, the
keyboard on demand and KEYBOARD, the X_ENC zoom and the waveform's scroll, LOAD's auto-load, SHIFT + SAVE's delete
prompt and refusal, the audition column, RECORD's recorder, and the favourites row with its 500 ms flash. They run on
the content card with ShellMenu on (`.superpowers/emu-card/content-shellmenu`: the content card plus the ShellMenu
card's `SETTINGS/CommunityFeatures.XML`), ShellMenu off on `content`, and `sm19` with `--audio-wav`. Windows over a
blink are at least 1 s; the scroll-in is read 100, 300 and 400 ms after the turn in short windows; a popup is read
100 ms after the press, before it times out.

`sm23-select-in-place` covers SELECT on an item main does not enter: the arpeggiator's ENABLED row and USB AUDIO's
INPUT toggle flip on their list, and a choice column (POLYPHONY on the VOICE page, set to MONO) stays on its page. Its
turns count the dropped first detent after boot (`+3` to ARPEGGIATOR) and none after (`+5` to USB AUDIO).

`sm26-follow-channel` covers a MIDI follow channel's own screen: the differentiation line, the device and the channel,
turned through CHANNEL 1, UNASSIGNED and both MPE zones, and the device a learned note names.

`sm27-learn-select-turn` checks that the select encoder edits nothing while LEARN is held on a param's page, and edits
again once it is released; its windows run a second or more over the patching sources' 800 ms blink.

`sm24-sidechain-columns` and `sm25-cable-polarity` cover the patch-cable strength items. `sm24` opens the two fixed
cables' columns from their shortcut pads (the SIDECHAIN page's DUCK meter, the MASTER page's VIBRATO text), SELECT on
each into its strength page and on to MODULATE DEPTH, and BACK out again. `sm25` drives a strength page: the cursor
across the places, MIDI and CV and a turn under a held Select switching the polarity with their LEDs, a plain and a
SHIFT turn. Both read values `main` draws exactly: `main`'s build truncates a non-zero strength whose zero places are
hidden (D71), so a strength is read with the cursor on its hundredths, or at 0.

The cards' factory presets are stamped `firmwareVersion` 4.1.3 (`tools/stamp_preset_versions.py`). `main` converts a
preset older than official 4.1 as it loads it, and our line leaves that to `tools/deluge-upgrade`, so an unstamped
preset shows the conversion in every comparison that touches it instead of the UI.
