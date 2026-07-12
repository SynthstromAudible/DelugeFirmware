capability: CLIP
capability-order: 4
sub-capability: Clip (Audio)
sub-capability-order: 1


# Create new audio clip in session view (press empty pad)

#OFFICIAL #SESSION

```shortcut
hold(GRID_UNLIT) + press(SELECT)
```

Press an empty row / pad in session view.

# Source for audio clip in session view

#OFFICIAL #COMMUNITY #SESSION

```shortcut
hold(LEARN) + press(GRID), turn(SELECT), press(SELECT)
```

```community
The community firmware adds the ability to select an internal track as the input source for an audio track.
```

# Change audio output mode to enable/disable monitoring and looping functionality in session view

#OFFICIAL #COMMUNITY #SESSION

```shortcut
press(GRID), turn(SELECT)
```

```community
The community firmware changes how input monitoring is set for audio tracks.

Player: monitoring always off

Sampler: if no recording exists or you are currently recording, monitoring will be on

Sampler: if recording exists, monitoring will be off

Looper: monitoring is always on and overdub recording is used
```

# Create new audio track in arranger view

#OFFICIAL #ARRANGER

```shortcut
hold(AUDITION) + press(SELECT)
```

Creates a new audio track from Arranger view.

# Source for audio clip in arranger view

#OFFICIAL #ARRANGER

```shortcut
hold(LEARN) + press(AUDITION), turn(SELECT), press(SELECT)
```

# Change audio output mode to enable/disable monitoring and looping functionality in arranger view

#OFFICIAL #COMMUNITY #ARRANGER

```shortcut
hold(AUDITION) + turn(SELECT)
```

```community
The community firmware changes how input monitoring is set for audio tracks.

Player: monitoring always off

Sampler: if no recording exists or you are currently recording, monitoring will be on

Sampler: if recording exists, monitoring will be off

Looper: monitoring is always on and overdub recording is used
```

# Change color of audio wave

#OFFICIAL #AUDIO

```shortcut
hold(SHIFT) + turn(Y)
```

Must be in Clip mode.

# End point / loop length (use red end point marker)

#OFFICIAL #AUDIO

```shortcut
press(GRID), press(GRID)
```

Tap a new position on the grid to shorten or lengthen the audio clip.

# Waveform start / end point

#OFFICIAL #AUDIO

```shortcut
hold(SHIFT) + press(GRID)
```

Uses the waveform view to change the audio clip start or end point.

# Adjust length - audio clip waveform

#OFFICIAL #AUDIO

```shortcut
hold(SHIFT) + turn(X)
```

The clip remains time-stretched, and shortened clips in waveform view play at a slower speed to fit the same time window. In many cases it may be better to record samples instead. Then you may need to use the tempo-from-audio shortcut afterward to apply the original tempo again.

# Set clip length to sample length

#COMMUNITY #AUDIO

```shortcut
press(Y) + press(X)
```

```community
This is a community firmware addition

Also available in Audio Clip Sound Menu > ACTIONS.
```

# Adjust audio clip length without timestretching

#COMMUNITY #AUDIO

```shortcut
hold(SHIFT) + press(Y) + turn(X)
```

```community
This is a community firmware addition
```

# Clear audio clip recording

#OFFICIAL #AUDIO

```shortcut
hold(X) + press(BACK)
```

Must be in Clip mode, not Song or Arranger, to delete the current audio recording.