# Bug Fixes
## 1. Introduction

Every time a Pull Request fixes a bug in the community firmware it shall be noted down.

## 2. File Compatibility Warning

In general, we try to maintain file compatibility with the official firmware. However, **files (including songs, presets, etc.) that use community features may not ever load correctly on the official firmware again**. Make sure to back up your SD card!

## 3. General Improvements

Here is a list of general improvements that have been made, ordered from newest to oldest:

#### 3.1 - MPE
- ([#29]) Bugfix to respect MPE zones in kit rows. In the official firmware, kit rows with MIDI learned to a channel would be triggered by an MPE zone which uses that channel. With this change they respect zones in the same way as synth and MIDI clips. ([#512]) adds further fixes related to channels 0 and 15 always getting received as MPE.

#### 3.2 - Set Default Mod-FX Type to Disabled for Audio Clip and Kit Affect Entire Parameters
- ([#945]) Previously, when creating a new Audio clip or Kit clip, the default Mod-FX Type was set to Flanger. This has now been corrected and the default Mod-FX Type has been set to Disabled.

[#29]: https://github.com/SynthstromAudible/DelugeFirmware/pull/29
[#512]: https://github.com/SynthstromAudible/DelugeFirmware/pull/512
[#945]: https://github.com/SynthstromAudible/DelugeFirmware/pull/945
