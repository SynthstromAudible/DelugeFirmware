# Deluge Community Firmware Change Log

> To find a detailed list of how to use each feature, check here: [Community Features](https://github.com/SynthstromAudible/DelugeFirmware/blob/community/docs/community_features.md)

## c1.3.0

### User Interface

- Added Vibrato and Sidechain patch cables to Automation View Overview and Grid Shortcuts

## c1.2.0 Chopin

### Sound Engine

- Added DX7 compatible synth type with support for importing patches from DX7 patch banks in syx format, as well as editing of patch parameters.
- Added blend control to compressors
- Added ability to record from a specific track's output. Set an audio clips input to TRACK, then in the audio clip menu
use the TRACK menu to select the specific track to record from
  - To hear the instrument through the audio clip's FX set the input to TRACK (FX)
- Added filters in FM synth mode. They're set to OFF by default, enable by changing them to any other mode using the menu or db/oct shortcut.
- Audio clips with monitoring (or FX) active in grid mode now support in place overdub using the global midi loop/layer commands

### User Interface

- Added ability to select audio source from within an Audio Clip by opening the `Audio Clip Sound Menu` (Press `SELECT`) and Selecting the `AUDIO SOURCE` menu
- Added new shortcut to set the length of an audio clip to the same length as its sample at the current tempo. This functionally removes timestretching until the Audio Clip length or Song tempo is changed. 
  - Press `▼︎▲︎` + `◀︎▶︎` to set the Audio Clip length equal to the length of the audio sample.
    - This action is also available in the `Audio Clip Sound Menu` (Press `SELECT`) by Selecting the `ACTIONS` menu and Pressing `SELECT` on the `Set Clip Length to Sample Length` action.
  - Press `SHIFT` + `◀︎▶︎` + `turn ◀︎▶︎` to adjust the audio clip's length independent of timestretching.
- The maximum zoom level for timelines has been increased. Now, the maximum zoom is the point the point where the entire timeline is represented by a single grid cell.
- Added ability to sync LFO2. Where LFO1 syncs relative to the grid, LFO2 syncs relative to individual notes.
- Added `VELOCITY VIEW`, accessible from `AUTOMATION VIEW OVERVIEW` by pressing the `VELOCITY` shortcut, from `AUTOMATION VIEW EDITOR` by pressing `SHIFT OR AUDITION PAD + VELOCITY` or from `INSTRUMENT CLIP VIEW` by pressing `AUDITION PAD + VELOCITY`. 
  - Velocity View enables you to edit the velocities and other parameters of notes in a single note row using a similar interface to `AUTOMATION VIEW`.
- Added `STEM EXPORT`, an automated process for exporting `CLIP STEMS` while in `SONG VIEW` and `INSTRUMENT STEMS` while in `ARRANGER VIEW`. Press `SAVE + RECORD` to start exporting stems. Press `BACK` to cancel stem exporting and stop recording and playback.
  - You can also start the stem export via a new `EXPORT STEMS` menu found in the `SONG` menu accessible in Song and Arranger Views. Start the stem export by entering the `SONG\EXPORT STEMS\` menu and pressing `SELECT` on the menu item titled `START EXPORT`. It will exit out of the menu and display the export progress on the display.
- Fixed a bug where instruments and kits wouldn't respect record arming state. They no longer record when not armed
- A white playhead is now rendered in Song Grid and Performance Views that let's you know when a clip or section launch event is scheduled to occur. The playhead only renders the last 16 notes before a launch event.
  - Note: this playhead can be turned off in the Community Features submenu titled: `Enable Launch Event Playhead (PLAY)`
- The display now shows the number of Bars (or Quarter Notes for the last bar) remaining until a clip or section launch event in all Song views (Grid, Row, Performance).
- For toggle (ON/OFF) menus, you can now view and toggle the ON/OFF status without entering the menu by simply pressing on the `SELECT` encoder while the menu is selected.
 - OLED renders a checkbox that shows current ON/OFF status. Selecting that menu with select encoder will toggle the checkbox as opposed to entering the menu.
 - 7SEG renders a dot at the end of the menu item to show current ON/OFF status. Selecting that menu with select encoder will toggle the dot as opposed to entering the menu.
- Submenus on OLED are rendered with a ">" at the end to indicate that it is a submenu.
- Added ability to `AUTOMATE TEMPO` in arranger view
- Added new mechanism for creating New Clips in New Tracks in `SONG GRID VIEW`.
  - When you press a pad in a new track, a menu will appear asking you to confirm the type of clip you wish to create. By default it will select the last clip type you created as the clip type to create. The clip type selected to be created is shown on the display and is also indicated by the clip type button that is blinking.
    - If you just a tap a pad quickly to create a new clip, it will create that new clip using the last created clip type.
    - If you press and hold a pad, you can choose a different type to create in a number of ways:
      - by turning the select encoder to switch between the various clip types. You can create that clip type by pressing on the select encoder or letting go of the pad.
      - by pressing one of the clip type buttons (e.g. `SYNTH`, `KIT`, `MIDI`, `CV`). 
      - If you let go of the pad without selecting a different type, it will create the clip using the last create type (or the last selected type if you changed selection using select encoder).
    - If you press `BACK` before releasing a pad or selecting a clip type, it will cancel the clip creation.  
    - These changes only apply to `SONG GRID VIEW` and NOT `SONG ROW VIEW`
Updated UI for Converting Empty Instrument Clip to Audio Clip, Setting Clip Mode and Clip Name in Song Grid View
- `HOLDING PAD FOR THE CLIP` + `PRESSING SELECT` in `SONG GRID VIEW` will now open the `CLIP SETTINGS` menu.
  - If you open the menu with with an `INSTRUMENT CLIP` selected, then the menu will give you three options:
    1) `Convert to Audio`: Press select on this option to convert the selected `instrument clip` into an `audio clip`. The menu will exit after converting the clip.
    2) `Clip Mode`: Press select on this option to enter the `Clip Mode` menu so you can change the Clip Mode between `INFINITE`, `FILL` and `ONCE`.
    3) `Clip Name`: Press select on this option to enter the `Clip Name` UI to set the name for the clip.
  - If you open the menu with an `AUDIO CLIP` selected, then the menu will give two options: `Clip Mode` and `Clip Name`.
  - This change only applies to `SONG GRID VIEW` and NOT `SONG ROW VIEW` 
- Updated Fonts and Character Spacing on OLED to provide a more refined and polished user experience.
- Added ability to scroll `KEYBOARD VIEW` horizontally using `<>` while editing Param values in the menu.
- Updated `PERFORMANCE VIEW` UI for exiting out of `EDITING MODE`. While in `EDITING MODE`, you can now press `BACK` to exit out to the previous screen.
- Updated OLED display for `SONG VIEW` and `ARRANGER VIEW` to display the Song Name, Current Tempo and Current Root Note and Scale Name.
- Changed the behaviour of the `PLAY` button while in `ARRANGER VIEW` with `CROSS SCREEN AUTO SCROLL MODE` active. Pressing `PLAY` while playback is off will now start playback from the current scroll position.
- Added community feature toggle `Alternative Playback Start Behaviour (STAR)` to change the behaviour of playback start shortcuts as follows:
  - With playback off, pressing `PLAY` will start playback from the current grid scroll position
  - With playback off, pressing `HORIZONTAL ENCODER ◀︎▶︎` + `PLAY` will start playback from the start of the arrangement or clip
- Added community feature toggle `Accessibility Shortcuts (ACCE)` to make specific shortcut combinations more accessible for users with mobility restrictions.
  - `HORIZONTAL ENCODER ◀︎▶︎` + `PLAY` is changed to `CROSS SCREEN` + `PLAY`
- Added community feature toggle `Grid View Loop Pads (LOOP)` to illuminate two pads (Red and Magenta) in the `GRID VIEW` sidebar for triggering the `LOOP` (Red) and `LAYERING LOOP` (Magenta) global MIDI commands to make it easier for you to loop in `GRID VIEW` without a MIDI controller.
- CLIP NAMES. MIDI, SYNTH & KIT clips can now be named. When entered the clip view, press `SHIFT` + `NAME` and enter the name of the clip. For KIT, its important to activate `AFFECT ENTIRE` to name the KIT clip. When on ARRANGER view, you are now able to scroll through the clip names when holding a pad.
- Pressing drum pads in the `KIT VELOCITY KEYBOARD VIEW` will now update the drum selection so that you can edit the parameters of each drum with gold knobs directly from the kit velocity keyboard view when affect entire is disabled.
  - Note: if you use the `MIDI FOLLOW FEEDBACK` feature, no MIDI Feedback data will be sent when you change drum selections in this view because it will send too much MIDI data and affect the Deluge's performance.
- Fixed bug where you wouldn't enter drum creator to select a drum sample when creating a new kit in the `KIT VELOCITY KEYBOARD VIEW`.
- Fixed a bug where pressing `UNDO` in a `KIT` could cause the `SELECTED DRUM` to change but not update the `GOLD KNOBS` so that they now control that updated kit row.
- Enabled renaming of `MIDI TRACKS` in `ARRANGER VIEW`.
- Added ability to load a sample to a drum in the `KIT VELOCITY KEYBOARD VIEW` by holding a drum pad and pressing `LOAD` or `KIT`.

### Keyboard View Improvements

- New `CHORDS` keyboard layout. The `CHORD` keyboard provides easy building and playing of in-scale chords. The `CHORD` keyboard provides two modes, a `ROW` mode, inspired by the Launchpad Pro, and a `COLUMN` mode, inspired by some of the features in the OXI one and others. 

- New `CHORD LIBRARY` keyboard layout. `CHORD LIBRARY` keyboard is a library of chords split up into columns, where each column belongs to a specific root note. Going up and down the columns will play different chords of the same root note. Voicings can also be changed with pressing a pad and pressing the `◀︎▶︎` encoder and turning it. 

- As the UI and implementation is still experimental, a community setting has to be activated to access the `CHORD`  and `CHORD LIBRARY` keyboards.

### MIDI
- Added Universal SysEx Identity response, including firmware version.
- Allow changing MPE y output to CC1 to support more synths
- Removed MPE zone auto learn as a huge source of midi bugs, MPE must now be configured in the menu

## c1.1.1 Beethoven

### User Interface

- Added `BASS FREQUENCY` and `TREBLE FREQUENCY` parameters to the list of assignable parameters in `PERFORMANCE VIEW`.
- Fixed `PERFORMANCE VIEW` bug where stutter pad could get stuck in active state which should not be possible.
- Fixed a couple bugs around `VU METER` rendering.
- Fixed a `MIDI FOLLOW` bug where the Deluge could crash if sending a note while loading a new song.
- Fixed `KIT CLIP` bug where having a note row without a drum blocked creating a drum in that row
- Fixed `SONG GRID VIEW` bug where `SECTIONS` would playback in the order of `SONG ROW VIEW`
- Fixed an `ARRANGER VIEW` bug where you could not move a `WHITE` clip to `SONG GRID VIEW` without the Deluge freezing.

## c1.1.0 Beethoven

### Sound Engine

- Added an adapted version of the reverb found in Émilie Gillet's Mutable Instruments Rings module. Switch between reverb types within the new `MODEL` sub-menu under `SOUND > FX > REVERB`.
    - This `MUTABLE` reverb model has been set as the default reverb model for new songs. Old songs will respect the reverb
      model used with those songs.
- Added compressors to synths, kits, audio clips, and kit rows. The compressor can be enabled and edited from their
  respective menus.
- Compressor behavior has been changed to reduce clipping. Songs made with community release 1.0.x may need to have their volume manually adjusted to compensate. This is done
  via affect entire in song/arranger mode, or by entering the `SONG MENU` by pressing `SELECT` in Song View and navigating to  `MASTER > VOLUME`
- Fixed Stereo Unison Spread + Ringmod Synth Mode + Noise causing excessively loud output.
- Fixed some bugs around the Waveform Loop Lock feature which allowed setting invalid loop points.
- Fixed a bug in triangle LFO that could cause audible artefacts with some modulation targets, particularly when synced.

### User Interface

- Added `PERFORMANCE VIEW`, accessible in Song Row View by pressing the Keyboard button and in Song Grid View by
  pressing the Pink Mode pad. Allows quick control of Song Global FX.
- Added `AUTOMATION VIEW` for Audio Clips and Arranger View.
- Added `AUTOMATION VIEW` for `PATCH CABLES / MODULATION DEPTH`. Simply enter the modulation menu that displays `SOURCE -> DESTINATION` and then press `CLIP` to access the `AUTOMATION VIEW EDITOR` for that specific Patch Cable / Modulation Depth.
  - You can also use the `SELECT ENCODER` while in the `AUTOMATION VIEW EDITOR` to scroll to any patch cables that exist.
- Updated `AUTOMATION VIEW EDITOR` to allow you to edit Bipolar params according to their Bipolar nature. E.g. positive values are shown in the top four pads, negative value in the bottom four pads, and the middle value (0) is shown by not lighting up any pads.
- Updated `AUTOMATION VIEW` for MIDI Clips to load the Parameter to CC mappings from the `MIDI FOLLOW MODE` preset
  file `MIDIFollow.XML`. These Parameter to CC mappings are used as the quick access MIDI CC shortcuts dislayed in the
  Automation Overview and with the shortcut combos (e.g. `SHIFT` + `SHORTCUT PAD`).
- Updated `AUTOMATION VIEW` to move the `INTERPOLATION` shortcut to the `INTERPOLATION` pad in the first column of the
  Deluge grid (second pad from the top). Toggle interpolation on/off using `SHIFT` + `INTERPOLATION` shortcut pad. The
  Interpolation shortcut pad will blink to indicate that interpolation is enabled.
- Updated `AUTOMATION VIEW` to move the `PAD SELECTION MODE` shortcut to the `WAVEFORM` pad in the first column of the Deluge grid (very top left pad). Toggle pad selection mode on/off using `SHIFT` + `WAVEFORM` shortcut pad. The Waveform shortcut pad will blink to indicate that pad selection mode is enabled.  
- Updated `AUTOMATION VIEW` to provide access to `SETTINGS` menu (`SHIFT` + press `SELECT`)
- Updated `AUTOMATION VIEW` to provide access to the `SOUND` menu (press `SELECT`)
- Updated automatable parameter editing menus (accessed via `SOUND` menu or `SHIFT + PARAMETER SHORTCUT`) to provide the ability to access the `AUTOMATION VIEW EDITOR` directly from the parameter menu. While in the parameter menu press `CLIP` (if you are in a clip) or `SONG` (if you are in arranger) to open the `AUTOMATION VIEW EDITOR` for that respective parameter or patch cable.
- Added configuration of the number of `COUNT-IN BARS` from 1 to 4. Found under `SETTINGS > RECORDING > COUNT-IN BARS`.
- Added Mod Button popups to display the current Mod (Gold) Encoder context (e.g. LPF/HPF Mode, Delay Mode and Type,
  Reverb Room Size, Compressor Mode, ModFX Type and Param).
- Added a menu for Song-level parameters, accessible in `SONG VIEW` and `ARRANGER VIEW` by pressing `SELECT`.
- Added a `KIT FX MENU` to KITS which allows you to customize the AFFECT ENTIRE KIT parameters with greater control.   Accessible in `KIT CLIP VIEW` by pressing `SELECT` with `AFFECT ENTIRE` enabled.
- Added support for LUMI Keys SYSEX protocol. When hosting a LUMI Keys keyboard, the Deluge's current scale will
  automatically be set on the LUMI's keyboard.
- Streamlined recording new clips in `GRID VIEW` while the Deluge is playing. Short-press empty clip pads in Grid View's `GREEN MODE` while recording is armed to automatically create a new clip and queue it to record at the start of the next bar.
- Fixed a bug preventing clip selection while `SHIFT` was held.
- Fixed numerous bugs, including some crash bugs, around the display of `QUANTIZED STUTTER`.
- Fixed a bug with `SHIFT + SCROLL SELECT ENCODER` on small menus which would allow moving off the end of the menu, causing crashes.
- Fixed several bugs with pad grid rendering.
- Added Master Chromatic Transpose of All Scale Mode Instrument Clips from any SONG View (`SONG VIEW`, `ARRANGER VIEW`,
  `ARRANGER AUTOMATION VIEW` and `PERFORMANCE VIEW`). Uses the same shortcut as in a Synth/Midi/CV clip (Press and
  turn `▼︎▲︎` to transpose all clips by +/- 1 semitone). The number of semitones transposed is customizable (Press `SHIFT` and press and turn `▼︎▲︎`). After transposing, the display shows the new Root Note (and Scale Name on OLED displays).
- Added `SIDEBAR CONTROLS` in  `KEYBOARD VIEW` for synths. By default the two sidebar columns pertain to `VELOCITY` (red) and `MOD WHEEL` (blue). Holding a pad sets it momentarily to that value, tapping a pad latches it to that value. The functionality can be changed per column by holding the top pad and turning `▼▲`. Available options are `VELOCITY`, `MOD WHEEL`, `CHORDS`, `CHORD MEMORY`, and `SCALES`.
- Added a `VU METER` toggle that displays the VU Meter on the sidebar in `SONG`, `ARRANGER`, & `PERFORMANCE VIEW`. To activate it, on a song view ensure `AFFECT-ENTIRE` is enabled, then select the `LEVEL/PAN` mod button and press it again to activate the meter.
- Added ability to save a drum kit row back to an synth preset by pressing `AUDITION` + `SAVE`.
- Mod (Gold) Encoders learned to the Mod Matrix can now access the full range of the Mod Matrix / Patch Cable parameters (Values from -50 to +50 where previously only 0 to +50 were accesible via Mod (Gold) Encoders).
- Added feature to automatically load song projects at startup.
    - To activate the feature, press `SHIFT` + `SELECT`: `MENU > DEFAULTS > STARTUP SONG`.
    - Modes are `NEW SONG`,`TEMPLATE`,`LAST OPENED SONG`,`LAST SAVED SONG`.
    - Failsafe mode introduced using canary file to deactivate feature in case of crash at startup.
- Added a feature which saves user-defined pad brightness level and restores it at startup. `SETTINGS>DEFAULTS>PAD BRIGHTNESS`
- Mod (Gold) Encoder LED indicators are now bipolar for bipolar params (e.g. `PAN`, `PITCH`, Patch Cables). Positive values illuminate the top two LEDs. Negative values illuminate the bottom two LEDs. The middle value doesn't light up any LEDs. 
- Added new `High CPU Usage Indicator`. The `PLAY` button button will blink when deluge CPU usage is high which indicates that synth voices / sample playback may be culled.
  - To activate the feature, press `SHIFT` + `SELECT` : `MENU > DEFAULTS > HIGH CPU INDICATOR`.
- Removed ability to convert an Audio Clip to an Instrument Clip (Synth/Kit/MIDI/CV) as this conversion process is currently error/bug prone.
- Restricted changing Synth/MIDI/CV Clips to the Kit Clips and vice versa if the clip is not empty.
- Added retrigger to all keyboard views.
- Added a new default setting that controls which playback mode new slices of a kit will receive. 
- Added indicators when `WAVEFORM LOOP LOCK` is enabled. 7SEG displays a rightmost `.` on screen and OLED displays a lock icon.
- Modified waveform marker rendering to improve clarity.
- Created a new menu hierarchies document that documents the Deluge menu structure for OLED and 7SEG and can be used as a reference for navigating the various menu's. See: [Menu Hierarchies](https://github.com/SynthstromAudible/DelugeFirmware/blob/community/docs/menu_hierarchies.md)
- Added MIDI learning of `SONG` and `KIT AFFECT ENTIRE` params.
- Added Synth/MIDI/CV clip configuration of note row play direction. Hold `AUDITION` while entering the `PLAY DIRECTION` menu to set the play direction for the selected note row. While in the note row play direction menu, you can select other note rows to quickly set the play directiom for multiple note rows.
- Added New MIDI Takeover Mode: `RELATIVE` for use with controllers sending relative value changes
- Added new default menu to set the length of time to register a `Hold Press` for use with `Sticky Shift`, `Performance View`, and the `Keyboard Sidebar Layouts.`
  - Set the default Hold Press time by accessing `SETTINGS > DEFAULTS > HOLD PRESS TIME`
- Added ability to save and load MIDI presets. They end up in a new folder named MIDI.  
- Holding back now fully exits the menu
- A new community feature toggle has been added (`Enable KB View Sidebar Menu Exit (EXIT)`) which will enable you to immediately exit the menu using the top left sidebar pad if you are in the `SETTINGS` or `SOUND` menu for `KEYBOARD VIEW`.

In addition, a number of improvements have been made to how the OLED display is used:

- Added parameter names (including mod matrix / patch cable mappings) to Mod (Gold) Encoder popups.
- `ARRANGER VIEW` and `SONG VIEW` now display the name of the current view on the screen.
- The 12TET note name is now displayed along with the MIDI note number.
- Added a new community setting which allows emulating the 7SEG style on the OLED display. When set to `TOGGLE` press `SHIFT`+`LEARN/INPUT`+`AFFECT ENTIRE` to switch to the emulated 7SEG display. 
- Fixed several cases where popups could get stuck open.
- Fixed a number of minor rendering bugs.

### Sequencer

- When changing instrument presets in `SONG VIEW` or `ARRANGER VIEW`, all pertaining clips of that instrument switch as well. Individual clips can still have only their preset changed by doing so in `CLIP VIEW`.
- Added support for 5 and 6 note scales.
- Added 8 new built-in scales: Melodic Minor, Hungarian Minor, Marva (Indian), Arabian, Whole Tone, Blues, Pentatonic
  Minor, Hirajoshi.
- Added `ONCE CLIP` launch mode, which allows a clip to play just once when launched and then mute itself. Settable in Song's `ROWS VIEW` by holding the `MUTE` pad of a row and then pressing `SELECT`. 
- Added `NOT FILL` note probability. Similar to the `FILL` probability but only plays when the `FILL` button is *not*
  pressed. When the `SYNC-SCALING` button is held, `NOT FILL` notes will be highlighted in red color, as opposed to
  blue for `FILL` notes.
- Added support for copy/paste of single rows.
- Added support for notes to `PASTE GENTLY` which pastes notes without removing old ones.
- Fixed numerous crash bugs around parameter automation when entering and leaving clip view.
- The default Mod-FX type for songs is now DISABLED rather than FLANGER.
- The shorcut `SHIFT` + hold and turn `▼︎▲︎`, inside a clip, has been changed to "Nudge notes vertically" without
  unexpectedly changing the scale and root note of the whole song.
- The arpeggiator has been completely redesigned to have advanced features for the user like independent `OCTAVE MODE` and `NOTE MODE`,
  `RHYTHM`, `SEQUENCE LENGTH` and `RATCHETS` notes. It also enables MPE keyboards to give more expression to the
  arpeggiated notes by updating live the velocity of the notes based on Aftertouch or MPE Y data coming from the keyboard.

### Audio Clips

- The default Mod-FX type is now `DISABLED` rather than `FLANGER` for Audio Clips (this means Mod-FX can be disabled on Audio
  Clips which was not previously possible).

### Kits

- Drum randomization is no longer limited to only 10 sounds per folder.
- Fixed several crashes related to drum randomization.
- The default Mod-FX type for kit affect-entire is now `DISABLED` rather than `FLANGER`.

### MIDI

- Added `MIDI FOLLOW MODE` which causes MIDI data to be directed based on context.
- Added `MIDI FEEDBACK`, so external MIDI controllers can be made aware of the state of the Deluge synth engine.
  Configurable via the new global `MIDI > MIDI-FOLLOW > FEEDBACK` menu.
- Added MIDI learn for kits, allowing a whole kit to be learnt to the same midi channel at once. The incoming note is
  the first row, and increasing notes (chromatically) go to the next rows.
- Added Loopy Pro and TouchOsc templates to use with MIDI follow/MIDI feedback.
- Added a `MIDI LOOPBACK` mode, accessible in the SONG menu, which directs MIDI data from internal MIDI clips back to
  the Deluge input.
- Added support for learning Program Change methods for most global commands.
- Added "MPE collapse" on MIDI clips which converts MPE X/Y/Z to Pitch/Modwheel/Aftertouch CCs for use of MPE
  controllers with non-MPE aware synths. Configurable via the clip menu.
- Added a new global setting, `MIDI > SELECT KIT ROW`, which causes MIDI notes sent to kits to select the corresponding
  learned row.
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
- `currentSong->currentClip` and similar expressions have been refactored in to functions to improve safety and reduce
  code size.
- String formatting is now done with a compact `printf` implementation in many cases.
- All publicly available fixes to the official firmware are integrated (up to commit
  0501b7dc38a363f89112c1b5c36075f4c0368be9)

## c1.0.1 Amadeus

- Fixed a bug where MIDI learned ModFX parameters in an audio clip with monitoring active could crash
- Fixed various crashes related to parameter automation
- Fixed crash when deleting loop points with loop lock enabled
- USB MIDI upstream ports were accidentally reversed, this is now corrected. If you only see 1 MIDI port on OSX, try
  unplugging the Deluge and then deleting the configuration in "MIDI Audio Setup"
- Fixed potential corruption of MIDI learned settings.

## c1.0.0 Amadeus

### Audio Improvements

##### Effects

- A `MASTER COMPRESSOR` has been added and is accessible in Song View.
- `STEREO CHORUS` has been added to `MOD FX TYPES`. Adjust stereo depth via `MOD FX DEPTH`.
- `GRAIN` has been added to `MOD FX TYPES`. Choose from 5 Grain Presets via `MOD FX FEEDBACK`. †
- `WAVEFOLD` distortion has been added and occurs pre-filter. The parameter pad shortcut is between `SATURATION`
  and `LPF CUTOFF`.
- `UNISON STEREO SPREAD` has been added and can be dialed to spread the unison parts across the stereo field.
  Click `SELECT` when in `UNISON AMOUNT` to reveal the parameter.

##### Filter

- New LPF/HPF State Variable Filters: `SVF NOTCH` and `SVF BANDPASS`.
- New Filter Parameters: `LPF MORPH` and `HPF MORPH`. This morphs the SVF through Lowpass, Bandpass, and Highpass; adds
  drive to the low pass ladder filters, and adds filter fm to the hpf ladder.
- `FILTER ROUTING` is accessible via the Sound Editor menu and adjusts the filter order from `HPF to LPF`, `LPF to HPF`,
  or `PARALLEL`.

##### LFO & Sync

- New LFO Shapes: `RANDOM WALK` and `SAMPLE & HOLD`.
- New Sync Modes: `TRIPLETS` and `DOTTED`. (All previous sync rates now include 'TPLTS' and 'DTTED' options.)

### Sequencing Improvements

`AUTOMATION VIEW` allows you to visually create and edit parameter automations across the main grid pads for SYNTH, KIT,
and MIDI clips on a per step basis at any zoom level. (Excludes MPE automations).

##### Probability & Iteration

- `PROBABILITY BY ROW` allows you to set probability for all notes in a given row, expanding from just being able to set
  probability to currently pressed down notes.
- `QUANTIZE` and `HUMANIZE` notes after they've been recorded/sequenced on either a per row basis or for all notes in a
  given clip at once. *
- Sequenced notes can be set to `FILL` which will only play them when the designated `FILL` button is being held (either
  a Global MIDI Command or `SYNC-SCALING†`)

### Keyboard View Improvements

- New `DRUM KEYBOARD VIEW` added. Kit rows can now be visualized and played across the main grid pads. By default the
  area of each sample is spread across 4x4 pads and additionally provides a range of 16 `VELOCITY` levels. This area can
  be adjusted from 1x1 to 8x8 grids.
- New `IN-KEY` keyboard layout. `IN-KEY` will only display notes that are in key of the selected scale across the
  keyboard layout. The original view is named `ISOMORPHIC`. Users can switch freely between the two and choose their
  Default Keyboard Layout in the `DEFAULTS` Menu.
- Adjust `ROOT NOTE` and `SCALE` with new shortcuts, this assists the user when using the `IN-KEY` keyboard layout where
  not every note is visible to set as a `ROOT NOTE`.
- Adjust the offset of `KEYBOARD VIEW` via `ROW STEP` from 1-16. The Deluge's default `ROW STEP` is 5.
- `HIGHLIGHT INCOMING NOTES` will light up incoming MIDI notes across the current `KEYBOARD VIEW` based on their
  velocity. *
- `NORNS LAYOUT` provides compatibility for the Monome Norns using the 'midigrid' script. †

### MIDI Improvements

- Change handling of MPE expression when collapsed to a single MIDI channel. Previously y axis would still be sent as
  CC74 on single midi channels. This changes it to send CC1 instead, allowing for controllable behavior on more non-MPE
  synths. Future work will make a menu to set this per device.
- Added additional MIDI ports which improves usability of MPE-capable devices via USB by sending MPE to one port and
  non-MPE to the other port.
- MIDI Takeover Mode - Affects how the Deluge handles MIDI input for learned CC controls. Options
  include `JUMP`, `PICKUP`, `SCALE`.
- Fixed bugs in mpe handling so that mpe and MIDI channels can be separated without requiring differentiate inputs

### User Interface Improvements

- `GRID VIEW` is an alternate `SONG VIEW` layout similar to Ableton's 'Session View'. It displays unique clips across
  pad rows and the clip variations across pad columns. Effectively allows you to view and launch 128 clips and
  variations without the need of scrolling to reveal more clips in comparison to `ROW VIEW`'s 8 clips at a time.
- Manual Slicing aka 'Lazy Chopping' is now possible by pressing the `◀︎▶︎` encoder when in the Slice Menu. Allows you
  to create slice points live while listening to the sample playback.
- Any synth preset can now be loaded into a Kit row. Hold the audition pad and press `SYNTH` to browse/load a preset.
- Gold encoders now display a pop-up of their current value when adjusted. The value range displayed is 0-50 for
  non-MIDI parameters and 0-127 for MIDI parameters.
- A `MOD MATRIX` entry has been added to the sound editor menu which shows a list of all currently active modulations of
  a given preset.
- You can change the launch status of a clip from `INFINITE` to `FILL`. When a `FILL` clip is launched it will schedule
  itself to play the fill at such a time that it _finishes_ by the start of the next loop and then mutes itself.
- You can now scroll through parameter values and menus faster by +/- 5 by holding `SHIFT` while turning the `SELECT`
  encoder.
- You can now shift a clip's row color from Song View without having to enter the given clip to do so.
- You can now set the stutter effect to be quantized to `4TH, 8TH, 16TH, 32ND, and 64TH` notes before engaging it. †
- Increased the resolution of modulation between sources and their destinations by including two decimal places to the
  modulation amount. *
- An option to swap the behavior of the `TEMPO` encoder when turned versus pressed & turned. *
- `STICKY SHIFT` - Tapping `SHIFT` will lock `SHIFT` ON unless another button is also pressed during the short press
  duration. Allows for quicker parameter editing. †
- Incoming `MODULATION WHEEL` MIDI data from non-MPE synths now maps to `Y` on the Deluge.
- The metronome's volume now respects the song's volume and will increase or decrease in volume in tandem with
  the `LEVEL`-assigned gold encoder. In addition, a `DEFAULTS` menu entry `METRONOME` enables you to set a value between
  1 and 5 to further adjust the volume of the Metronome.
- An alternative setting when pressing `DELAY`-assigned gold encoders can be enabled. The default
  is `PINGPONG` (`ON/OFF`) and `TYPE` (`DIGITAL/ANALOG`) for the upper and lower gold knobs respectively. The alternate
  mode changes it to  `SYNC TYPE` (`EVEN, TRIPLETS, DOTTED`) and `SYNC RATE` (`OFF, WHOLE, 2ND, 4TH, ETC.`)
  respectively. †
- The default behavior of 'catching'/playing notes when instantly launching/muting clips can now be turned off. This can
  result in less unexpected percussive sounds triggering when instantly switching between clips. *
- Waveform Loop Lock - When a sample has loop start and loop end points set, holding down loop start and tapping loop
  end will lock the loop points together when adjusting their position across the waveform.
- Pressing `AUDITION` + `RANDOM` on a drum kit row will load a random sample from the same folder as the current
  sample. *
- You can now batch delete kit rows which do not contain any notes, freeing kits from unused sounds. *
- Audio waveforms can be shifted in an Audio clip, similar to instrument clips, with the exclusion of wrapping the audio
  around.
- Support for sending and receiving large `SYSEX` messages has been added. This allows 7SEG behavior to be emulated on
  OLED hardware and vice versa. Also allows for loading firmware over USB. As this could be a security risk, it must be
  enabled in community feature settings. †

## Footnotes

`*` - Denotes a feature that is `ENABLED` by default in the `COMMUNITY FEATURES` menu but can be disabled.

`†` - Denotes a feature that is `DISABLED` by default in the `COMMUNITY FEATURES` menu but can be enabled.
