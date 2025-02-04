Added ability to save / load Patterns from/to Files. A Pattern represents all Notes of the actual Deluge Screen including the Attributes Velocity, Probability, Lift, Itterance and Fill. The Patterns can be either of Type melodic Instrument (Synt, Midi, CV) or rhythmic Instrument (Kit, Drum).

There are different ways to create Pattern Files. The first way is to just Enter Notes on the Deluge, and then save it to a Pattern. Basically, it works exactly the same way as the Standard Copy/Paste function with the difference, that the Pattern is stored on the SD-Card and not just into the Clipboard. If you load a Pattern, it gets loaded into the Clipboard and then pasted once. After that, you can Paste it on other Screens or even gently Pasted as many Times as you want.

The second Way to use this Feature is by converting MIDI-Files to Deluge Pattern Files and import them. There are two Tools described in the [Additional Tools](#additional-tools) Section. With this tools you can import simple Midi Files and safe them as a Pattern XML File. This XMLs can than be imported on the Deluge (just move them to the [corresponding Folder](#prerequisites)  on your SD Card an load them over the Pattern Load Function).

This feature enables you to build up and share your own Patterns Library on the Deluge. A few Examples what can be stored to a Pattern:

- A simple Drum-Pattern for a single Drum
- A complex Rhythm-Pattern for a Full Kit
- A simple Melody
- A Chord Progression
- A full Instrument Track

Everything you can think of and what can be copy/pasted can now also be stored an recalled to/from the SD Card.


### Prerequisites
You need to add the following File Structure to the Root of your SD Card:

```
"PATTERNS/RHYTHMIC/DRUM"
"PATTERNS/RHYTHMIC/KIT"
"PATTERNS/MELODIC"
```

### HowTo Use it:
**For Melodic Instruments (Synth, Midi, CV)**

Save Melodic Pattern:
`SAVE+PUSH_HORIZONTAL_ENCODER`

Load Melodic Pattern:
`LOAD+PUSH_HORIZONTAL_ENCODER`

Load Melodic Pattern without overwriting existing Notes:
`LOAD+CROSS_SCREEN+PUSH_HORIZONTAL_ENCODER`

Load Melodic Pattern without scaling original File:
`LOAD+SCALE_CHANGE+PUSH_HORIZONTAL_ENCODER`

_Remark: If noScaling is set, Pattern will allways overwrite existing Notes and start from beginning of the Clip and not at the left Side of the Screen._

Preview of Pattern:
Press and hold `PLAY` while in Pattern-Load Menu



**For Rhythmic Instruments (Kit, Drums)**

Save Kit Pattern:
1. Enable `AFFECT_ENTIRE`
2. `SAVE+PUSH_HORIZONTAL_ENCODER`

Load Kit Pattern:
1. Enable `AFFECT_ENTIRE`
2. `LOAD+PUSH_HORIZONTAL_ENCODER`

Load Kit Pattern without overwriting existing Notes:
1. Enable `AFFECT_ENTIRE`
2. `LOAD+CROSS_SCREEN+PUSH_HORIZONTAL_ENCODER`

Load Kit Pattern without scaling original File:
1. Enable `AFFECT_ENTIRE`
2. `LOAD+SCALE_CHANGE+PUSH_HORIZONTAL_ENCODER`

_Remark: If noScaling is set, Pattern will allways overwrite existing Notes and start from beginning of the Clip and not at the left Side of the Screen._

Preview of Pattern:
Press and hold `PLAY` while in Pattern-Load Menu

Save Drum Pattern:
1. Disable `AFFECT_ENTIRE`
2. Select Drum Row to save (Audition Pad)
5. `SAVE+PUSH_HORIZONTAL_ENCODER`

Load Drum Pattern:
1. Disable `AFFECT_ENTIRE`
2. Select Drum Row to save (Audition Pad)
3. `LOAD+PUSH_HORIZONTAL_ENCODER`

Load Drum Pattern without overwriting existing Notes:
1. Disable `AFFECT_ENTIRE`
2. Select Drum Row to save (Audition Pad)
3. `LOAD+CROSS_SCREEN+PUSH_HORIZONTAL_ENCODER`

Load Drum Pattern without scaling original File:
1. Disable `AFFECT_ENTIRE`
2. `LOAD+SCALE_CHANGE+PUSH_HORIZONTAL_ENCODER`

_Remark: If noScaling is set, Pattern will allways overwrite existing Notes and start from beginning of the Clip and not at the left Side of the Screen._

Preview of Pattern:
Press and hold `PLAY` while in Pattern-Load Menu

Hint:
After loading a Patter, the Pattern is also copied into Clipboard and can be pasted (`SHIFT+LEARN_INPUT+PUSH_HORIZONTAL_ENCODER`) or gently pasted (`SHIFT+CROSS_SCREEN+PUSH_HORIZONTAL_ENCODER`) as many Times as needed.


### Implemented Features
- save/load of Pattern files for Synths, Midi, CV, Kit and Drums
- Browsing and Preview of patterns (press and hold Play to audition them)
- streching/compressing of patterns
- paste Gently of Patterns
- Dynamically change Window Size based on original-Pattern size (noScaling)
- supported Note Attributes: velocity, propability, lift, iterance, fill
- If Zoomlevel of the Loading Pattern is not identical to the actual Zoom level, a Popup with the Scaling is shown (Scale-Factor for 7Seg)


### Additional Tools
Under [contrib/midi2deluge](contrib/midi2deluge), you will find two simple Tools - a WebApp and a python Script - which can generate Pattern-Files for the Deluge out of Simple Midi-Files.
Important:The Tools where tested with the [Wikipedia Midifile Sample](https://en.wikipedia.org/wiki/File:MIDI_sample.mid) and may will not work with very complex Midi-Files.


![midi2Deluge](https://github.com/user-attachments/assets/902fdcd1-58e7-4992-8c02-6c0ad063233d)



### TODOs:
- [ ] Auto-Creation of Folder Structure
