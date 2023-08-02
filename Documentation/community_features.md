# Introduction

Every time a Pull request improves the community firmware it shall note down it's achievements and usage in this document.

# File Compatibility Warning
In general, we try to maintain file compatibility with the official firmware. However, **files (including songs, presets, etc.) that use community features may not ever load correctly on the official firmware again**. Make sure to back up your SD card!

# General improvements

Here is a list of general improvements that have been made ordered from newest to oldest:

* PR [#17] - Increase the resolution of "patch cables" between mod sources and destinations.
* PR [#29] - Bugfix to respect MPE zones in kit rows. In the official firmware kit rows with midi learned to a channel would be triggered by an MPE zone which uses that channel. With this change they respect zones in the same way as synth and midi clips.
* PR [#47] - Extra MIDI ports on the USB interface for MPE. Port 2 shows in the midi device menu, and improves the usability of MPE-capable devices through the USB interface by allowing MPE zones to be sent to port 2 and non-MPE to be sent to port 1 (or vice versa). A third port is added for future use such as a desktop/mobile companion app, DAW control or Mackie HUI emulation.
* PR [#178] - New option (FINE TEMPO in the "community features" menu) to invert the push+turn behavior of the tempo knob. With this option enabled the tempo changes by 1 when unpushed and 4 when pushed (vs 4 unpushed and 1 pushed in the official firmware). This option defaults to OFF.


# Added features

Here is a list of features that have been added to the firmware as a list, grouped by category:

## Synthesizer features

### New LFO shapes
LFO types added to the "LFO SHAPE" shortcut.

 - ([#32]) Random Walk. Starts at zero and walks up or down by small increments when triggered.
 - ([#32]) Sample&Hold. Picks a new random value every time it is triggered.

### New LFO Synchronization Modes
Synchronization modes accessible through the "LFO SYNC" shortcut.

 - ([#32]) Triplets. Synchronizes the LFO to triplet (3/2) divisions.
 - ([#32]) Dotted. Synchronizes the LFO to "dotted" (2/3) divisions.

### Filters
 - ([#103]) adds a new filter in the low-pass slot, a state-variable filter. This filter has significantly less distortion than the ladder filters, think sequential vs. moog. Cutoff and resonance ranges are subject to change with further testing.

## New behaviors
### Song View
 - ([#163]) Pressing a clip row + shift & scroll vertically changes the selected row color. This is the same shortcut like before when setting the color in the clip view.

### Instrument Keyboard View
 - ([#46]) Note offset between rows is now configurable by holding shift and using the horizontal encoder. This allows e.g. an isomorphic keyboard layout by setting the row offset to 12. The setting is saved per clip in the song file.
 - ([#138]) Keyboard API and general improvements
	 - Users can switch between layouts with "keyboard" button and select knob
	 - Keyboard mode allows freely switch between all types (Synth, Kit, MIDI, CV) automatically getting the first compatible layout
	 - Drum trigger edge sizes in Drums layout for kits can now be changed between 1 and 8 with shift + horizontal encoder
	 - A new in-key only layout that removes out of scale buttons
	 - New way to change scale in keyboard mode: Hold scale and press selection knob
	 - New way to change scale root note in keyboard mode: Hold scale and turn selection knob

### Kit Clip View
 - ([#122]) Pressing "AUDITION + RANDOM" on a drum kit row will load a random sample from the same folder as the currently enabled sample and load it as the sound for that row. Currently limited to 25 files for performance reasons. This feature can be toggled in the [runtime features menu](#runtime-features).
  - ([#234]) While you can delete a kit row by holding a Note in a row and pressing SAVE/DELETE, the "Delete Unused Kit Rows" feature allows you to batch delete kit rows which does not contain any notes, freeing kits from unused sounds (which take some precious RAM). While inside a kit, hold "KIT" and press "Shift + SAVE/DELETE". A confirmation message will appear: "Deleted unused rows". This command is not executed if there are no notes at all in the kit. This feature can be toggled in the [runtime features menu](#runtime-features).

### Kit Keyboard View
 - ([#112]) All-new use for the "keyboard" button in kit clips, uses the main pad grid for MPC-style 16 level playing. Horizonatal encoder scrolls by one pad at a time, allowing positioning drums left to right, and vertical encoder jumps vertically by rows.

### Audio Clip View
 - ([#141]) Holding the vertical encoder down while turning the horizontal encoder will shift the clip along the underlying audio file, similar to the same interface for instrument clips.

### Takeover Mode

 - ([#170]) The Takeover menu consists of three modes that can be selected from:

	1. Jump: This is the default mode for the Deluge. As soon as a Midi Knob/Fader position is changed, the Deluge's internal Knob position/Parameter value jumps to the position of the Midi Knob/Fader.

	2. Pickup: The deluge will ignore changes to its internal Knob position/Parameter value until the Midi Knob/Fader's position is equal to the Deluge Knob position. After which the Midi Knob/Fader will move in sync with the Deluge.

	3. Scale: The deluge will increase/decrease its internal Knob position/Parameter value relative to the change of the Midi Knob/Fader position and the amount of "runway" remaining on the Midi controller. Once the Midi controller reaches its maximum or minimum position, the Midi Knob/Fader will move in sync with the Deluge. The Deluge will value will always decrease/increase in the same direction as the Midi controller.

### Catch Notes
 - ([#221]) The normal behavior of the Deluge is to try to keep up with 'in progress' notes when instant switching between clips by playing them late. However this leads to glitches with drum clips and other percussive sounds. Changing this setting to OFF will prevent this behavior and *not* try to keep up with those notes, leading to smoother instant switching between clips.

### Alternative Delay Params for golden knobs
 - ([#281]) Ability to select, using a Community Features Menu, which parameters are controlled when you click the Delay-related golden knobs. The default (for upper and lower knobs) is PingPong On/Off and Type (Digital/Analog), and you can modify it so the knob clicks change the Sync Type (Even, Triplets, Even) and SyncLevel (Off, Whole, 2nd, 4th...) respectively.

<h1 id="runtime-features">Runtime settings aka Community Features Menu</h1>

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


# Compiletime settings

This list includes all preprocessor switches that can alter firmware behaviour at compile time and thus require a different firmware

* HAVE_OLED

    Currently determines if the built firmware is intended for OLED or 7SEG hardware

* FEATURE_...

    Description of said feature, first new feature please replace this

[#17]: https://github.com/SynthstromAudible/DelugeFirmware/pull/17
[#29]: https://github.com/SynthstromAudible/DelugeFirmware/pull/29
[#32]: https://github.com/SynthstromAudible/DelugeFirmware/pull/32
[#46]: https://github.com/SynthstromAudible/DelugeFirmware/pull/46
[#47]: https://github.com/SynthstromAudible/DelugeFirmware/pull/47
[#103]: https://github.com/SynthstromAudible/DelugeFirmware/pull/103
[#112]: https://github.com/SynthstromAudible/DelugeFirmware/pull/112
[#122]: https://github.com/SynthstromAudible/DelugeFirmware/pull/122
[#141]: https://github.com/SynthstromAudible/DelugeFirmware/pull/141
[#138]: https://github.com/SynthstromAudible/DelugeFirmware/pull/138
[#163]: https://github.com/SynthstromAudible/DelugeFirmware/pull/163
[#178]: https://github.com/SynthstromAudible/DelugeFirmware/pull/178
[#170]: https://github.com/SynthstromAudible/DelugeFirmware/pull/170
[#221]: https://github.com/SynthstromAudible/DelugeFirmware/pull/221
[#234]: https://github.com/SynthstromAudible/DelugeFirmware/pull/234
[#281]: https://github.com/SynthstromAudible/DelugeFirmware/pull/281
