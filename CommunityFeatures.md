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

Here is a list of features that have been added to the firmware as a list ordered from newest to oldest:

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

### Instrument Keyboard View
 - ([#46]) Note offset between rows is now configurable by holding shift and using the horizontal encoder. This allows e.g. an isomorphic keyboard layout by setting the row offset to 12. The setting is saved per clip in the song file.

### Kit Clip View
 - ([#122]) Pressing "AUDITION + RANDOM" on a drum kit row will load a random sample from the same folder as the currently enabled sample and load it as the sound for that row. Currently limited to 25 files for performance reasons. This feature can be toggled in the [runtime features menu](#runtime-features).

### Kit Keyboard View
 - ([#112]) All-new use for the "keyboard" button in kit clips, uses the main pad grid for MPC-style 16 level playing. Horizonatal encoder scrolls by one pad at a time, allowing positioning drums left to right, and vertical encoder jumps vertically by rows.


<h1 id="runtime-features">Runtime settings aka Community Features Menu</h1>

In the main menu of the deluge (Shift + Pressing selection knob) there is an entry called "Community Features" that allows changing behavior and turning features on and off in comprison to the original and previous community firmwares. Here is a list of all options and what they do:

* DRUM RANDOMIZER
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
[#103]: https://github.com/SynthstromAudible/DelugeFirmware/pull/103
[#112]: https://github.com/SynthstromAudible/DelugeFirmware/pull/112
[#122]: https://github.com/SynthstromAudible/DelugeFirmware/pull/122
