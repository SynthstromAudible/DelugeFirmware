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
- Note 1: Stems are exported without Master (Song) FX applied
- Note 2: MIDI and CV Instruments and Clips are excluded from the stem export
- Note 3: Instruments and Clips that are `EMPTY` (e.g. they have no Notes or Audio Files) are excluded from the stem export
- Note 4: In Arranger View, any Instruments that are `MUTED` are excluded from the stem export

- You can also start the stem export via a new `EXPORT STEMS` menu found in the `SONG` menu accessible in Song and Arranger Views. Start the stem export by entering the `SONG\EXPORT STEMS\` menu and pressing `SELECT` on the menu item titled `START EXPORT`. It will exit out of the menu and display the export progress on the display.

### Cancelling Stem Export

- Press `BACK` to cancel Stem Export process
- When you cancel stem export, a dialog will appear on the screen asking you to confirm if you want to cancel the export. Press on the `SELECT` encoder to confirm that you want to cancel. Press `BACK` or any `PAD` on the grid to exit out of the dialog and continue with stem export process.
- Note: When you press `BACK`, the stem export still continues in the background until you confirm you want to stop.

## Recording Length and Silence

- Stems are recorded until silence is reached (mutable noise floor, ~70dB from peak) to allow for sound tails (e.g. delay, reverb) to be captured
- In terms of the length of each stem recording:
  - In Arranger, a track is played until the end of the arrangement's length is reached, at which point tails will be allowed to ring out and recording will continue until silence
  - In Song, a clip is played until the end of the longest note row with notes in it is reached, at which point tails will be allowed to ring out and recording will continue until silence

## Clip Stem Loop Points

- For clip stems, a loop point marker is saved with the stem file to mark the clip's loop length. This makes it easy to reload your stems and they will play back and loop as if you were playing those clip's on the deluge.

## Stem Export Menu

A new `EXPORT STEMS` menu has been added to the `SONG` menu accessible in Song and Arranger Views. 

This menu allows you to start a stem export and configure various settings related to the stem export.

- Start the stem export by entering the `SONG\EXPORT STEMS\` menu and pressing `SELECT` on the menu item titled `START EXPORT`. It will exit out of the menu and display the export progress on the display.

- Configure settings for the stem export by entering the `SONG\EXPORT STEMS\CONFIGURE EXPORT\` menu.
    - Currently only one configuration object has been added (`NORMALIZATION`) but more will be added in the near future.

## Videos

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/77bb8dfc-8b39-408d-8688-fc43eb7be593

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/c48d0ab8-656f-427a-99c0-f7c4076b06ca

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/c00e69c6-96a6-4b25-94ed-abcf1a0088f4
