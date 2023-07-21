# Introduction

Every time a Pull request improves the community firmware it shall note down it's achievements and usage in this document.

# File Compatibility Warning
In general, we try to maintain file compatibility with the official firmware. However, **files (including songs, presets, etc.) that use community features may not ever load correctly on the official firmware again**. Make sure to back up your SD card!

# General improvements

Here is a list of general improvements that have been made ordered from newest to oldest:

* PR [#17] - Increase the resolution of "patch cables" between mod sources and destinations.
* PR [#29] - Bugfix to respect MPE zones in kit rows. In the official firmware kit rows with midi learned to a channel would be triggered by an MPE zone which uses that channel. With this change they respect zones in the same way as synth and midi clips.
* PR [#47] - Extra MIDI ports on the USB interface for MPE. Port 2 shows in the midi device menu, and improves the usability of MPE-capable devices through the USB interface by allowing MPE zones to be sent to port 2 and non-MPE to be sent to port 1 (or vice versa). A third port is added for future use such as a desktop/mobile companion app, DAW control or Mackie HUI emulation.


# Added features

Here is a list of features that have been added to the firmware as a list ordered from oldest first:

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

### Kit Clip View
 - ([#122]) Pressing "AUDITION + RANDOM" on a drum kit row will load a random sample from the same folder as the currently enabled sample and load it as the sound for that row. Currently limited to 25 files for performance reasons. This feature can be toggled in the [runtime features menu](#runtime-features).

### Kit Keyboard View
 - ([#112]) All-new use for the "keyboard" button in kit clips, uses the main pad grid for MPC-style 16 level playing. Horizonatal encoder scrolls by one pad at a time, allowing positioning drums left to right, and vertical encoder jumps vertically by rows.
 
### Audio Clip View
 - ([#141]) Holding the vertical encoder down while turning the horizontal encoder will shift the clip along the underlying audio file, similar to the same interface for instrument clips.

### Takeover Mode

 - ([#170]) The Takeover menu consists of three modes that can be selected from:
 
 			1) Jump: This is the default mode for the Deluge. As soon as a Midi Knob/Fader position is changed, 
					 the Deluge's internal Knob position/Parameter value jumps to the position of the Midi Knob/Fader.

			2) Pickup: The deluge will ignore changes to its internal Knob position/Parameter value until the 
					   Midi Knob/Fader's position is equal to the Deluge Knob position. After which the Midi Knob/Fader 
					   will move in sync with the Deluge.
			
			3) Scale: The deluge will increase/decrease its internal Knob position/Parameter value relative 
					  to the change of the Midi Knob/Fader position and the amount of "runway" remaining on the Midi 
					  controller. Once the Midi controller reaches its maximum or minimum position, the Midi Knob/Fader 
					  will move in sync with the Deluge. The Deluge will value will always decrease/increase in the 
					  same direction as the Midi controller.

### Audio Clip Timestretch control

([#54]) usefule for less beat-centric music e.g. ambient, this allows user to enable/disable audio time-stretching, either: 
 - for all audio clips in the song => with **<shift>+SYNC-SCALING** button 
 - for an individual clip => with either **clip/sample/stretch** menu, or **<shift>+Sample1/Mode** shortcut)

See also [Make time stretching optional for AudioClips](https://github.com/SynthstromAudible/DelugeFirmware/issues/53)

#### Song XML changes
Two XML attributes are added to SONG XML files for  </song > and </audioClip> elements (timeStretchingEnabled (bool) . These attributes will be ignored by earlier firmware 
The default time-stretching setting is **on** to remain consistent with earlier versions.

<h1 id="runtime-features">Runtime settings aka Community Features Menu</h1>

In the main menu of the deluge (Shift + Pressing selection knob) there is an entry called "Community Features" that allows changing behavior and turning features on and off in comprison to the original and previous community firmwares. Here is a list of all options and what they do:

### DRUM RANDOMIZER
    Enable or disables the "AUDITION + RANDOM" shortcut. 

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
[#54]: https://github.com/SynthstromAudible/DelugeFirmware/pull/54
[#103]: https://github.com/SynthstromAudible/DelugeFirmware/pull/103
[#112]: https://github.com/SynthstromAudible/DelugeFirmware/pull/112
[#122]: https://github.com/SynthstromAudible/DelugeFirmware/pull/122
[#141]: https://github.com/SynthstromAudible/DelugeFirmware/pull/141
[#163]: https://github.com/SynthstromAudible/DelugeFirmware/pull/163
