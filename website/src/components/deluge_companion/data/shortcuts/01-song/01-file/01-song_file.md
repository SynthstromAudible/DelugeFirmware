capability: SONG
capability-order: 2
sub-capability: Song (File)
sub-capability-order: 1

# New song

#OFFICIAL

```shortcut
hold(SHIFT) + press(LOAD)
```

Creates a new song.

# Delete song

#OFFICIAL

```shortcut
press(LOAD), turn(SELECT), hold(SHIFT) + press(SAVE), press(SAVE)
```

Scroll to the song to delete, then use Shift and Save/Delete to delete it. Press Save/Delete again to confirm.

# Load song (use saved tempo)

#OFFICIAL

```shortcut
press(LOAD), turn(SELECT), press(LOAD)
```

Note #1: Hold SHIFT and turn SELECT to fast-scroll songs on the display.

# Load song (keep current tempo)

#OFFICIAL

```shortcut
press(LOAD), turn(SELECT), hold(TEMPO), press(LOAD)
```

If you wish to maintain the tempo of the old song into the new one, press down on the tempo knob while you press the load button. And if tempo magnitude matching is enabled, then a multiple of the old song’s tempo may be used if it means a less drastic change to the new song’s tempo.

Or, if the Deluge is playing synced as a slave, the tempo will remain the same regardless, with the tempo magnitude matching setting taking effect if enabled.

# Load song (delay song change)

#OFFICIAL

```shortcut
press(LOAD), turn(SELECT), hold(LOAD)
```

As your new song may take up to a few seconds to load if it contains a lot of samples, there is a way to more precisely specify its launch-time, too. After selecting the song you wish to switch to, hold down the load button rather than pressing it momentarily. While holding the button down, the Deluge will load the song, indicating “DONE” when loading is complete. Then only when you release your press on the load button will the Deluge become armed to switch the song at the completion of the current loop.

# Fast scroll song list

#OFFICIAL

```shortcut
press(LOAD), hold(SHIFT) + turn(SELECT)
```

# Save song

#OFFICIAL

```shortcut
press(SAVE), turn(SELECT), press(SELECT)
```

Note: Song slots with a '.' after the name already exist, and 'OVERwrite' will flash when trying to save. A, B, C designate iterations of the same song.

# Save song, collect all samples

#OFFICIAL

```shortcut
press(SAVE), turn(SELECT), hold(SELECT), press(SELECT)
```

Saves the song, creates a folder with the same name, and copies all used sample files into it under /SONGS.
