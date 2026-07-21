capability: CLIP
capability-order: 5
sub-capability: Clip (Melodic)
sub-capability-order: 2
sub-sub-capability: Clip (MIDI)
sub-sub-capability-order: 2

# Setup MIDI sequencing of notes

#OFFICIAL #MIDI

```shortcut
press(MIDI), turn(SELECT)
```

Select MIDI channel 1-16 for note output.

# MIDI sequencing of parameters - first select parameter

#OFFICIAL #MIDI

```shortcut
hold(PARAMETER) + turn(SELECT), turn(PARAMETER)
```

Deluge labels do not apply. Use any button to map the function. 'None' means nothing is assigned.

# Change dial control, but keep automation

#OFFICIAL #MIDI

```shortcut
hold(PARAMETER) + hold(SELECT) + turn(SELECT)
```

Parameters with recorded automation are not shown with the normal Change MIDI Parameter command, so this command avoids writing automation in error.

# Load MIDI instrument preset

#COMMUNITY #MIDI

```shortcut
hold(LOAD) + press(MIDI)
```

Loads a MIDI instrument preset for the current MIDI clip.

# Save MIDI instrument preset

#COMMUNITY #MIDI

```shortcut
hold(SAVE) + press(MIDI)
```

Saves the current MIDI clip as a MIDI instrument preset.

# Save MIDI CC Labels to Song

#COMMUNITY #MIDI

```shortcut
press(SAVE)
```

Saves renamed MIDI CC labels into the current song.

# Save MIDI CC Labels to Preset

#COMMUNITY #MIDI

```shortcut
press(SAVE) + press(MIDI)
```

Saves renamed MIDI CC labels into the current MIDI preset.

# Save MIDI CC Labels to File

#COMMUNITY #MIDI

```shortcut
press(SAVE), turn(PARAMETER)
```

Writes the current MIDI CC labels to a MIDI device definition file.

# Load MIDI CC Labels from File

#COMMUNITY #MIDI

```shortcut
press(LOAD), turn(PARAMETER)
```

Loads MIDI CC labels from a MIDI device definition file.

# Unlink MIDI Device Definition

#COMMUNITY #MIDI

```shortcut
press(SELECT)
```

Used on the File Linked setting to unlink the current MIDI device definition file.
