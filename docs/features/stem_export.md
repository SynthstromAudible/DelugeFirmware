# Automated Stem Exporting

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/044efe71-ac49-4c4b-bd5c-92b8eb269582

## Description

Added `STEM EXPORT`, an automated process for exporting `CLIP STEMS` while in `SONG VIEW`,  `INSTRUMENT STEMS` while in `ARRANGER VIEW` and `DRUM STEMS` while in `KIT CLIP VIEW`. Press `SAVE + RECORD` to start exporting stems.

Now with one quick action you can start a stem export job, walk away from your Deluge and come back with a bunch of stems for all your clips, arranger instruments, and kit drums.

## Stems Folder

Stems get exported to a new `SAMPLES/STEMS/` folder. 

Within the `STEMS` folder, a folder with the `SONG NAME` is created for each stem export job which contains all the WAV file recordings. 

If the same SONG is exported more than once, a 5 digit number incremental number is appended to that song's folder name.

## Stem File Names

Stem's are given a meaningful name in the following format:

`ClipType_ExportType_PresetName_FileNumber.WAV`

> For example:
> 
> SYNTH_CLIP_PRESETNAME_00000.WAV
> SYNTH_TRACK_PRESETNAME_00000.WAV

For Drum exports, the stem's are given the following format for the file name:

`ClipType_ExportType_PresetName_DrumName_FileNumber.WAV`

> For example:
> 
> KIT_DRUM_003 TR-909_KICK_00000.WAV

## Shortcuts to Start/Stop Stem Exporting

### Starting Stem Export

- Hold `SAVE` + Press `RECORD` while Playback and Record are disabled to launch Stem Export process
- When the stem export is finished, a dialog will appear on the display that tells you that the stem export process has finished. Press `SELECT`, `BACK` or any `PAD` on the grid to exit the dialog.
- Note 1: Stems are exported without Master (Song) FX applied
- Note 2: MIDI and CV Instruments, Clips, and Drums are excluded from the stem export
- Note 3: Instruments, Clips and Drums that are `EMPTY` (e.g. they have no Notes or Audio Files) are excluded from the stem export
- Note 4: In Arranger View, any Instruments that are `MUTED` are excluded from the stem export
- Note 5: In Kits, any Drums that are `MUTED` are excluded from the stem export.
- `(NOT WORKING YET)` Note 6: Drum Stems are exported without Kit Affect Entire FX applied

### Cancelling Stem Export

- Press `BACK` to cancel Stem Export process
- When you cancel stem export, a dialog will appear on the screen asking you to confirm if you want to cancel the export. Press on the `SELECT` encoder to confirm that you want to cancel. Press `BACK` or any `PAD` on the grid to exit out of the dialog and continue with stem export process.
- Note: When you press `BACK`, the stem export still continues in the background until you confirm you want to stop.

## Videos

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/77bb8dfc-8b39-408d-8688-fc43eb7be593

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/c48d0ab8-656f-427a-99c0-f7c4076b06ca

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/c00e69c6-96a6-4b25-94ed-abcf1a0088f4

https://github.com/user-attachments/assets/b1fd6408-ea5b-4c6d-b5ca-09f73c62ab69
