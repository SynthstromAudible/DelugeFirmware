# Automated Audio Exporting

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/044efe71-ac49-4c4b-bd5c-92b8eb269582

## Description

Added `AUDIO EXPORT`, an automated process for exporting `CLIP's` while in `SONG VIEW` and `TRACK's` while in `ARRANGER VIEW`. Press `SAVE + RECORD` to start exporting.

Now with one quick action you can start an export job, walk away from your Deluge and come back to a collection of renders from all your clips and arranger tracks.

## Exports Folder

Audio exports are placed in a new `SAMPLES/EXPORTS/` folder.

Within the `EXPORTS` folder, a folder with the `SONG NAME` is created if it does not already exist when exporting. Unsaved songs are saved with the song name `UNSAVED`. Thus, you will have a new folder named `SAMPLES/EXPORTS/SONG NAME/`.

Within the `SONG NAME` folder, a folder for the type of export (e.g. `CLIPS` or `TRACKS`) is created for each export job which contains all the WAV file recordings.

If the same SONG and EXPORT TYPE is exported more than once, a 2 digit number incremental number is appended to that export type's folder name (e.g. TRACKS## or CLIPS##).

## File Names

Exported files are given a meaningful name in the following format:

`ClipType_ExportType_PresetName_Tempo_Root Note-Scale Name_FileNumber.WAV`

For example:

```
SYNTH_CLIP_PRESETNAME_###BPM_Root Note-Scale Name_000.WAV
SYNTH_TRACK_PRESETNAME_###BPM_Root Note-Scale Name_000.WAV
MIXDOWN_###BPM_Root Note-Scale Name.WAV
```

## Shortcuts to Start/Stop Exporting

### Start Export

- Hold `SAVE` + Press `RECORD` while Playback and Record are disabled to launch the export process
- When the export is finished, a dialog will appear on the display that tells you that the export process has finished. Press `SELECT`, `BACK` or any `PAD` on the grid to exit the dialog.
- Note 1: Master (Song) FX are excluded from the export by default (but can be included - see below)
- Note 2: MIDI and CV Tracks and Clips are excluded from the export
- Note 3: Tracks and Clips that are `EMPTY` (e.g. they have no Notes or Audio Files) are excluded from the export
- Note 4: In Arranger View, any Tracks that are `MUTED` are excluded from the export

- You can also start the export via a new `EXPORT AUDIO` menu found in the `SONG` menu accessible in Song and Arranger Views. Start the export by entering the `SONG\EXPORT AUDIO\` menu and pressing `SELECT` on the menu item titled `START EXPORT`. It will exit out of the menu and display the export progress on the display.

### Cancel Export

- Press `BACK` to cancel the export process
- When you cancel exporting, a dialog will appear on the screen asking you for confirmation. Press on the `SELECT` encoder to confirm that you want to cancel. Press `BACK` to exit out of the dialog and continue with export process.
- Note: The export still continues in the background until you confirm you want to stop.

## Recording Parameters

### Recording Length
- In terms of the length of each rendered audio file:
  - In Arranger, a track is played until the end of the arrangement's length is reached.
  - In Song, a clip is played until the end of the longest note row with notes in it is reached.
  - If `EXPORT TO SILENCE` is enabled, tails will be allowed to ring out and recording will continue past the track or clip length until silence is reached (see below).

### Silence
- By default, files are rendered until silence is reached (mutable noise floor, ~70dB from peak) to allow for sound tails (e.g. delay, reverb) to be captured
  - This can be turned off in the export configuration menu located at: `SONG\EXPORT AUDIO\CONFIGURE EXPORT\EXPORT TO SILENCE`
- If silence is not reached within 60 seconds of playback stopping, then the recording will stop automatically as a safety precaution.

### Normalization
- By default, normalization is off. Normalization sets the peak of the renders to be at 0dB (as loud as possible without distorting).
  - Normalization can be turned on in the export configuration menu located at: `SONG\EXPORT AUDIO\CONFIGURE EXPORT\NORMALIZATION`

### Song FX
- By default, song FX are excluded (these are the master FX in Song and Arranger Views when Affect Entire is enabled). They can be included in the export configuration menu located at: `SONG\EXPORT AUDIO\CONFIGURE EXPORT\SONG FX`

### Clip Loop Points

- For clip exports, a loop point marker is saved with the exported file to mark the clip's loop length. This makes it easy to load your rendered files so they will play back and loop properly in a DAW.

### Offline Rendering
- By default, offline Rendering is enabled. Offline rendering enables you to render and export faster than if you recorded playback using live audio (e.g. resampling/online rendering). There are still improvements to be made to make offline rendering even faster, but it is significantly fast as is!
  - Offline rendering can be turned off in the export configuration menu located at: `SONG\EXPORT AUDIO\CONFIGURE EXPORT\OFFLINE RENDERING`

## Audio Export Menu

A new `EXPORT AUDIO` menu has been added to the `SONG` menu accessible in Song and Arranger Views.

This menu allows you to start an export and configure various settings related to the export.

- Start the export by entering the `SONG\EXPORT AUDIO\` menu and pressing `SELECT` on the menu item titled `START EXPORT`. It will exit out of the menu and display the export progress on the display.

- Configure settings for the export by entering the `SONG\EXPORT AUDIO\CONFIGURE EXPORT\` menu.
    - You can currently configure the following:
      - `NORMALIZATION`: Normalization sets the peak of the render to be at 0dB (as loud as possible without distorting).
      - `EXPORT TO SILENCE`: Exports are rendered until silence is reached (mutable noise floor, ~70dB from peak) to allow for sound tails (e.g. delay, reverb) to be captured.
      - `SONG FX`: Exports are rendered with or without Song FX applied.
      - `OFFLINE RENDERING`: Exports are rendered offline. You will not hear any audio playback, as exports are rendered at a faster than real-time basis.

## Troubleshooting

### I have a track that won't export

#### Scenario: Track(s) get exported with only 5 seconds of audio

Several users reported that their tracks were not getting exported properly as they included only 5 seconds of audio.

All of these users were using offline rendering.

All of these users had complex arrangements.

Solution:

For heavy arrangements, if you encounter the above issue, we recommend turning off `Offline Rendering` in the `SONG\EXPORT AUDIO\CONFIGURE EXPORT\` menu.

The problem is due to memory filling up faster when using `Offline Rendering` compared to `Online (Live) Rendering`. We hope to find a solution for this problem as soon as possible.

#### Scenario: Special characters in the track name

One user reported that they were unable to export a track even though the export process indicated that the track had been exported.

Solution:

Check that the track name doesn't have any special characters. In this case, the user had a track called << Organ >>. Removing the characters "<< >>" and just naming the track "Organ" allowed the track to be exported.

### I have a track that takes much longer to export than others

#### Scenario: The track has an exceptionally long tail that doesn't not drop to silence

If you are using the `EXPORT TO SILENCE` feature, it may take longer for your track to export because the renderer is waiting for your track to become silent. Thus, you will see that playback has turned off but the record button continues to blink rapidly because it is still waiting for silence before stopping the render.

As a safety measure, if your track does not become silent within 60 seconds of playback stopping, then the render will stop automatically.

If you do not want to wait potentially 60 seconds, you will need to press `BACK` to cancel the export and save what has been recorded up to that point.

If you want to continue using the `EXPORT TO SILENCE` feature, check what might be contributing to the exceptionally long tails (e.g. delay, reverb, release, compressor). Use the `VU Meter` to check the levels when you start and stop a track. If the `VU Meter` gets stuck with pads that do not turn off, then it is an indication that you have exceptionally long tails.

### The tails of one export "bleeds" into the beginning of another

If you are not using the `EXPORT TO SILENCE` feature, it is possible that while exporting, the audio from the previous render may bleed into the start of the next render.

Other than using the `EXPORT TO SILENCE` feature, we do not have a solution for this yet. Ideally if we can find a way to cut any sustaining audio from the previous track / clip rendered that would ensure that no bleed occurs, however we have not found a way to do this yet.

## Videos

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/77bb8dfc-8b39-408d-8688-fc43eb7be593

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/c48d0ab8-656f-427a-99c0-f7c4076b06ca

https://github.com/SynthstromAudible/DelugeFirmware/assets/138174805/c00e69c6-96a6-4b25-94ed-abcf1a0088f4
