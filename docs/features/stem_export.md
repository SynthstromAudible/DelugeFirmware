# Automated Stem Exporting

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/044efe71-ac49-4c4b-bd5c-92b8eb269582

## Description

Added `STEM EXPORT`, an automated process for exporting `CLIP STEMS` while in `SONG VIEW` and `INSTRUMENT STEMS` while in `ARRANGER VIEW`. Press `SAVE + RECORD` to start exporting stems.

Now with one quick action you can start a stem export job, walk away from your Deluge and come back with a bunch of stems for all your clips and arranger instruments.

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

## Shortcuts to Start/Stop Stem Exporting

### Starting Stem Export

- Hold `SAVE` + Press `RECORD` while Playback and Record are disabled to launch Stem Export process
- When the stem export is finished, a dialog will appear on the display that tells you that the stem export process has finished. Press `SELECT`, `BACK` or any `PAD` on the grid to exit the dialog.
- Note: In Arranger View, any Instruments that are `MUTED` will be excluded from the stem export.

### Cancelling Stem Export

- Press `BACK` to cancel Stem Export process
- When you cancel stem export, a dialog will appear on the screen asking you to confirm if you want to cancel the export. Press on the `SELECT` encoder to confirm that you want to cancel. Press `BACK` or any `PAD` on the grid to exit out of the dialog and continue with stem export process.
- Note: When you press `BACK`, the stem export still continues in the background until you confirm you want to stop.

## Videos

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/77bb8dfc-8b39-408d-8688-fc43eb7be593

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/c48d0ab8-656f-427a-99c0-f7c4076b06ca

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/c00e69c6-96a6-4b25-94ed-abcf1a0088f4
