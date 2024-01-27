# Deluge Community Firmware Change Log

Last Updated: 2024-01-27

## In Progress (Not Yet In Nightly Build)

Here you will find a listing of current items being worked on that have not yet made it into the Nightly Build.

### Performance Improvements

- `PERFORMANCE VIEW` pink mode has been added to Song Grid View. It replaces the Keyboard button to enter this view in Song Grid View. 

### Sequencing Improvements

- `AUTOMATION VIEW` has been added for Arranger

### Keyboard View Improvements

- Add multiple functions (velocity, mod, chord, chord memory, ...) to sidebar pads in keyboard views

### User Interface Improvements

- You can now obtain a snapshot of the current Mod (Gold) Encoder settings by pressing on the Mod Encoder buttons (the row of buttons between the Gold Encoders). By pressing the buttons, you will get a pop-up that tells you what the current setting is. E.g. if you're selved the LPF/HPF/EQ button, it will tell you whether the Gold Encoders are currently set to LPF, HPF, or EQ and it will tell you what the current LPF or HPF type is (e.g. Drive, Ladder).

## Nightly Build

### Audio Improvements

##### Effects

- New `MUTABLE INSTRUMENTS REVERB` mode has been added and can be enabled via a new `REVERB MODE` sub-menu which has been added under the existing Reverb menu. 

### Performance Improvements

- `PERFORMANCE VIEW` has been added and is accessible in Song View by pressing the Keyboard button. It allows you to quickly control Song Global FX.

### Sequencing Improvements

- `AUTOMATION VIEW` has been added for Audio Clips

##### Probability & Iteration

- Play `ONCE` clip setting. Similar to Fill clips, you can set a clip to play just once, at the beginning of a loop and then it auto mutes itself.
- `NOT FILL` note probability, similar to Fill notes, but these notes will play only when the Fill button is not pressed, and muted when the Fill button is pressed.

##### Synth Improvements

- New Scales: Added new scales, Melodic Minor, Hungarian Minor, Marva (Indian), Arabian, Whole Tone, Blues, Pentatonic Minor, Hirajoshi

### Keyboard View Improvements

- Improvements to norns grid

### MIDI Improvements

- `MIDI FOLLOW MODE` has been added to allow you to quickly map a MIDI controller to control and play the deluge's instruments and its parameters.
- `MIDI LOOPBACK` has been added and is accessible in Song View through the `SONG MENU` by pressing on the Select Encoder.
- Enable learning global commands from midi PC messages
- MPE improvements
- Control non mpe synths with an MPE midi controller (for Midi clips)

### User Interface Improvements

- Added Parameter Name to Mod (Gold) Encoder popups
- The Song View display now shows "Song View" on the screen
- The Arranger View display now shows "Arranger View" on the screen
- A `SONG MENU` was added and is accessible in Song View and Arranger View by pressing on the Select Encoder. This menu gives you quick access to Song Global FX parameters as well as the new `MIDI LOOPBACK` feature.
- A `KIT AFFECT ENTIRE GLOBAL FX MENU` was added to Kits when affect entire is enabled and is accessible by pressing on the Select Encoder. This menu gives you quick access to Kit Global FX parameters.
- Copy/paste single rows
- Unlimited Drum Randomizer
- Show human note name instead of integer note number
- Add Lumi Keys sysex message support 
- Emulated display (community setting). You can emulate the 7SEG display in the OLED display
- Sysex Firmware Loading over USB

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
