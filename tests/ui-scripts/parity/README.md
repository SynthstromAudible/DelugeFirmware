# Parity scripts

Short level-3 UI scripts for `tools/emu_parity.py`, which plays each on a reference image (current `main`) and the
image under test in delugemu and requires identical panels at every snapshot. Each script starts from a fresh boot
and covers one surface, so a divergence in one cannot hide the others. Format: `tests/harness/README.md`; times are
milliseconds after boot settles, and a snapshot waits for both panels to settle.

`p16`–`p27` are longer walks: the automation view on a kit and the song (`p16`), a MIDI clip (`p17`) and a synth
(`p18`), the cable menus (`p19`), learn (`p20`), MIDI follow's CCs (`p21`), the mod knobs (`p22`), the performance view
(`p23`), param undo (`p24`), the gold knobs pressed with a menu up (`p25`), a learned CC (`p26`) and the gold knobs at
the root views (`p27`). They make an input every 250 ms (a press held 120 ms), and each snapshot is a 1 s
window. Their results are in `DIFFERENCES.md`'s `p16`–`p24` section.

Each script's `# card:` lines name the cards it runs on, and `tools/emu_parity.py tests/ui-scripts/parity` runs it on
each. Scripts named `sm*` enter the menus through SelectEnc, which opens the menu.

`sm00-select-turn` checks the emulator's encoder input before anything leans on it: of five separate single Select
detents, the first moves nothing and each of the others moves the sound menu's list one row, on both images. A black
encoder drops the first detent of its first turn after boot and of any turn that reverses its direction (each decoder
starts at its detent's resting state): count one extra detent for those turns. `sm10-patched-blink` covers the
patched-param and patching-source blinks; its snapshots have 1 s windows because a source patched to a param flashes
once every 800 ms, and a shorter window decides by phase alone whether it sees the flash.

`sm13-midi-selftest` checks the emulator's DIN MIDI input the same way: a note and then a CC learned to RECORD each
toggle its LED, once per message, on both images.

`sm14-learn-commands` and `sm15-learn-params` cover MIDI learn in the menus: the MIDI commands
(from the list and on an item, FILL's refusal), a param's gold-knob, CC and pitch-bend learn and unlearn, and the MIDI
follow channel.

`sm32-menu-value-follow` covers the open value following a change made behind the menu: a learned gold knob and a
learned CC moving the LPF frequency that a page or a full screen shows, which `main` re-reads on the 25 ms
`DISPLAY_AUTOMATION` timer, with no knob popup over the same param.

`sm33-full-screen-values` runs with HorizontalMenus off, where a shortcut opens its item alone: a transpose's cursor,
step and Horizontal move, the coming-soon popup inside the menu, and retrigger phase's cursor. Its popup is read 100 ms
after the press.

`sm34-claim-back` covers a sample claim made from the audio clip's SAMPLE page (opened from the clip's root list) and
the BACKs after it: the claim returns to the SAMPLE page and the first BACK leaves the menu, on both images, with
Horizontal Menus on. It converts the boot song's empty clip to audio (a held clip pad + SELECT), so it needs a content
card's presets. The OLED, LEDs and UI stack agree at every snapshot; `claimed`, `back1` and `back2` differ only in the
colour of the waveform row (y 4, 16 pads), the new audio clip's random hue (D11).

`sm35-toggle-words` covers the words a toggle draws, with HorizontalMenus off (where a shortcut opens its item alone): a
Community Feature's row is a checkbox SELECT flips in place, OSC SYNC's page lists Disabled and Enabled as `main`'s
does, and PING-PONG's lists its options.

`sm16-automation-retarget`, `sm17-automation-menu-view` and `sm18-automation-kit` cover the automation view under the
menu: the param it follows as the menu moves (SELECT, BACK, column buttons, shortcuts), the pads, knobs and encoders
that reach it, CLIP's "menu view" and KEYBOARD from it, a patch cable's page, and a kit's rows and affect-entire. Every
snapshot has a 1 s window, for the interpolation and shortcut blinks, except the boot and two snapshots over the
scrolling preset name. Their walks rest on how `main`'s menus move: a single large Select turn is capped, so long
moves are several turns; the sound menu reopens on the row it last showed; BACK from a page a shortcut opened leaves
the menu.

`sm28-menu-view-buttons` covers the sound editor's view buttons under the menu: KEYBOARD over the clip view, the
keyboard screen and the automation view, SESSION, and SYNC-SCALING. On `content/fill` (`syncScalingAction` 1,
Fill) SYNC-SCALING holds fill and lights its LED.

`sm29-audition-shortcut` covers the shortcut pads with an audition held inside the sound menu, on a synth and on a kit
row (the edit stays on the row the menu opened for, and so does the kit's selection), and the sequence-direction pad's
note-row editor. It stops on the note-row editor: BACK there sticks on `main` (D92).

`sm19-sample-browser-preview`, `sm20-sample-browser-waveform`, `sm21-sample-browser-buttons` and
`sm22-sample-browser-favourites` cover the sample browser that SHIFT + BROWSE opens from the synth clip: the greyed root
view and its BROWSE blink, the preview and its waveform, the horizontal scroll-in as the selection moves, the keyboard
on demand and KEYBOARD, the X_ENC zoom and the waveform's scroll, LOAD's auto-load, SHIFT + SAVE's delete prompt and
refusal, the audition column, RECORD's recorder, and the favourites row with its 500 ms flash. Windows over a blink are
at least 1 s; the scroll-in is read 100, 300 and 400 ms after the turn in short windows; a popup is read 100 ms after
the press, before it times out.

`sm19`'s audio is checked by hand:
`tools/emu_parity.py --audio-wav tests/ui-scripts/parity/sm19-sample-browser-preview.txt` writes each image's
`audio.wav`/`raw.s32` for the envelope and spectrum comparison (DIFFERENCES.md Coverage); `ui-parity` does not capture
audio.

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
hidden (D71), so a strength is read with the cursor on its hundredths, or at 0. `sm30-cable-column-midi-cv` presses
MIDI and CV with each column focused, before its strength page is open: neither switches the cable's polarity.

`sm31-automation-note-editor` opens the note editor from the automation view (pad selection on, a grid pad held,
SELECT) and drives it there: the held pad's release, a Vertical turn, a grid press, SHIFT + a shortcut pad, a Select
turn and BACK, which returns to the automation overview. It makes no Horizontal turn, on which `main` freezes (E379)
with no note under the held pad.

`p25-menu-knob-presses` presses the gold knobs and the mod buttons with a menu up, where the shell's knob service takes
them, which the other knob scripts never do (they press only over the root views). On the boot synth (the low-pass
frequency's page), a kit with affect-entire (its menu from SELECT) and the song (the song menu), each press is followed
by a turn, so a press that moved a knob shows in the next popup and the knob LEDs: the filter knob's cycle, the mode
steps, a chain's mod FX views and type, the song's compressor entered from its threshold knob, walked round and left,
and a held stutter. Its five kit snapshots that differ from `main` (`entire_menu`, `entire_mod1`,
`entire_mod1_upper_push3`, `entire_mod1_lower_push`, `entire_mod1_lower_push_turn`: the kit's knob LEDs, and a popup's
timing and value) differ the same way between `next` 18998b56c and `main`; with the kit's menu opened from its LPF
shortcut instead, its whole kit half differs there (that page draws a marker on its bar that `main`'s does not).

`p26-learned-cc` learns a MIDI CC from the sound menu (LEARN held, then the CC) onto a param, then plays it at 0, 64,
127 and 64. It learns the boot synth's low-pass frequency, a kit's first row's, the kit's with affect-entire, and the
song's reverb amount; the first three close the menu and read the value off the filter knobs' rings, the song's keeps
its page open, where the new value shows. A fresh CC knob is relative until a value from 16 to 111 shows it is not, so
the 0 moves nothing and the 64 that follows is the first absolute move.

`p21-midi-follow` plays every CC of MIDI follow on a synth clip, a kit row, a kit with affect-entire and the song. The
synth, kit-row and song sections show the sound table and the global table in their popups, knob LEDs and the DIN bytes
of the feedback; the kit affect-entire section shows only error-popup frames, on `main` too, with no feedback, so the
script does not see the affect-entire choice of `midi::followFocus` (`controls::resolve_follow`). That choice is pinned
by `tests/unit/ui/controls/follow_spec.cpp` and `tests/integration/ui/scenes/midi_follow_spec.cpp`. No script sends a pitch bend (the
runner has no bend verb) or unlearns a knob on a sound with a non-zero pitch adjust, so the two accepted learned-MIDI
differences (D164, D165 in `DIFFERENCES.md`) are pinned by specs.

`p27-root-knob-views` pins the gold knobs at the root views, in the order its sections run:
- note tails (`tail_*`, `menu_tail_*`): a long note on the boot synth, envelope-1 sustain set to its minimum from the menu
  (no gold knob changes it), then the release knob (mod button 2, lower) turned across the tail flip, at root and with the
  sound menu up; the pads show the tails going and returning.
- performance view (`perf_*`): mod button 1's knobs with and without a pad held, mod button 6 with a stutter pad held and
  the upper knob pushed; then, in the song view, mod button 0 toggling the VU meter (`vu_*`). The sidebar redraws only
  once the meter is drawn there, so the toggle off (`vu_off`) is the snapshot a missing redraw changes.
- the automation view's overview (`auto_*`): the low-pass frequency painted with automation in its editor, then Learn +
  push copying it from the upper knob and Shift + Learn + push pasting it onto the lower knob (the resonance); the
  pasted automation shows in the overview pads at `auto_paste_lower`. The copy's popup is gone before a snapshot reads
  it.
- a MIDI clip's CC select (`midi_*`): each knob's push held (the popup shows the CC), a gold-knob turn during the hold
  (changes nothing), select-encoder turns during the hold (they change the CC, also with the select push held), the CC
  shown again on a second push, and mod button 3 pressed and released.
- an audio clip (`audio_*`), a kit row and the kit with affect-entire (`kit_row_*`, `kit_entire_*`), the arranger
  (`arr_*`: an output's audition pad held, then the song's knobs and the VU meter) and the keyboard screen over the kit's
  row (`kb_*`): the clips are made in the song view (a held empty clip pad and SELECT, or KIT), and in each root mod
  button 1 and both knobs are turned, each knob pushed, then mod button 0 and both knobs turned. The kit's keyboard
  shows its rows' random colours, so its pads compare as lit or unlit. Two groups differ from `main` exactly as `next`
  a35f6b613 does: mod button 1's lower knob on the new kit, its row's and with affect-entire (`kit_row` to
  `kit_row_lower`, `kit_entire_mod1` to `kit_entire_lower`: the lower ring and the value the turn shows), and mod
  button 0 on the held arranger row (`arr_row_mod0*`: the upper ring and the OLED).
- the layouts the engine changes on its own, on the kit after the keyboard: a row made from a sample through the drum
  creator's browser (`smp_*`: mod button 6, its lower knob the pitch adjust); a MIDI drum and a gate drum (`mdrum_*`,
  `gdrum_*`: auditioned, both knobs turned (the lower knob steps the channel and a MIDI drum's upper knob its note) and
  mod button 2 pressed; with no audition both knobs and mod button 2 move nothing); the clip made a synth and its
  synth mode set to FM and back (`fm_*`, `sub_*`: mod button 1's knobs, the modulators' levels in FM and the filter's
  again after, turned and pushed);
  and the clip made a CV clip (`cv_*`: both knobs and mod button 2 move nothing). The new rows' audition pads show
  random colours. On the menu's second visit the first select turn leaves the value as it was, on `main` too. These
  differ from `main` exactly as `next` ebdbe75de does: the new synth's upper ring and its menu's volume bar (`fm_synth`
  to `fm_back`), and the volume bar on the menu's second visit (`sub_menu`, `sub_first_turn`, `sub_set`); every knob
  snapshot in these sections matches `main`.

`p28-metronome` covers the metronome and a recording's count-in: SHIFT+TAP TEMPO turns the metronome on and off (the
TAP TEMPO LED), the settings menu's RECORDING page sets COUNT-IN to one bar, and RECORD then PLAY show the count-in's
countdown popup before the take. Its settings walk is counted for the host app (`tools/x86_parity.py`), which turns the
encoder by exactly its detents. On delugemu, which drops the first detent of some turns (see `sm00-select-turn`), the
walk reaches COUNT-IN too, so both runners cover the count-in.

The cards' factory presets are stamped `firmwareVersion` 4.1.3 (`tools/stamp_preset_versions.py`). `main` converts a
preset older than official 4.1 as it loads it, and our line leaves that to `tools/deluge-upgrade`, so an unstamped
preset shows the conversion in every comparison that touches it instead of the UI.

## When a change needs an emulator sweep

The host app plays every parity script on the virtual clock (`tools/x86_parity.py`, gate `x86-parity`). A change whose
x86 run against its base differs in nothing keeps the base's parity with `main` for everything the host app can show,
so the base's explained sweep carries over. The emulator sees what the host app cannot: upstream `main` itself, ARM
code generation, the BSP and the device's timing. A change gets one sweep of its final image against `main` (read
against `DIFFERENCES.md`) only when it:

- merges upstream `main` (rebuild the reference image at the merged `main` first);
- touches `src/platform/device/`, the `deluge-sdk` pin in `Cargo.lock`, `src/platform/device/linker/`, the memory
  placement (`tools/placement_roots.toml`), or the device compiler flags and toolchain;
- changes input or display timing: encoder or button polling, UI timers, the display flush;
- shows any difference in x86 against its base that is meant to be there.

Every other change is checked by x86 against its base (zero differences) and its host gates. ARM code generation is
covered by `unit-arm` and `dsp-diff` under qemu-arm.
