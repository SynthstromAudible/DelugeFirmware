# Chromatic sample - same sample, different pitches (on new synth)

#SYNTH

```shortcut
hold(AUDITION) + press(LOAD), press(SELECT), turn(SELECT), press(SELECT)
```

Press Select once to load as a chromatic sample: the same sample is used at different pitches.

Deluge detects the pitch of provided samples regardless of filename, though for multi-samples it is best to order samples on the SD card from low to high where possible.

# Basic sample - load sample with no pitch detection

#SYNTH

```shortcut
hold(AUDITION) + press(LOAD), press(SELECT), turn(SELECT), hold(SELECT) + menu(NONE), press(SELECT)
```

Hold Select and turn to choose BASIc, which loads a sample with no pitch detection.

# Multi-sampling

#SYNTH

```shortcut
hold(AUDITION) + press(LOAD), press(SELECT), turn(SELECT), hold(SELECT) + menu(NONE), press(SELECT)
```

Hold Select and turn to choose MULTi for multi-sampling. Scroll through folders to select a sample or parent folder for multi-samples.

# Single cycle waveforms

#SYNTH

```shortcut
hold(AUDITION) + press(LOAD), press(SELECT), turn(SELECT), hold(SELECT) + menu(NONE), press(SELECT)
```

Single-cycle waveforms should be under 20ms. Deluge automatically transposes to C and sets loop mode. Use the SINGle option to force samples into this mode.

# External sound source

#SYNTH

```shortcut
hold(SHIFT) + press(GRID), turn(SELECT), menu(NONE), press(PLAY)
```

Use an external input as an oscillator source by selecting IN. You can pitch-shift around the source or play chords with several sequenced notes at once. A stereo-to-mono adapter can feed one source to OSC1 INL and another to OSC2 INR.

# Record to arranger - live record parameter changes and MIDI notes (RECORD to end)

#SONG

```shortcut
hold(RECORD) + press(SONG)
```

Both Record and Song buttons flash while active. Play or Song stops recording. While this mode is active you cannot change to Clip or Arranger mode.

Live recording can capture parameter changes, MIDI notes, and parameter changes on existing MIDI-learned clips.

# Append recording to existing arrangement (from arrangement mode; RECORD to end)

#ARRANGER

```shortcut
turn(X), press(SONG), hold(RECORD) + press(SONG)
```

Move the play bar to the point where you want to append new live recording to the arranger. Everything to the right of the play bar is deleted and the new recording is appended. You can undo if a mistake is made.

# Waveform edit view - change start / end / loop

#SYNTH #KIT

```shortcut
hold(SHIFT) + press(GRID)
```

# Zoom in and out

#WAVEFORM

```shortcut
hold(X) + turn(X)
```

# Move along waveform

#WAVEFORM

```shortcut
turn(X)
```

# Change start

#WAVEFORM

```shortcut
press(WAVE_START), press(GRID)
```

Click anywhere on the green start bar until it flashes, then click the target column to move it.

# Change end

#WAVEFORM

```shortcut
press(WAVE_END), press(GRID)
```

Click anywhere on the red end bar until it flashes, then click the target column to move it.

# Create loop start point

#WAVEFORM

```shortcut
hold(WAVE_START) + press(GRID)
```

Hold the green start bar and click on the grid to the right to create the loop start point. The loop start bar can then be moved like the start / end bars.

# Delete loop start point

#WAVEFORM

```shortcut
hold(WAVE_LOOP_START) + press(WAVE_START)
```

Hold anywhere on the blue loop-start bar and press the green start bar. The loop point disappears.

# Create loop end point

#WAVEFORM

```shortcut
hold(WAVE_END) + press(GRID)
```

Hold the red end bar and click on the grid to the left to create the loop end point. The loop end bar can then be moved like the start / end bars.

# Delete loop end point

#WAVEFORM

```shortcut
hold(WAVE_LOOP_END) + press(WAVE_END)
```

Hold anywhere on the purple loop-end bar and press the red end bar. The loop point disappears.

# Waveform Loop Lock

#WAVEFORM

```shortcut
hold(WAVE_LOOP_START) + press(WAVE_LOOP_END)
```

Locks loop points together. Repeat to unlock.
