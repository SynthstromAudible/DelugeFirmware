# Deluge Community Firmware Change Log

## c1.1.0 Beethoven

> To find a detailed list of how to use each feature, check here: [Community Features](https://github.com/SynthstromAudible/DelugeFirmware/blob/community/docs/community_features.md)

### Sound Engine

- Added an adapted version of the reverb found in Émilie Gillet's Mutable Instruments Rings module. Can be enabled via a new `REVERB MODEL` sub-menu under the existing Reverb menu. 
    - The Mutable reverb model has been set as the default reverb model for new songs. Old songs will respect the reverb model used with those songs.
- Fixed some bugs around the waveform Loop Lock feature which allowed setting invalid loop points.

### User Interface

- Added `PERFORMANCE VIEW`, accessible in Song Row View by pressing the Keyboard button and in Song Grid View by pressing the Pink Mode pad. Allows quick control of Song Global FX.
- Added `AUTOMATION VIEW` for Audio Clips and Arranger View.
- Updated `AUTOMATION VIEW` for MIDI Clips to load the Parameter to CC mappings from the `MIDI FOLLOW MODE` preset file `MIDIFollow.XML`. These Parameter to CC mappings are used as the quick access MIDI CC shortcuts dislayed in the Automation Overview and with the shortcut combos (e.g. Shift + Shortcut Pad).
- Added Mod Button popups to display the current Mod (Gold) Encoder context (e.g. LPF/HPF Mode, Delay Mode and Type, Reverb Room Size, Compressor Mode, ModFX Type and Param).
- Added a menu for song parameters, accessible in Song View and Arranger View by pressing on the Select Encoder.
- Added a `AFFECT ENTIRE GLOBAL FX MENU` to Kits, accessible in Kit Clip View by pressing on the Select Encoder with Affect Entire enabled.
- Added support for LUMI Keys SYSEX protocol protocol. When hosting a LUMI Keys keyboard, the current scale will automatically be set on the keyboard.
- Short-press of empty clip pads in Grid mode while REC is enabled automatically creates a new clip and queues it for recording at the start of the next bar.
- Fixed a bug preventing clip selection while Shift was held.
- Fixed numerous bugs, including some crash bugs, around the display of quantized stutter.
- Fixed a bug with shift+scroll on small menus which would allow moving off the end of the menu, causing crashes.
- Fixed several bugs with pad grid rendering.

In addition, a number of improvements have been made to how the OLED display is used:

- Added parameter name to Mod (Gold) Encoder popups.
- `ARRANGER VIEW` and `SONG VIEW` now display the name of the current view on the screen.
- The 12TET note name is now displayed along with the MIDI note number.
- Added a new community setting which allows emulating the old 7SEG style on the OLED display.
- Fixed several cases where popups could get stuck open.
- Fixed a number of minor rendering bugs.

### Sequencer

- When changing instrument presets in session or arranger view all presets are now changed. Individual clips can still have their presets changed by first entering the clip.
- Added support for 5 and 6 note scales.
- Added 8 new built-in scales: Melodic Minor, Hungarian Minor, Marva (Indian), Arabian, Whole Tone, Blues, Pentatonic Minor, Hirajoshi.
- Added "play `ONCE`" clip launch mode, settable by turning the Select Encoder left while holding the audition pad in Song view. Causes the clip to play once when triggered and then mute without changing the section.
- Added `NOT FILL` note probability. Similar to the `FILL` probability but only plays when the `FILL` button is *not* pressed.
- Added support for copy/paste of single rows.
- Added support for "gentle paste" of notes which pastes notes without removing old ones.
- Fixed numerous crash bugs around parameter automation when entering and leaving clip view.
- The default ModFX type for songs is now DISABLED rather than FLANGER.

### Audio Clips

- The default ModFX type is now DISABLED rather than FLANGER for Audio Clips (this means ModFX can be disabled on Audio Clips which was not previously possible).

### Kits

- Drum randomization is no longer limited to only 10 sounds per folder.
- Fixed several crashes related to drum randomization.
- The default ModFX type for kit affect-entire is now DISABLED rather than FLANGER.

### MIDI

- Added `MIDI FOLLOW MODE` which causes MIDI data to be directed based on context.
- Added `MIDI FEEDBACK`, so external MIDI controllers can be made aware of the state of the Deluge synth engine. Configurable via the new global `MIDI > MIDI-FOLLOW > FEEDBACK` menu.
- Added Loopy Pro and TouchOsc templates for use with MIDI follow/MIDI feedback.
- Added a `MIDI LOOPBACK` mode, accessible in the SONG menu, which directs MIDI data from internal MIDI clips back to the Deluge input.
- Added support for learning Program Change methods to most global commands.
- Added "MPE collapse" on MIDI clips which converts MPE X/Y/Z to Pitch/Modwheel/Aftertouch CCs for use of MPE controllers with non-MPE aware synths. Configurable via the clip menu.
- Added a new global setting, `MIDI > SELECT KIT ROW`, which causes MIDI notes sent to kits to select the corresponding learned row.
- Fixed reversal of upstream MIDI ports (port 1 was upstream port 2, port 2 was upstream port 1).
- Fixed a number of MPE channel matching and learning bugs.
- Fixed crash when using an external controller to control ModFX.
- Fixed CC74 on the MPE master channel behaving like an expression event.

### Internal Changes
- The Deluge Build Tool toolchain has been updated from GCC 12 to GCC 13.
- All code is now built in THUMB mode by default, except DSP code which is still ARM.
- Support for parallel toolchains has been added so re-downloading is not required while moving between branches.
- Numerous improvements to the VSCode configuration, including auto-format on save.
- Various types have been renamed and refactored to improve legibility.
- A number of on/off UI toggles have been collapsed in to the new `ToggleBool` class
- `currentSong->currentClip` and similar expressions have been refactored in to functions to improve safety and reduce code size.
- String formatting is now done with a compact `printf` implementation in many cases.
- All publicly available fixes to the official firmware are integrated (up to commit 0501b7dc38a363f89112c1b5c36075f4c0368be9)

## c1.0.1 Amadeus

- Fixed a bug where MIDI learned ModFX parameters in an audio clip with monitoring active could crash
- Fixed various crashes related to parameter automation
- Fixed crash when deleting loop points with loop lock enabled
- USB MIDI upstream ports were accidentally reversed, this is now corrected. If you only see 1 MIDI port on OSX, try unplugging the Deluge and then deleting the configuration in "MIDI Audio Setup"
- Fixed potential corruption of MIDI learned settings.

## c1.0.0 Amadeus

### Audio Improvements

##### Effects

- A `MASTER COMPRESSOR` has been added and is accessible in Song View.
- `STEREO CHORUS` has been added to `MOD FX TYPES`. Adjust stereo depth via `MOD FX DEPTH`.
- `GRAIN` has been added to `MOD FX TYPES`. Choose from 5 Grain Presets via `MOD FX FEEDBACK`. †
- `WAVEFOLD` distortion has been added and occurs pre-filter. The parameter pad shortcut is between `SATURATION` and `LPF CUTOFF`.
- `UNISON STEREO SPREAD` has been added and can be dialed to spread the unison parts across the stereo field. Click `SELECT` when in `UNISON AMOUNT` to reveal the parameter.

##### Filter

- New LPF/HPF State Variable Filters: `SVF NOTCH` and `SVF BANDPASS`.
- New Filter Parameters: `LPF MORPH` and `HPF MORPH`. This morphs the SVF through Lowpass, Bandpass, and Highpass; adds drive to the low pass ladder filters, and adds filter fm to the hpf ladder.
- `FILTER ROUTING` is accessible via the Sound Editor menu and adjusts the filter order from `HPF to LPF`, `LPF to HPF`, or `PARALLEL`.

##### LFO & Sync

- New LFO Shapes: `RANDOM WALK` and `SAMPLE & HOLD`.
- New Sync Modes: `TRIPLETS` and `DOTTED`. (All previous sync rates now include 'TPLTS' and 'DTTED' options.)

### Sequencing Improvements

`AUTOMATION VIEW` allows you to visually create and edit parameter automations across the main grid pads for SYNTH, KIT, and MIDI clips on a per step basis at any zoom level. (Excludes MPE automations).

##### Probability & Iteration

- `PROBABILITY BY ROW` allows you to set probability for all notes in a given row, expanding from just being able to set probability to currently pressed down notes.
- `QUANTIZE` and `HUMANIZE` notes after they've been recorded/sequenced on either a per row basis or for all notes in a given clip at once. *
- Sequenced notes can be set to `FILL` which will only play them when the designated `FILL` button is being held (either a Global MIDI Command or `SYNC-SCALING†`)

### Keyboard View Improvements

- New `DRUM KEYBOARD VIEW` added. Kit rows can now be visualized and played across the main grid pads. By default the area of each sample is spread across 4x4 pads and additionally provides a range of 16 `VELOCITY` levels. This area can be adjusted from 1x1 to 8x8 grids. 
- New `IN-KEY` keyboard layout. `IN-KEY` will only display notes that are in key of the selected scale across the keyboard layout. The original view is named `ISOMORPHIC`. Users can switch freely between the two and choose their Default Keyboard Layout in the `DEFAULTS` Menu.
- Adjust `ROOT NOTE` and `SCALE` with new shortcuts, this assists the user when using the `IN-KEY` keyboard layout where not every note is visible to set as a `ROOT NOTE`.
- Adjust the offset of `KEYBOARD VIEW` via `ROW STEP` from 1-16. The Deluge's default `ROW STEP` is 5.
- `HIGHLIGHT INCOMING NOTES` will light up incoming MIDI notes across the current `KEYBOARD VIEW` based on their velocity. *
- `NORNS LAYOUT` provides compatibility for the Monome Norns using the 'midigrid' script. †

### MIDI Improvements

- Change handling of MPE expression when collapsed to a single MIDI channel. Previously y axis would still be sent as CC74 on single midi channels. This changes it to send CC1 instead, allowing for controllable behavior on more non-MPE synths. Future work will make a menu to set this per device. 
- Added additional MIDI ports which improves usability of MPE-capable devices via USB by sending MPE to one port and non-MPE to the other port.
- MIDI Takeover Mode - Affects how the Deluge handles MIDI input for learned CC controls. Options include `JUMP`, `PICKUP`, `SCALE`.
- Fixed bugs in mpe handling so that mpe and MIDI channels can be separated without requiring differentiate inputs 

### User Interface Improvements

- `GRID VIEW` is an alternate `SONG VIEW` layout similar to Ableton's 'Session View'. It displays unique clips across pad rows and the clip variations across pad columns. Effectively allows you to view and launch 128 clips and variations without the need of scrolling to reveal more clips in comparison to `ROW VIEW`'s 8 clips at a time.
- Manual Slicing aka 'Lazy Chopping' is now possible by pressing the `◀︎▶︎` encoder when in the Slice Menu. Allows you to create slice points live while listening to the sample playback.
- Any synth preset can now be loaded into a Kit row. Hold the audition pad and press `SYNTH` to browse/load a preset.
- Gold encoders now display a pop-up of their current value when adjusted. The value range displayed is 0-50 for non-MIDI parameters and 0-127 for MIDI parameters.
- A `MOD MATRIX` entry has been added to the sound editor menu which shows a list of all currently active modulations of a given preset.
- You can change the launch status of a clip from `DEFAULT` to `FILL`. When a `FILL` clip is launched it will schedule itself to play the fill at such a time that it _finishes_ by the start of the next loop and then mutes itself.
- You can now scroll through parameter values and menus faster by +/- 5 by holding `SHIFT` while turning the `SELECT` encoder.
- You can now shift a clip's row color from Song View without having to enter the given clip to do so.
- You can now set the stutter effect to be quantized to `4TH, 8TH, 16TH, 32ND, and 64TH` notes before engaging it. †
- Increased the resolution of modulation between sources and their destinations by including two decimal places to the modulation amount. *
- An option to swap the behavior of the `TEMPO` encoder when turned versus pressed & turned. *
- `STICKY SHIFT` - Tapping `SHIFT` will lock `SHIFT` ON unless another button is also pressed during the short press duration. Allows for quicker parameter editing. †
- Incoming `MODULATION WHEEL` MIDI data from non-MPE synths now maps to `Y` on the Deluge.
- The metronome's volume now respects the song's volume and will increase or decrease in volume in tandem with the `LEVEL`-assigned gold encoder. In addition, a `DEFAULTS` menu entry `METRONOME` enables you to set a value between 1 and 5 to further adjust the volume of the Metronome.
- An alternative setting when pressing `DELAY`-assigned gold encoders can be enabled. The default is `PINGPONG` (`ON/OFF`) and `TYPE` (`DIGITAL/ANALOG`) for the upper and lower gold knobs respectively. The alternate mode changes it to  `SYNC TYPE` (`EVEN, TRIPLETS, DOTTED`) and `SYNC RATE` (`OFF, WHOLE, 2ND, 4TH, ETC.`) respectively. †
- The default behavior of 'catching'/playing notes when instantly launching/muting clips can now be turned off. This can result in less unexpected percussive sounds triggering when instantly switching between clips. *
- Waveform Loop Lock - When a sample has loop start and loop end points set, holding down loop start and tapping loop end will lock the loop points together when adjusting their position across the waveform.
- Pressing `AUDITION` + `RANDOM` on a drum kit row will load a random sample from the same folder as the current sample. *
- You can now batch delete kit rows which do not contain any notes, freeing kits from unused sounds. *
- Audio waveforms can be shifted in an Audio clip, similar to instrument clips, with the exclusion of wrapping the audio around.
- Support for sending and receiving large `SYSEX` messages has been added. This allows 7SEG behavior to be emulated on OLED hardware and vice versa. Also allows for loading firmware over USB. As this could be a security risk, it must be enabled in community feature settings. †

## Footnotes

`*` - Denotes a feature that is `ENABLED` by default in the `COMMUNITY FEATURES` menu but can be disabled.

`†` - Denotes a feature that is `DISABLED` by default in the `COMMUNITY FEATURES` menu but can be enabled.
