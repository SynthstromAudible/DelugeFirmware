# Community Features
## 1. Introduction

Every time a Pull Request improves the community firmware it shall be noted down what it accomplishes and how it is used.

Reference the 'Community Features Menu' section at the end of this document to understand what each entry is and their 7SEG abbreviations.

## 2. File Compatibility Warning
In general, we try to maintain file compatibility with the official firmware. However, **files (including songs, presets, etc.) that use community features may not ever load correctly on the official firmware again**. Make sure to back up your SD card!

## 3. General Improvements

Here is a list of general improvements that have been made, ordered from newest to oldest:

#### 3.1 - Patch Cable Modulation Resolution

- ([#17]) Increase the resolution of "patch cables" between mod sources and destinations. This adds two decimal points when modulating parameters.
  	- This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 3.2 - MPE
- ([#29]) Bugfix to respect MPE zones in kit rows. In the official firmware, kit rows with MIDI learned to a channel would be triggered by an MPE zone which uses that channel. With this change they respect zones in the same way as synth and MIDI clips. ([#512]) adds further fixes related to channels 0 and 15 always getting received as MPE.

- ([#512]) Change handling of MPE expression when collapsed to a single MIDI channel. Previously Y axis would still be sent as CC74 on single MIDI channels. This changes it to send CC1 instead, allowing for controllable behaviour on more non-MPE synths. Future work will make a menu to set this per device. 

#### 3.3 - MIDI
- ([#47]) Extra MIDI ports on the USB interface for MPE. Port 2 shows in the MIDI device menu, and improves the usability of MPE-capable devices through the USB interface by allowing MPE zones to be sent to port 2 and non-MPE to be sent to port 1 (or vice versa). A third port is added for future use such as a desktop/mobile companion app, DAW control or Mackie HUI emulation. When USB for MIDI is plugged into the Deluge, you can browse these settings in `SETTINGS > MIDI > DEVICES > UPSTREAM USB PORT 1` or `UPSTREAM USB PORT 2`.
- (#147) Allows CCs to be learnt to the global commands (play, stop, loop, fill, etc.)
- ([#781]) Master MIDI Follow Mode whereby after setting a master MIDI follow channel for Synth/MIDI/CV clips, Kit clips, and for Parameters, all MIDI (notes + cc’s) received on the channel relevant for the active context will be directed to control the active view (e.g. arranger view, song view, audio clip view, instrument clip view). 
	- Comes with a MIDI feedback mode to send updated parameter values on the MIDI follow channel for learned MIDI cc's. Feedback is sent whenever you change context on the deluge and whenever parameter values for the active context are changed.
	- Settings related to MIDI Follow Mode can be found in `SETTINGS > MIDI > MIDI-FOLLOW`. 

#### 3.4 - Tempo
- ([#178]) New option (`FINE TEMPO` in the `COMMUNITY FEATURES` menu). Inverts the push+turn behavior of the `TEMPO` encoder. With this option enabled the tempo changes by 1 when unpushed and ~4 when pushed (vs ~4 unpushed and 1 pushed in the official firmware).
	- This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 3.5 - Kits

- ([#395]) Load synth presets into kit rows by holding the row's `AUDITION` + `SYNTH`. Saving kit rows to synth presets is not yet implemented.

#### 3.6 - Global Interface

- ([#118]) Sticky Shift - When enabled, tapping `SHIFT` will lock shift `ON` unless another button is also pressed during the short press duration.
 	- This feature is `OFF` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.
- ([#118]) Shift LED feedback can now be toggled manually, however it will turn ON or OFF in conjunction with Sticky Shift (adjust SHIFT LED FEEDBACK afterwards).
	- This feature is `OFF` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 3.7 - Mod Wheel
- ([#512]) Incoming mod wheel MIDI data from non-MPE devices now maps to the `Y` axis.

#### 3.9 - Enable Stutter Automation
- ([#653]) Enabled ability to record stutter automation with mod (gold) encoder.
  	- This feature is not present in the v1.0.0 release.

#### 3.9 - Enable Stutter Automation
- ([#653]) Enabled ability to record stutter automation with mod (gold) encoder.

#### 3.8 - Visual Feedback on Value Changes with Mod Encoders and Increased Resolution for Value's in Menu's
- ([#636]) Changing parameter values with Mod (Gold) Encoders now displays a pop-up with the current value of the parameter. The `SOUND` and `MODULATION` screens when parameter and modulation editing have also been adjusted to show the same value range as displayed with the Mod Encoders.
	- This allows for better fine-tuning of values. 
	- The value range displayed is 0-50 for non-MIDI parameters and 0-127 for MIDI parameters.
	- Note: In the Menu, if you wish to scroll through the parameter value range faster at an accelerated rate of +/- 5, hold `SHIFT` while turning the Select Encoder.

#### 3.9 - Adjust Metronome Volume
- ([#683]) The Metronome's volume now respects the song's volume and will increase and decrease in volume together with the Gold Volume Encoder.
	- In addition, a `DEFAULTS` menu entry was created titled `METRONOME` which enables you to set a value between 1-50 to further adjust the volume of the Metronome. 1 being the lowest metronome volume that can be heard when the Song's volume is at its maximum and 50 being the loudest metronome volume.

## 4. New Features Added

Here is a list of features that have been added to the firmware as a list, grouped by category:

### 4.1 - Song View Features

#### 4.1.1 - Master Compressor
- ([#630]) In Song view, select `AFFECT ENTIRE` and the `SIDECHAIN`-related parameter button. Adjust the `UPPER` gold encoder for a single knob compressor with auto makeup gain (`ONE` mode). For detailed editing, press the `SIDECHAIN`-related gold encoder (`FULL` mode). The top LED will become a compression meter.  Clicking the `REVERB`-related lower gold encoder will cycle through additional params: `RATIO` (displays ratio), `ATTACK` & `RELEASE` (shown in milliseconds) and Sidechain `HPF` (shown in Hz). The sidechain HPF is useful to remove some bass from the compressor level detection, which sounds like an increase in bass allowed through the compression.
 
	- `ATTACK`: 0ms - 63ms

	- `RELEASE`: 50ms - 363ms

	- `HPF SHELF`: 0hz - 102hz

	- `RATIO`: 2:1 - 256:1

	- `THRESHOLD`: 0 - 50


#### 4.1.2 - Change Row Colour

 - ([#163]) In Song View, pressing a clip row pad + `SHIFT` + turning `▼︎▲︎` changes the selected row color. This is similar to the shortcut when setting the color while in a clip view.

#### 4.1.3 - Fill Clips

 - ([#196]) Holding the status pad (mute pad) for a clip and pressing `SELECT` brings up a clip type selection menu. The options are:
    - Default (DEFA) - the default Deluge clip type.
	- Fill (FILL) - Fill clip. It appears orange/cyan on the status pads, and when triggered it will schedule itself to start at such a time that it _finishes_ at the start of the next loop. If the fill clip is longer than the remaining time, it is triggered immediately at a point midway through. The loop length is set by the longest playing clip, or by the total length of a section times the repeat count set for that section. **Limitation**: a fill clip is still subject to the one clip per instrument behavior of the Deluge. Fill clips can steal an output from another fill, but they cannot steal from a non-fill. This can lead to some fills never starting since a default type clip has the needed instrument. This can be worked around by cloning the instrument to an independent copy.

#### 4.1.4 - Catch Notes

 - ([#221]) The normal behavior of the Deluge is to try to keep up with 'in progress' notes when instant switching between clips by playing them late. However this leads to glitches with drum clips and other percussive sounds. Changing this setting to OFF will prevent this behavior and *not* try to keep up with those notes, leading to smoother instant switching between clips.
	- This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 4.1.5 - Grid View

 - ([#251]) Add new grid session layout to "SONG" mode. All functionality from (classic) row layout applies except for the following:
	 - The data model of rows and grid mode are compatible, you can switch between them freely
	 - In grid mode you will not be able to see multiple clips that are in the same section, only the first one. To make them visible move the clips to other sections
	 - The colored coloumn on the right are all available sections, the columns are automatically filled with the tracks in the same order as in arrangement mode
	 - In session mode hold "SONG" and turn "SELECT" knob to switch between row layout and grid layout
	 - Compared to rows layout overdub recording and copying clips to arranger is currently not supported
	 - Every track (column) has a random generated color that can be changed in edit mode (see below)
	 - Launched clips are full color, unlaunched dimmed and during soloing all non soloed clips are greyed out
	 - New default settings that can be reached pressing both `SHIFT` + `SELECT`: `MENU > DEFAULTS > UI > SONG`
		- Layout: Select the default layout for all new songs
		- Grid
			- Default active mode: "Selection" allows changing the mode as described below, all other settings will always make mode snap back to the configured one (default Selection)
			- Select in green mode: Enabling this will make allow holding clips in green (launch) mode to change their parameters like in blue mode, tradeoff is arming is executed on finger up (default on)
			- Empty pad unarm: Enabling will make pressing empty pads in a track unarm all playing tracks in that track (default off)
	 - There are different interaction modes that change how the grid behaves
		- The mode can be changed by clicking on one of the colored pads in the Audition/Section column on the right
		- To permanently switch the mode click on a pad and release, to temporarily switch hold the mode pad and use the grid, the mode will snap back to the current permanent one
		- Green mode
		    - All main pads behave the same as the Mute/Launch pads in rows layout (arm/immediate launch/mute/record/MIDI learn arm)
			- Section pads (left sidebar column) behave the same as in rows layout, in addition Shift+Section will immediate launch all clips in that row
		- Blue mode
			- All main pads behave the same as the main pads in rows layout (open/select/create/delete/MIDI learn)
			- While holding a clip it can be copied to other empty slots by clicking on them, apart from audio/instrument conversion clips are automatically moved to that instrument/track and converted (e.g. Synth to MIDI target)
			- Track color can be changed by holding any populated clip in a column and rotating `▼︎▲︎`. For fine changes to the color press `▼︎▲︎` while turning it.
			- Section pads (left sidebar column) will allow changing repeat count while held

#### 4.1.6 - New Performance View
- ([#711]) Adds a new performance view, accessible from Song and Arranger View's using the Keyboard button.
	- Each column on the grid represents a different "FX" and each pad/row in the column corresponds to a different FX value.
	- Specifications:
		- Perform FX menu to edit Song level parameters and enable/disable editing mode.
		- 16 FX columns
			- 1 Param assigned to each column
		- 8 FX values per column
			- Long press pads in a column to change value momentarily and reset it (to the value before the pad was pressed) upon pad release
			- Short press pads in a column to the change value until you press the pad again (resetting it to the value before the pad was pressed)
		- Editing mode to edit the FX values assigned to each pad and the parameter assigned to each FX column
		- Save defaults as PerformanceView.xml file
			- Adjustable default Values assigned to each FX column via "Value" editing mode or PerformanceView.xml
			- Adjustable default Param assigned to each FX column via "Param" editing mode or PerformanceView.xml
			- Adjustable default "held pad" settings for each FX column via Performance View or PerformanceView.xml (simply change a held pad in Performance View and save the layout to save the layout with the held pads).
		- Load defaults from PerformanceView.xml file

### 4.2 - Clip View - General Features (Instrument and Audio Clips)

#### 4.2.1 - Filters
 - ([#103] and [#336]) Adds 2 new state variable filters to both `LPF dB/OCT` and `HPF dB/OCT`. This filter has significantly less distortion than the ladder filters, think sequential vs. moog.
 - The `MORPH` parameter (pads under both `LPF dB/OCT` and `HPF dB/OCT`) adjusts smoothly from lowpass -> bandpass/notch -> highpass. `MORPH` is inverted in the `HPF MORPH` slot so that at 0 the filter is highpass and at 50 it is lowpassed. 
 	- The `MORPH` param is also effective to the ladder filters. In `12 LADDER/24 LADDER/DRIVE` filters it smoothly increases drive and in the `HP LADDER` it adds filter FM. This param is modulatable and automatable.
	- `FILTER ROUTE` is accessible via the `SOUND` menu only and adjusts the filter order from `HPF TO LPF`, `LPF TO HPF`, or `PARALLEL`.

#### 4.2.2 - Stereo Chorus
- ([#120]) New Stereo Chorus type added to Mod FX. `MOD FX DEPTH` will adjust the amount of stereo widening the effect has.

#### 4.2.3 - MIDI Takeover Mode

This mode affects how the Deluge handles MIDI input for learned CC controls.

 - ([#170]) A new `TAKEOVER` submenu was created in the `MIDI` settings menu which consists of three modes that can be selected from:

	**1. `JUMP`:** This is the default mode for the Deluge. As soon as a MIDI encoder/Fader position is changed, the Deluge's internal encoder position/Parameter value jumps to the position of the MIDI encoder/Fader.

	**2. `PICKUP`:** The Deluge will ignore changes to its internal encoder position/Parameter value until the MIDI encoder/Fader's position is equal to the Deluge encoder position. After which the MIDI encoder/Fader will move in sync with the Deluge.

	**3. `SCALE`:** The Deluge will increase/decrease its internal encoder position/Parameter value relative to the change of the MIDI encoder/Fader position and the amount of "runway" remaining on the MIDI controller. Once the MIDI controller reaches its maximum or minimum position, the MIDI encoder/Fader will move in sync with the Deluge. The Deluge value will always decrease/increase in the same direction as the MIDI controller.

#### 4.2.4 - Alternative Delay Types for Param Encoders (Gold encoders)
- ([#282]) Ability to select in `COMMUNITY FEATURES` menu, which parameters are controlled when you click the `DELAY`-related golden encoders. The default (for upper and lower encoders) is `PINGPONG` (`ON/OFF`) and `TYPE` (`DIGITAL`/`ANALOG`), and you can modify it so the encoder clicks change the `SYNC TYPE` (`EVEN, TRIPLETS, DOTTED`) and `SYNC RATE` (`OFF, WHOLE, 2ND, 4TH, ETC`) respectively.

	- This feature is `OFF` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 4.2.5 - Patchable Wavefolding Distortion

- ([#349]) Adds a pre filter `WAVEFOLDE` distortion, and the depth is patchable/automatable. The depth is accessible in both the menu and on the un-labeled pad between `SATURATION` and `LPF FREQ`. The fold has no effect when set to 0 and removes itself from the signal path.
	- Note that it has no effect on square waves, it's best with sines and triangles

#### 4.2.6 - Quantized Stutter

- ([#357]) Set the stutter effect to be quantized to `4TH, 8TH, 16TH, 32ND, and 64TH` rate before triggering it. Once you have pressed the `STUTTER`-related gold encoder, then the selected value will be the center value of the encoder and you can go up and down with the golden encoder and come back to the original rate by centering the encoder (LEDs will flash indicating it).

	- This feature is `OFF` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 4.2.7 - Grain FX

- ([#363]) New `GRAIN` added to Mod FX. This effect is resource-intensive, so it's suggested to use only one instance per song and/or resample and remove the clip or effect afterwards. As such it is turned `OFF` by default for now.

	- Parameters:
 		- **`MOD RATE`:** Sets Grain Rate (0.5hz - 180hz)
		- **`MOD DEPTH`:** Controls Grain Volume / Dry Wet Mix
   		- **`MOD FEEDBACK`:** Selects Grain Type (See below for values)
		- **`MOD OFFSET`:** Adjusts Grain Size (10ms - 800ms)
     
	- Grain Type (Presets):
		- **`0-10`:** Unison and +1 Octave (Reversed)
		- **`11-20`** Unison and -1 Octave
		- **`21-30`:** Unison and +1 Octave (Defalut)
		- **`31-40`:** 5th and +1 Octave
		- **`41-50`:** Unison and +1/-1 Octave (Tempo Sync)
    
	- This feature is `OFF` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

### 4.3 - Instrument Clip View - General Features

These features were added to the Instrument Clip View and affect Synth, Kit and MIDI instrument clip types.

#### 4.3.1 - New LFO Shapes
LFO types added to the "LFO SHAPE" shortcut.

 - ([#32]) **`RANDOM WALK`:** Starts at zero and walks up or down by small increments when triggered.
 - ([#32]) **`SAMPLE&HOLD`:** Picks a new random value every time it is triggered.

#### 4.3.2 - New LFO Synchronization Modes
Synchronization modes accessible through `SYNC` shortcuts for `ARP`, `LFO1`, `DELAY`.

 - ([#32]) **`TPLTS`:** Synchronizes the LFO to triplet (3/2) divisions.
 - ([#32]) **`DTTED`:** Synchronizes the LFO to dotted (2/3) divisions.

#### 4.3.3 - Quantize & Humanize
 - ([#129])
	- Press and hold an audition pad in clip view and turn the tempo encoder right or left to apply `QUANTIZE` or `HUMANIZE` respectively to that row.
	- Press and hold an audition pad and press and turn the tempo encoder to apply quantize or humanize to all rows.
	- The amount of quantization/humanization is shown in the display by 10% increments: 0%-100%
	- This is destructive (your original note positions are not saved) The implementation of this feature is likely to change in the future
	- This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 4.3.4 - Fill Mode
 - ([#211]) Fill Mode is a new iteration/probability setting for notes. The `FILL` setting is at the start of the probability range, right before `5%`. Notes set to `FILL` are only played when fill mode is active. There are two ways to activate `FILL` mode - set it as a Global MIDI Command and/or set it to override the front panel `SYNC-SCALING` button. For Global MIDI Commands go to `SETTINGS > MIDI > CMD > FILL`. To override to `SYNC-SCALING`, set `SETTINGS > COMMUNITY FEATURES > SYNC` to `FILL`. The orignal `SYNC-SCALING` function is moved to `SHIFT` + `SYNC-SCALING`.

#### 4.3.5 - Automation View
 - For a detailed description of this feature as well the button shortcuts/combos, please refer to the feature documentation: [Automation View Documentation]
 - ([#241]) Automation Instrument Clip View is a new view that complements the existing Instrument Clip View.
	- It is accessed from within the Clip View by pressing  `CLIP` (which will blink to indicate you are in the Automation View).
	- You can edit Non-MPE Parameter Automation for Synth, Kit and MIDI instrument clips on a per step basis at any zoom level.
	- A `COMMUNITY FEATURES` sub-menu titled `AUTOMATION` was created to access a number of configurable settings for changes to existing behaviour.
	- The three changes to existing behaviour included in this feature are: Clearing Clips, Nudging Notes and Shifting a Clip Horizontally.
 - Follow-up PR's: 
	- ([#347]) Added new automatable parameters
 	- ([#360]) Fixed interpolation bugs, added fine tuning for long presses, and added pad selection mode
	- ([#636]) Updated Parameter Values displayed in Automation View to match Parameter Value Ranges displayed in the Menu's. E.g. instead of 0 - 128, it now displays 0 - 50 (except for Pan which now displays -25 to +25 and MIDI instrument clips which now display 0 - 127).
	- ([#658]) Added Stutter Rate Parameter to Automation View. There is no grid shortcut for this parameter so you will not see a pad on the Automation Overview that indicates whether Stutter has been automated. This parameter can be selected and automated using the Select Encoder to scroll the available list of Automatable Parameters.
	- ([#681]) Added new automation community feature menu to re-instate audition pad shortcuts in the Automation Instrument Clip View.
		- Currently in the Instrument Clip View if you hold down an audition pad and press a shortcut pad on the grid, it will open the menu corresponding to that shortcut pad.
		- By default in the Automation Instrument Clip View that same behaviour of holding an audition pad and pressing a shortcut pad is disabled in favour of you being able to hold down an audition pad and adjust the automation lane values so that you can audible hear the changes to the sound while adjusting automation settings.
		- Through the community features menu, you can disable this change and re-instate the audition pad shortcuts by setting the community feature to "Off."

#### 4.3.6 - Set Probability By Row

- ([#368]) Extends the probability system to set a row at a time. Hold an `AUDITION` pad and turn `SELECT` to change the whole rows probability. This is particularly useful in combination with the euclidean sequencing to get a semi random pattern going

### 4.4 - Instrument Clip View - Synth/MIDI/CV Clip Features

#### 4.4.1 - Keyboard View

##### 4.4.1.1 - Row Step

 - ([#46]) Row Step affects the offset between rows, `ROW STEP` is now configurable by holding `SHIFT` and turning `◀︎▶︎`. The default Deluge Keyboard note offset is 5. A row step of 12 will produce a layout in which any given note's octave is directly above or below it. The setting is saved per clip in the song file. 

##### 4.4.1.2 - Keyboard API and General Improvements

 - ([#138] and [#337])
  	 - Users can switch between layouts by pressing `KEYBOARD` and turning `SELECT`
	 - Keyboard mode allows freely switch between all types (`SYNTH, KIT, MIDI, CV`) automatically getting the first compatible layout
	 - Drum trigger edge sizes in Drums layout for kits can now be changed between 1 and 8 with `SHIFT` + turn `◀︎▶︎`
	 - A new in-key only layout that removes out of scale buttons/notes.
	 - New way to change `SCALE` in keyboard mode: Hold `SCALE` and press `SELECT`
	 - New way to change scale `ROOT NOTE` in keyboard mode: Hold `SCALE` and turn `SELECT`
	 - A new menu to select the default Keyboard Layout has been added in `MENU -> DEFAULTS -> UI -> KEYBOARD -> LAYOUT`

##### 4.4.1.3 - Highlight Incoming Notes

 - ([#250]) New community feature makes In-Key and Isometric layout display incoming MIDI notes with their velocity.
	- This feature is `ON` by default and can be set to `ON` or `OFF` in the `COMMUNITY FEATURES` menu (via `SETTINGS > COMMUNITY FEATURES`). 

##### 4.4.1.4 - Display Norns Layout

 - ([#250]) New community feature renders all incoming notes consecutively as white pads with velocity as brightness.
	- This feature is `OFF` by default and can be set to `ON` or `OFF` in the `COMMUNITY FEATURES` menu (via `SETTINGS > COMMUNITY FEATURES`).
   
### 4.5 - Instrument Clip View - Synth/Kit Clip Features

#### 4.5.1 - Mod Matrix
 - ([#157]) Add a `MOD MATRIX` entry to the `SOUND` menu which shows a list of all currently active modulations.

#### 4.5.2 - Unison Stereo Spread
 - ([#223]) The unison parts can be spread accross the stereo field. Press `SELECT` when in the `UNISON NUMBER` menu to access the new unison spread parameter.
 - **Warning: Do not use with `RING MOD` synth type when `UNISON NUMBER` and `NOISE` are both values greater than 1. Extremely loud noise is produced. (Known Bug)**

#### 4.5.3 - Waveform Loop Lock
 - ([#293]) When a sample has `LOOP START` (CYAN) and `LOOP END` (MAGENTA) points set, holding down `LOOP START` and tapping `LOOP END` will `LOCK` the points together. Moving one will move the other, keeping them the same distance apart. Use the same process to unlock the loop points. Use `SHIFT` + turn `◀︎▶︎` to double or half the loop length.
- **WARNING: If you shift loop points after being locked there is a chance when you delete a loop point, a loop point will exist outside of the SAMPLE START or SAMPLE END range and cause a crash. (Known Bug)**

 #### 4.5.4 - Copy row(s)
 - ([#805]) If you hold down the audition pad(s) while pressing the shortcut to "copy notes", only the auditioned rows will be copied. If you were to normally paste these rows, no behavior is changed from the current implementation. It's exactly the same result as deleting the notes on any rows you didn't have held down before copying.

 #### 4.5.5 - Paste gently
 - ([#805]) Shift+Cross+X_ENC will paste any notes on the clipboard _over_ the existing notes i.e. the existing notes will not be deleted and the clipboard contents will be added to the existing notes. Positioning/scale/timing semantics have not changed, only whether the notes are cleared before pasting. 

### 4.6 - Instrument Clip View - Kit Clip Features

#### 4.6.1 - Keyboard View
 - ([#112]) All-new use for the `KEYBOARD` button in kit clips, uses the main pad grid for MPC-style 16 level playing. `◀︎▶︎` scrolls by one pad at a time, allowing positioning drums left to right, and `▼︎▲︎` jumps vertically by rows.
 - Follow-up PR: ([#317]) Fixed the issue where audition pads would remain illuminated after pressing pads 9 to 16 and then returning to the Clip View.

#### 4.6.2 - Drum Randomizer / Load Random Samples

 - ([#122]) Pressing `AUDITION` + `RANDOM` on a drum kit row will load a random sample from the same folder as the currently enabled sample and load it as the sound for that row. Currently limited to 25 files for performance reasons.
	- This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

#### 4.6.3 - Manual Slicing / Lazy Chop

- ([#198]) In the `SLICE` menu, press `◀︎▶︎` to enter `MANUAL SLICING`. When you press the green pad in the bottom left, it starts playing the first slice, and pressing any empty pad creates a new slice at the time you've pressed it. Turning `SELECT` to the left or pressing the `PAD` + `DELETE` button allows you to delete a slice.

- Turning `◀︎▶︎` allows you to adjust the start point of the slice. Additionally, turning `▼︎▲︎` transposes the slice by a semitone.

- Follow-up PR: ([#210]) Changed to stop audio preview (played by the sample browser) when entering manual slicer mode.

#### 4.6.4 - Batch Delete Kit Rows
   
 - ([#234]) While you can delete a kit row by holding a Note in a row and pressing `SAVE/DELETE`, the `DELETE UNUSED KIT ROWS` feature allows you to batch delete kit rows which does not contain any notes, freeing kits from unused sounds (which take some precious RAM). While inside a Kit, hold `KIT` + `SHIFT` + `SAVE/DELETE`. A confirmation message will appear: "DELETE UNUSED ROWS". This command is not executed if there are no notes at all in the kit.
	- This feature is `ON` by default and can be set to `ON` or `OFF` via `SETTINGS > COMMUNITY FEATURES`.

### 4.7 Audio Clip View - Features

#### 4.7.1 - Shift Clip

 - ([#141]) Holding `▼︎▲︎` down while turning `◀︎▶︎` will shift the waveform of an Audio clip, similar to Instrument clips.

## 5. Community Features Menu (aka Runtime Settings)

In the main menu of the Deluge (accessed by pressing both "SHIFT" + the "SELECT" encoder) there is the `COMMUNITY FEATURES` (OLED) or `FEAT` (7SEG) entry which allows you to turn features on and off as needed. Here is a list of all options as listed in OLED and 7SEG displays and what they do:

* Drum Randomizer (DRUM)
	* When On, the "AUDITION + RANDOM" shortcut is enabled.
* Fine Tempo Knob (TEMP)
	* When On, the Fine Tempo change option is enabled.
* Quantize (QUAN)
	* When On, the Note Quantize shortcut is enabled.
* Mod. Depth Decimals (MOD.)
	* When On, Modulation Resolution is increased.
* Catch Notes (CATC)
	* When Off, existing "Catch Notes" behaviour is disabled.
* Delete Unused Kit Rows (UNUS)
	* When On, the Delete Unused Kit Rows shortcut (hold "KIT" then "SHIFT" + "SAVE/DELETE") is enabled.
* Alternative Golden Knob Delay Params (DELA)
	* When On, it changes the behaviour of the Mod Encoder button action from the default (PingPong and Type) to the alternative params (SyncType and SyncLevel).
* Stutter Rate Quantize (STUT)
	* When On, the ability to set the stutterer effect to be quantized to 4th, 8th, 16th, 32nd, and 64th rate when selecting it is enabled.
* Automation (AUTO)
	* Interpolation (INTE)
		* When On, Interpolation is on by default in the Automation Instrument Clip View.
		* Note: This is just a default setting and can be overriden in the Automation Instrument Clip View using the Select encoder button.
	* Clear Clip (CLEA)
		* When On, clearing a clip in the regular Instrument Clip View will clear Notes and MPE, but not Automation.
		* When On, to clear Non-MPE Automation you will need to enter the Automation Instrument Clip View.
	* Nudge Note (NUDG)
		* When On, nudging a note in the regular Instrument Clip View will nudge the Note and MPE, but not the Automation.
		* When On, to nudge Non-MPE Automation, you will need to either Shift or Manually Edit the automation in the Automation Instrument Clip View.
	* Shift Note (SHIF)
		* When On, shifting notes horizontally in the regular Instrument Clip View will shift the Notes and MPE, but not the Automation.
		* When On, to shift Non-MPE Automation horizontally you will need to enter the Automation Instrument Clip View.
	* Disable Audition Pad Shortcuts (SCUT)
		* When On, audition pad shortcuts are disabled. Holding an audition pad and pressing a shortcut pad will not activate the shortcut and will not change the selected parameter.
		* When On, to change the selected parameter you will need to either: 
			1) use the select encoder; 
			2) use the shift + shortcut pad combo; or
			3) go back to the automation overview;
* Allow Insecure Develop Sysex Messages (SYSX)
  	* When On, the ability to load firmware over USB is enabled.
* Sync Scaling Action (SCAL)
  	* When set to Fill, it changes the behaviour of the "SYNC-SCALING" button is changed to activate "FILL" mode. The original Sync Scaling button function is moved to "SHIFT" + "SYNC-SCALING".
* Highlight Incoming Notes (HIGH)
  	* When On, In-Key and Isometric Keyboard layouts display incoming MIDI notes with their velocity.
* Display Norns Layout (NORN)
  	* When On, all incoming notes are rendered consecutively as white pads with velocity as brightness.
* Sticky Shift (STIC)
  	* When On, tapping shift briefly will enable sticky keys while a long press will keep it on. Enabling this setting will automatically enable "Light Shift" as well.
* Light Shift (LIGH)
  	* When On, the Deluge will illuminate the shift button when shift is active. Mostly useful in conjunction with sticky shift.
* Grain FX (GRFX)
	* When On, `GRAIN` will be a selectable option in the `MOD FX TYPE` category. Resource intensive, recommended to only use one instance per song or resample and remove instance afterwards.	 

## 6. Sysex Handling

Support for sending and receiving large sysex messages has been added. Initially, this has been used for development centric features.

- ([#174] and [#192]) Send the contents of the screen to a computer. This allows 7SEG behavior to be evaluated on OLED hardware and vice versa
- ([#215]) Forward debug messages. This can be used as an alternative to RTT for print-style debugging.
- ([#295]) Load firmware over USB. As this could be a security risk, it must be enabled in community feature settings

## 7. Compiletime settings

This list includes all preprocessor switches that can alter firmware behaviour at compile time and thus require a different firmware

* ENABLE_SYSEX_LOAD

    Allow loading firmware over sysex as described above

* FEATURE_...

    Description of said feature, first new feature please replace this

[#17]: https://github.com/SynthstromAudible/DelugeFirmware/pull/17
[#29]: https://github.com/SynthstromAudible/DelugeFirmware/pull/29
[#32]: https://github.com/SynthstromAudible/DelugeFirmware/pull/32
[#46]: https://github.com/SynthstromAudible/DelugeFirmware/pull/46
[#47]: https://github.com/SynthstromAudible/DelugeFirmware/pull/47
[#103]: https://github.com/SynthstromAudible/DelugeFirmware/pull/103
[#112]: https://github.com/SynthstromAudible/DelugeFirmware/pull/112
[#118]: https://github.com/SynthstromAudible/DelugeFirmware/pull/118
[#120]: https://github.com/SynthstromAudible/DelugeFirmware/pull/120
[#122]: https://github.com/SynthstromAudible/DelugeFirmware/pull/122
[#125]: https://github.com/SynthstromAudible/DelugeFirmware/pull/125
[#129]: https://github.com/SynthstromAudible/DelugeFirmware/pull/129
[#138]: https://github.com/SynthstromAudible/DelugeFirmware/pull/138
[#141]: https://github.com/SynthstromAudible/DelugeFirmware/pull/141
[#147]: https://github.com/SynthstromAudible/DelugeFirmware/pull/147
[#157]: https://github.com/SynthstromAudible/DelugeFirmware/pull/157
[#163]: https://github.com/SynthstromAudible/DelugeFirmware/pull/163
[#170]: https://github.com/SynthstromAudible/DelugeFirmware/pull/170
[#174]: https://github.com/SynthstromAudible/DelugeFirmware/pull/174
[#178]: https://github.com/SynthstromAudible/DelugeFirmware/pull/178
[#192]: https://github.com/SynthstromAudible/DelugeFirmware/pull/192
[#196]: https://github.com/SynthstromAudible/DelugeFirmware/pull/196
[#198]: https://github.com/SynthstromAudible/DelugeFirmware/pull/198
[#200]: https://github.com/SynthstromAudible/DelugeFirmware/pull/200
[#210]: https://github.com/SynthstromAudible/DelugeFirmware/pull/210
[#211]: https://github.com/SynthstromAudible/DelugeFirmware/pull/211
[#212]: https://github.com/SynthstromAudible/DelugeFirmware/pull/212
[#215]: https://github.com/SynthstromAudible/DelugeFirmware/pull/215
[#220]: https://github.com/SynthstromAudible/DelugeFirmware/pull/220
[#221]: https://github.com/SynthstromAudible/DelugeFirmware/pull/221
[#223]: https://github.com/SynthstromAudible/DelugeFirmware/pull/223
[#234]: https://github.com/SynthstromAudible/DelugeFirmware/pull/234
[#241]: https://github.com/SynthstromAudible/DelugeFirmware/pull/241
[#250]: https://github.com/SynthstromAudible/DelugeFirmware/pull/250
[#251]: https://github.com/SynthstromAudible/DelugeFirmware/pull/251
[#282]: https://github.com/SynthstromAudible/DelugeFirmware/pull/282
[#293]: https://github.com/SynthstromAudible/DelugeFirmware/pull/293
[#295]: https://github.com/SynthstromAudible/DelugeFirmware/pull/295
[#317]: https://github.com/SynthstromAudible/DelugeFirmware/pull/317
[#336]: https://github.com/SynthstromAudible/DelugeFirmware/pull/336
[#337]: https://github.com/SynthstromAudible/DelugeFirmware/pull/337
[#339]: https://github.com/SynthstromAudible/DelugeFirmware/pull/339
[#347]: https://github.com/SynthstromAudible/DelugeFirmware/pull/347
[#349]: https://github.com/SynthstromAudible/DelugeFirmware/pull/349
[#357]: https://github.com/SynthstromAudible/DelugeFirmware/pull/357
[#360]: https://github.com/SynthstromAudible/DelugeFirmware/pull/360
[#363]: https://github.com/SynthstromAudible/DelugeFirmware/pull/363
[#368]: https://github.com/SynthstromAudible/DelugeFirmware/pull/368
[#395]: https://github.com/SynthstromAudible/DelugeFirmware/pull/395
[#512]: https://github.com/SynthstromAudible/DelugeFirmware/pull/512
[#630]: https://github.com/SynthstromAudible/DelugeFirmware/pull/630
[#636]: https://github.com/SynthstromAudible/DelugeFirmware/pull/636
[#653]: https://github.com/SynthstromAudible/DelugeFirmware/pull/653
[#658]: https://github.com/SynthstromAudible/DelugeFirmware/pull/658
[#681]: https://github.com/SynthstromAudible/DelugeFirmware/pull/681
[#683]: https://github.com/SynthstromAudible/DelugeFirmware/pull/683
[#711]: https://github.com/SynthstromAudible/DelugeFirmware/pull/711
[#781]: https://github.com/SynthstromAudible/DelugeFirmware/pull/781
[Automation View Documentation]: https://github.com/SynthstromAudible/DelugeFirmware/blob/release/1.0/docs/features/automation_view.md
