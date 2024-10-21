# MIDI Device Definition Files

## Description

Added ability to rename MIDI CC's in MIDI clips. Changes are saved by Instrument (e.g. per MIDI channel). Changes can be saved to a `MIDI preset`, with the `Song`, and to a `MIDI device definition file`.

The `MIDI Device Definition File` is saved on the SD Card in the folder path: `MIDI_DEVICES/DEFINITION/`.

Updated MIDI CC name's will be displayed when pressing / turning the `Gold (Mod) Encoder` the CC is assigned to and also when automating that CC in `Automation View`

### Rename a MIDI CC

To rename a `MIDI CC`, enter `Automation View` and select the CC you wish to rename using the `Select Encoder` or using the `Grid Shortcut`.

With the `MIDI CC` selected, press `Shift` + the `Name` grid shortcut to open the `MIDI CC Renaming UI`. Enter a new name and press `Select` or `Enter`.

The new `MIDI CC` name will be immediately visible in `Automation View` and you can assign that CC to a `Gold Knob` in the MIDI clip and it will show that name when you press down on the `Gold (Mod) Encoder` and when you turn the `Gold (Mod) Encoder` to change the CC value.

### Save MIDI CC Name(s)

If you made changes to the name of a `MIDI CC`, they will need to be saved so that you can use them should you reboot the Deluge. They can be saved to the `Song`, to a `MIDI Preset` or to a `MIDI Device Definition File`.

#### Save to the Song

Simply save the song by pressing `Save`

If you had previously linked a `MIDI Device Definition File` to the Song, then the file path to that definition file will be saved to the Song.

In addition, the last edited midi cc name(s)'s will also be saved with the song in case the `MIDI Device Definition File` cannot be found.

#### Save to the MIDI Preset

While in the MIDI clip, press `Save + MIDI`

If you had previously linked a `MIDI Device Definition File` to the MIDI Instrument, then the file path to that definition file will be saved to the preset.

In addition, the last edited midi cc name(s)'s will also be saved with the preset in case the `MIDI Device Definition File` cannot be found.

#### Save to the MIDI Device Definition File

To save the `MIDI CC` labels to a `MIDI Device Definition File`, hold `Save` + Press down on either `Gold (Mod) Encoder`. It will ask you to enter a file name. Press `Select`, `Enter on the Keyboard` or `Save` to save the file.

A `MIDI Device Definition File` is saved on the SD Card in the folder path: `MIDI_DEVICES/DEFINITION/`.

### Load MIDI CC Name(s)

#### Load with the Song

When you load a song for which MIDI CC name changes were previously saved, those name changes will be re-loaded. 

If you linked the Song to a `MIDI Device Definition File`, then the changes will be loaded from that definition file.

If the Song is not linked to a definition file, then the name changes will be loaded from the Song.

#### Load from Midi Preset

When you load a MIDI preset for which MIDI cc name changes were previously saved, those name changes will be re-loaded. 

If you linked the preset to a `MIDI Device Definition File`, then the changes will be loaded from that definition file.

If the preset is not linked to a definition file, then the name changes will be loaded from the preset.

#### Load from Midi Device Definition File

To load the `MIDI CC` labels from a `MIDI Device Definition File`, hold `Load` + Press down on either `Gold (Mod) Encoder`. It will ask you to select a file. Press `Select`, `Enter on the Keyboard` or `Load` to load the file.

Saving or Loading a `MIDI Device Definition File` will link that file to the current MIDI Instrument in the current song. If you save the song or save the current midi instrument as a preset, the song file and midi instrument preset file will include a file path to the `MIDI Device Definition File`. If you re-load the song or re-load the midi instrument preset, it will load information from the linked `MIDI Device Definition File`.

### Linking / Unlinking a MIDI Device Definition File

You can unlink a Song or Midi Instrument preset from the a `MIDI Device Definition File` via the `MIDI > Device Definition (DEVI)` menu. You will need to re-save the song and/or preset to save the changes. 

You can also manually unlink the song file / preset file from the `MIDI Device Definition File` by searching for `definitionFile`. You should see `name="***"` right under it. Do not delete the name line from the preset. Instead replace the name with `name=""`

You can also use the `MIDI > Device Definition` menu as another way to link / load a `MIDI Device Definition File`. When clicking on the `File Linked (LINK)` setting, it will prompt you to select a `MIDI Device Definition File` to load. After successfully loading the file, the file name will be displayed (on OLED only) below the `File Linked` setting.

## Tips and Tricks

### Scenario: I want to load a `MIDI Device Definition File` but make changes to the CC labels afterwards

Firstly, when you load a `MIDI Device Definition File`, the song will become linked to that file. If you wish to make changes to the cc labels, you will need to either:

A) unlink the song from the `MIDI Device Definition File` and make changes to the labels and save them to the song, 

or 

B) you will need to save those label changes back to a `MIDI Device Definition File` and save the song so that it reloads from that file.

#### A) Make changes to labels without updating `MIDI Device Definition File`

TLDR steps:
```
1. Load song
2. Load label file
3. Unlink label file
4. Make changes
5. Save song
```

If you loaded a `MIDI Device Definition File` and now wish to make changes, but don't wish to save those changes to that `MIDI Device Definition File` or another definition file (e.g. you want to keep your changes specific to the Song / MIDI preset), then

As a preliminary step, after loading a `MIDI Device Definition File`, go to the `MIDI > Device Definition` menu. It will show if a `MIDI Device Definition File` is linked. Press `SELECT` on `File Linked (LINK)` to unlink that file.

Now you can go follow the steps to update CC Labels.

After updating the CC Labels, save your changes to the Song and/or to a MIDI preset.

When you re-load the Song or MIDI preset next time, it will load the CC labels from the Song or MIDI preset and not from the `MIDI Device Definition File` you started with.

#### B) Make changes to labels and update `MIDI Device Definition File`

TLDR steps:
```
1. Load song
2. Load label file
3. Make changes
4. Save label file
5. Save song
```

If you loaded a song and loaded a `MIDI Device Definition File` and now want to make changes to CC Labels, or you just started making changes as part of that song, you can save those changes back to the same or a new `MIDI Device Definition File`. 

After you have saved the changes to the `MIDI Device Definition File`, you will need to Save the song and/or MIDI preset to link the Song / Preset to that `MIDI Device Definition File`.

When you re-load the Song or MIDI preset next time, it will load the CC labels from the `MIDI Device Definition File`. The Song or MIDI preset will also keep a copy of those changes in case the `MIDI Device Definition File` is removed from your SD card.

## Midi Device Definition File Layout

The same information saved to the Midi Device Definition File is also saved to the Song / MIDI Preset with the exception of the Definition File Name which is not saved to the Definition File.

```
<midiDevice>
	<definitionFile
		name="" />
	<ccLabels
		0=""
		2=""
		3=""
		4=""
		5=""
		6=""
		7=""
		8=""
		9=""
		10=""
		11=""
		12=""
		13=""
		14=""
		15=""
		16=""
		17=""
		18=""
		19=""
		20=""
		21=""
		22=""
		23=""
		24=""
		25=""
		26=""
		27=""
		28=""
		29=""
		30=""
		31=""
		32=""
		33=""
		34=""
		35=""
		36=""
		37=""
		38=""
		39=""
		40=""
		41=""
		42=""
		43=""
		44=""
		45=""
		46=""
		47=""
		48=""
		49=""
		50=""
		51=""
		52=""
		53=""
		54=""
		55=""
		56=""
		57=""
		58=""
		59=""
		60=""
		61=""
		62=""
		63=""
		64=""
		65=""
		66=""
		67=""
		68=""
		69=""
		70=""
		71=""
		72=""
		73=""
		74=""
		75=""
		76=""
		77=""
		78=""
		79=""
		80=""
		81=""
		82=""
		83=""
		84=""
		85=""
		86=""
		87=""
		88=""
		89=""
		90=""
		91=""
		92=""
		93=""
		94=""
		95=""
		96=""
		97=""
		98=""
		99=""
		100=""
		101=""
		102=""
		103=""
		104=""
		105=""
		106=""
		107=""
		108=""
		109=""
		110=""
		111=""
		112=""
		113=""
		114=""
		115=""
		116=""
		117=""
		118=""
		119="" />
</midiDevice>
```
