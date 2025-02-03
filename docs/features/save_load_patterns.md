Added availability to save / load Pattern-Files to Files. A Pattern represents all Notes of the actual Deluge Screen including Velocity, Probability, Lift, Iterance and Fill. The Patterns can be either of Type melodic Instrument (Synt, Midi, CV) or rhythmic Instrument (Kit, Drum).

### Prerequisits:
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


### Implemented Features:
- save/load of Pattern files for Synths, Midi, CV, Kit and Drums
- Browsing and Preview of patterns (press and hold Play to audition them)
- streching/compressing of patterns
- paste Gently of Patterns
- Dynamically change Window Size based on original-Pattern size (noScaling)
- supported Note Attributes: velocity, propability, lift, iterance, fill
- If Zoomlevel of the Loading Pattern is not identical to the actual Zoom level, a Popup with the Scaling is shown (Scale-Factor for 7Seg)


### Additional Tools:
Under [contrib/midi2deluge](contrib/midi2deluge), you will find two simple Tools - a WebApp and a python Script - which can generate Pattern-Files for the Deluge out of Simple Midi-Files.
Important:The Tools where tested with the [Wikipedia Midifile Sample](https://en.wikipedia.org/wiki/File:MIDI_sample.mid) and may will not work with very complex Midi-Files.


![midi2Deluge](https://github.com/user-attachments/assets/902fdcd1-58e7-4992-8c02-6c0ad063233d)



### TODOs:
- [ ] Auto-Creation of Folder Structure
