# Community Features
## 1. Introduction

Every time a Pull request improves the community firmware it shall note down it's achievements and usage in this document.

## 2. File Compatibility Warning
In general, we try to maintain file compatibility with the official firmware. However, **files (including songs, presets, etc.) that use community features may not ever load correctly on the official firmware again**. Make sure to back up your SD card!

## 3. General Improvements

Here is a list of general improvements that have been made, ordered from newest to oldest:

#### 3.1 - Patch Cables

- ([#17]) - Increase the resolution of "patch cables" between mod sources and destinations.

#### 3.2 - MPE
- ([#29]) - Bugfix to respect MPE zones in kit rows. In the official firmware kit rows with midi learned to a channel would be triggered by an MPE zone which uses that channel. With this change they respect zones in the same way as synth and midi clips.

#### 3.3 - MIDI
- ([#47]) - Extra MIDI ports on the USB interface for MPE. Port 2 shows in the midi device menu, and improves the usability of MPE-capable devices through the USB interface by allowing MPE zones to be sent to port 2 and non-MPE to be sent to port 1 (or vice versa). A third port is added for future use such as a desktop/mobile companion app, DAW control or Mackie HUI emulation.

#### 3.4 - Tempo
- ([#178]) - New option (FINE TEMPO in the "community features" menu) to invert the push+turn behavior of the tempo knob. With this option enabled the tempo changes by 1 when unpushed and 4 when pushed (vs 4 unpushed and 1 pushed in the official firmware). This option defaults to OFF.

## 4. New Features Added

Here is a list of features that have been added to the firmware as a list, grouped by category:

### 4.1 - Song View Features

#### 4.1.1 - Master Compressor
- ([#137]) In the Song view, select Affect Entire and SIDECHAIN modulation button, and adjust the upper gold knob. Push the upper gold knob to switch to the next setting (Threshold (dB), Makeup Gain (dB), Attack (ms), Release (ms), Ratio, MIX). The LEDs next to the knob act as a gain reduction meter.

- This compressor function can be turned on/off in the runtime feature setting.

- Follow up PR's:
	- ([#200]) Fixed master compressor. The masterVolumeAdjustment value is now considered in the process. With this change, threshold value are now displayed correctly. This modification affects songs that had saved master compressor settings.
	- ([#220] Fixed a bug in the song view that, when the SIDECHAIN knob was turned while holding down a clip pad, the SIDECHAIN value and the Master compressor Threshold would change at the same time.

#### 4.1.2 - Change Row Colour

 - ([#163]) Pressing a clip row + shift & scroll vertically changes the selected row color. This is the same shortcut like before when setting the color in the clip view.

#### 4.1.3 - New Grid Layout

 - ([#251]) Add new grid session layout to "Song" mode. All functionality from (classic) row layout applies except for the following:
	 - The data model of rows and grid mode are compatible, you can switch between them freely
	 - In grid mode you will not be able to see multiple clips that are in the same section, only the first one. To make them visible move the clips to other sections
	 - The colored coloumn on the right are all available sections, the columns are automatically filled with the tracks in the same order as in arrangement mode
	 - In session mode hold "Song" and turn selection encoder to switch between row layout and grid layout
	 - Existing clips (dimly white or green) can be opened by holding "Clip" button and clicking on them
	 - New clips can be created by holding "Clip" button and clicking on an empty pad. If the column was empty a new track is created
	 - By quickly clicking (and releasing) populated pads you can change the arm state
		 - If "Shift" is held at the same time the clip will launch immediately
		 - If "Record" is held at the same time you can change recording status
		 - If horizontal encoder <> is held at the same time you can change solo state
	 - By holding a populated pad you can see the track, change the parameters and convert it to other instruments similar to rows layout
	 - Hold an existing pad and press on another pad in the same, other or empty row to copy clips. If possible the content will be converted to the target track type
	 - To delete a clip hold the pad and press the "Save/Delete" button
	 - To arm a whole row click on the section color to the right
	 - To immediately switch to a whole row hold "Shift" and click on the section color
	 - To MIDI learn:
		 - Arming a section hold "Learn/Input" and hold the section pad
		 - Arming a clip hold "Learn/Input" and hold the clip pad
		 - Note input to a track hold "Shift" + "Learn/Input" and hold the pad of any populated clip for that track
	 - Compared to rows layout the following is not supported
	 	 - Overdub recording
		 - Copying clips to arranger
		 - Copying audio clips between different tracks

#### 4.1.3 - Catch Notes

 - ([#221]) The normal behavior of the Deluge is to try to keep up with 'in progress' notes when instant switching between clips by playing them late. However this leads to glitches with drum clips and other percussive sounds. Changing this setting to OFF will prevent this behavior and *not* try to keep up with those notes, leading to smoother instant switching between clips.

### 4.2 - Clip View - General Features (Instrument and Audio)

#### 4.2.1 - Filters
 - ([#103]) Adds a new filter in the low-pass slot, a state-variable filter. This filter has significantly less distortion than the ladder filters, think sequential vs. moog. Cutoff and resonance ranges are subject to change with further testing.

	- Follow-up PR's: ([#125] and [#212]) Various SVF fixes

- ([#336]) Morphable and Driveable Parallel Filters
	- Add an SVF option to the HPF slot, set for HPF mode by default. This can be modified through the menu, shortcuts, or clicking the lower knob while HPF is selected.

	- Follow-up PR: ([#339]) SVF Notch Filter
 		- Adds an SVF notch mode (SV_Notch) alongside the SVF Band mode (SV_Band)
		- Changes SVF morph in HP slot to go HP-band/notch-LP (HP by default)
		- Tweaks default ladder saturation to match original firmware
		- Applies ladder gain on input instead of feedback, avoiding interaction with resonance
		- Tweaks SVF level to match ladder volumes
		- Removes distinction between high and lowpass SVF enums so they can now be used in either slot
adds HP ladder morph to filter FM

	- Adds a 3rd filter paramater called morph, placed in the menu and on the pads under the db/oct on filters. Morph smoothly morphs the SVF through LP-BP-HP, and smoothly increases drive in the ladder filters. This param is modulatable and automatable

	- Adds a setting to switch the filter order or run them in parallel. This setting is menu only and named ROUTE

#### 4.2.2 - Stereo Chorus
- ([#120]) New Steroe Chorus type added to Mod FX. The recommended settings are OFFSET=30, DEPTH=17, and RATE=15.

#### 4.2.2 - Mod Matrix
 - ([#157]) Add a "Mod Matrix" entry to the sound editor menu which shows a list of all currently active modulations.

#### 4.2.3 - MIDI Takeover Mode

This mode affects how the deluge handles MIDI input for learned CC controls.

 - ([#170]) A new takeover submenu was created in the MIDI settings menu which consists of three modes that can be selected from:

	1. Jump: This is the default mode for the Deluge. As soon as a Midi Knob/Fader position is changed, the Deluge's internal Knob position/Parameter value jumps to the position of the Midi Knob/Fader.

	2. Pickup: The deluge will ignore changes to its internal Knob position/Parameter value until the Midi Knob/Fader's position is equal to the Deluge Knob position. After which the Midi Knob/Fader will move in sync with the Deluge.

	3. Scale: The deluge will increase/decrease its internal Knob position/Parameter value relative to the change of the Midi Knob/Fader position and the amount of "runway" remaining on the Midi controller. Once the Midi controller reaches its maximum or minimum position, the Midi Knob/Fader will move in sync with the Deluge. The Deluge value will always decrease/increase in the same direction as the Midi controller.

#### 4.2.4 - Alternative Delay Types for Mod Encoders (Golden Knobs)
 - ([#282]) Ability to select, using a Community Features Menu, which parameters are controlled when you click the Delay-related golden knobs. The default (for upper and lower knobs) is PingPong On/Off and Type (Digital/Analog), and you can modify it so the knob clicks change the Sync Type (Even, Triplets, Even) and Sync Level (Off, Whole, 2nd, 4th...) respectively.

#### 4.2.5 - Sample Waveform View
 - ([#293]) When a sample has loop start and loop end points set, holding down loop start and tapping loop end will lock the loop points together. Moving one will move the other, keeping them the same distance apart. Use the same process to unlock the loop points. Use SHIFT+TURN<> to double or half the loop length.

#### 4.2.6 - Grain FX

- ([#363]) New Grain FX type added to Mod FX. This effect is somewhat resource-intensive, so please use only one instance per song.

	- Parameters:
		- Mod Depth: Controls Grain Volume / Dry Wet Mix
		- Mod Offset: Adjusts Grain Size (10ms - 800ms)
		- Mod Rate: Sets Grain Rate (0.5hz - 180hz)
		- Mod Feedback: Selects Grain Type

	- Grain Type (Presets):
		- Preset 1: Unison and +1 Octave (Reverse)
		- Preset 2: Unison and -1 Octave
		- Preset 3: Unison and +1 Octave (Defalut)
		- Preset 4: 5th and +1 Octave
		- Preset 5: Unison and +1/-1 Octave (Tempo Sync)

### 4.3 - Instrument Clip View - General Features

These features were added to the Instrument Clip View and affect Synth, Kit and Midi instrument clip types.

#### 4.3.1 - New LFO Shapes
LFO types added to the "LFO SHAPE" shortcut.

 - ([#32]) - **Random Walk:** Starts at zero and walks up or down by small increments when triggered.
 - ([#32]) - **Sample&Hold:** Picks a new random value every time it is triggered.

#### 4.3.2 - New LFO Synchronization Modes
Synchronization modes accessible through the "LFO SYNC" shortcut.

 - ([#32]) - **Triplets:** Synchronizes the LFO to triplet (3/2) divisions.
 - ([#32]) - **Dotted:** Synchronizes the LFO to "dotted" (2/3) divisions.

#### 4.3.3 - Quantize & Humanize
 - ([#129])
	- Press and hold a note in clip view and turn the tempo knob right or left to apply quantize or humanize respectively to that row.
	- Press and hold a note and press and turn the tempo knob to apply quantize or humanize to all rows.
	- The amount of quantization/humanization is shown in the display.
	- This feature can be turned On/Off in the runtime feature setting. (Runtime Feature Setting is available in the Shift+Select knob menu.)

#### 4.3.4 - Fill Mode
 - ([#211]) Fill Mode is a new iteration/probability setting for notes. The FILL setting is at the start of the probability range, before 5%. Notes set to FILL are only played when fill mode is active. There are two ways to activate fill mode - set it as a Global MIDI Command and/or set it to override the front panel Sync Scaling button. For Global MIDI Commands go to SETTINGS > MIDI > CMD > FILL. To override the Sync Scaling button set SETTINGS > FEATURES > SYNC to FILL. The orignal Sync Scaling function is moved to SHIFT+SyncScaling.

#### 4.3.5 - Unison Stereo Spread
 - ([#223]) - **Unison: **The unison parts can be spread accross the stereo field. Press SELECT in the unison NUMBER menu to access the new unison spread parameter.

#### 4.3.6 - Automation View
 - For a detailed description of this feature as well the button shortcuts/combos, please refer to the feature documentation: [Automation View Documentation]
 - ([#241]) Automation Instrument Clip View is a new view that complements the existing Instrument Clip View.
	- It is accessed from within the Clip View by pressing the Clip button (which will blink to indicate you are in the Automation View).
	- You can edit Non-MPE Parameter Automation for Synth, Kit and Midi instrument clips on a per step basis at any zoom level.
	- A community features sub-menu titled Automation was created to access a number of configurable settings for changes to existing behaviour.
	- The three changes to existing behaviour included in this feature are: Clearing Clips, Nudging Notes and Shifting a Clip Horizontally.
 - Follow-up PR's: 
	- ([#347]) Added new automatable parameters
 	- ([#360]) Fixed interpolation bugs, added fine tuning for long presses, and added pad selection mode

#### 4.3.7 - Set Probability By Row

- ([#368]) Extends the probability system to set a row at a time. Hold an audition pad and turn select to change the whole rows probability. This is particularly useful in combination with the euclidean sequencing to get a semi random pattern going

### 4.4 - Instrument Clip View - Synth/Midi Features

#### 4.4.1 - Keyboard View

##### 4.4.1.1 - Note Offset

 - ([#46]) Note offset between rows is now configurable by holding shift and using the horizontal encoder. This allows e.g. an isomorphic keyboard layout by setting the row offset to 12. The setting is saved per clip in the song file.

##### 4.4.1.2 - Keyboard API and General Improvements

 - ([#138] and [#337])
  	 - Users can switch between layouts with "keyboard" button and select knob
	 - Keyboard mode allows freely switch between all types (Synth, Kit, MIDI, CV) automatically getting the first compatible layout
	 - Drum trigger edge sizes in Drums layout for kits can now be changed between 1 and 8 with shift + horizontal encoder
	 - A new in-key only layout that removes out of scale buttons
	 - New way to change scale in keyboard mode: Hold scale and press selection knob
	 - New way to change scale root note in keyboard mode: Hold scale and turn selection knob

##### 4.4.1.3 - Highlight Incoming Notes

 - ([#250]) New community feature makes In-Key and Isometric layout display incoming MIDI notes with their velocity.

##### 4.4.1.4 - Display Norns Layout

 - ([#250]) New community feature renders all incoming notes consecutively as white pads with velocity as brightness.

### 4.5 - Instrument Clip View - Kit Features

#### 4.5.1 - Kit Keyboard View
 - ([#112]) All-new use for the "keyboard" button in kit clips, uses the main pad grid for MPC-style 16 level playing. Horizontal encoder scrolls by one pad at a time, allowing positioning drums left to right, and vertical encoder jumps vertically by rows.
 - Follow-up PR: ([#317]) Fixed the issue where audition pads would remain illuminated after pressing pads 9 to 16 and then returning to the clip view.

#### 4.5.2 - Drum Randomizer / Load Random Samples

 - ([#122]) Pressing "AUDITION + RANDOM" on a drum kit row will load a random sample from the same folder as the currently enabled sample and load it as the sound for that row. Currently limited to 25 files for performance reasons. This feature can be toggled in the [runtime features menu](#runtime-features).

#### 4.5.3 - Manual Slicing / Lazy Chop

- ([#198]) In the Slicer view, press the Horizonal Encoder ◀︎▶︎ knob. When you press the green pad in the bottom left, it starts playing the first slice, and pressing an empty pad creates a new slice. Turning the Select knob to the left or pressing the Pad + DELETE button allows you to delete a slice.

- Turning the Horizontal Encoder ◀︎▶︎ knob allows you to adjust the start point of the slice. Additionally, turning the ▼︎▲︎ knob transposes the slice.

- Follow-up PR: ([#210]) Changed to stop audio preview (played by the sample browser) when entering manual slicer mode.

#### 4.5.4 - Batch Delete Kit Rows
   
 - ([#234]) While you can delete a kit row by holding a Note in a row and pressing SAVE/DELETE, the "Delete Unused Kit Rows" feature allows you to batch delete kit rows which does not contain any notes, freeing kits from unused sounds (which take some precious RAM). While inside a kit, hold "KIT" and press "Shift + SAVE/DELETE". A confirmation message will appear: "Deleted unused rows". This command is not executed if there are no notes at all in the kit. This feature can be toggled in the [runtime features menu](#runtime-features).

### 4.6 Audio Clip View - General Features

#### 4.6.1 - Shift Clip

 - ([#141]) Holding the vertical encoder down while turning the horizontal encoder will shift the clip along the underlying audio file, similar to the same interface for instrument clips.

## 5. Runtime settings aka Community Features Menu

In the main menu of the deluge (Shift + Pressing selection knob) there is an entry called "Community Features" that allows changing behavior and turning features on and off in comparison to the original and previous community firmwares. Here is a list of all options and what they do:

* Drum Randomizer (DRUM)
	Enable or disables the "AUDITION + RANDOM" shortcut.
* Master Compressor (MAST)
	Enable or disables the master compressor.
* Fine Tempo (FINE)
	Enable or disables the fine tempo change option.
* Quantize (QUAN)
	Enable or disables the note quantize shortcut.
* Quantize (MOD.)
	Enable or disables increased modulation resolution.
* Catch Notes (CATC)
	Enable or disables the 'catch notes' behavior.
* Delete Unused Kit Rows (DELE)
	Enable or disables the Delete Unused Kit Rows shortcut (hold KIT then SHIFT+SAVE/DELETE).
* Alternative Golden Knob Delay Params
	When On, changes the behaviour of the click action, from the default (PingPong and Type) to the alternative params (SyncType and SyncLevel).
* Automation (AUTO)
	* Interpolation
		* When On, Interpolation is on by default in the Automation Instrument Clip View.
		* Note: This is just a default setting and can be overriden in the Automation Instrument Clip View using the Select encoder button.
	* Clear Clip
		* When On, clearing a clip in the regular Instrument Clip View will clear Notes and MPE, but not Automation.
		* When On, to clear Non-MPE Automation you will need to enter the Automation Instrument Clip View.
	* Nudge Note
		* When On, nudging a note in the regular Instrument Clip View will nudge the Note and MPE, but not the Automation.
		* When On, to nudge Non-MPE Automation, you will need to either Shift or Manually Edit the automation in the Automation Instrument Clip View.
	* Shift Note
		* When On, shifting notes horizontally in the regular Instrument Clip View will shift the Notes and MPE, but not the Automation.
		* When On, to shift Non-MPE Automation horizontally you will need to enter the Automation Instrument Clip View.

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
[#120]: https://github.com/SynthstromAudible/DelugeFirmware/pull/120
[#122]: https://github.com/SynthstromAudible/DelugeFirmware/pull/122
[#125]: https://github.com/SynthstromAudible/DelugeFirmware/pull/125
[#129]: https://github.com/SynthstromAudible/DelugeFirmware/pull/129
[#141]: https://github.com/SynthstromAudible/DelugeFirmware/pull/141
[#137]: https://github.com/SynthstromAudible/DelugeFirmware/pull/137
[#138]: https://github.com/SynthstromAudible/DelugeFirmware/pull/138
[#163]: https://github.com/SynthstromAudible/DelugeFirmware/pull/163
[#178]: https://github.com/SynthstromAudible/DelugeFirmware/pull/178
[#170]: https://github.com/SynthstromAudible/DelugeFirmware/pull/170
[#174]: https://github.com/SynthstromAudible/DelugeFirmware/pull/174
[#192]: https://github.com/SynthstromAudible/DelugeFirmware/pull/192
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
[#360]: https://github.com/SynthstromAudible/DelugeFirmware/pull/360
[#363]: https://github.com/SynthstromAudible/DelugeFirmware/pull/363
[#368]: https://github.com/SynthstromAudible/DelugeFirmware/pull/368
[Automation View Documentation]: https://github.com/SynthstromAudible/DelugeFirmware/blob/community/docs/features/automation_view.md
