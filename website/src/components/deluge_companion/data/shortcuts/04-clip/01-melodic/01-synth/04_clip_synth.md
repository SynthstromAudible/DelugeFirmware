capability: CLIP
capability-order: 5
sub-capability: Clip (Melodic)
sub-capability-order: 2
sub-sub-capability: Clip (Synth)
sub-sub-capability-order: 1

# Create new synth

#OFFICIAL #SYNTH

```shortcut
hold(SHIFT) + press(SYNTH)
```

# View synth preset browser

#OFFICIAL #SYNTH

```shortcut
hold(LOAD) + press(SYNTH)
```

# Change synth preset

#OFFICIAL #SYNTH

```shortcut
turn(SELECT)
```

Changes the current synth preset.

# Clone synth preset

#OFFICIAL #SYNTH

```shortcut
hold(LOAD) + press(SYNTH), turn(SELECT), hold(LOAD) + menu(NONE), press(SELECT)
```

A single preset can only appear in one active clip, so clone the original preset for multiple instances in the same song. It is good practice to clone first when tweaking to avoid affecting interdependent songs.

# Save synth preset

#OFFICIAL #SYNTH

```shortcut
hold(SAVE) + press(SYNTH), turn(SELECT), press(SELECT)
```

# Create DX7 Synth

#COMMUNITY #SYNTH

```shortcut
hold(PARAMBUTTON7) + press(SYNTH)
```

With the DX7 community feature enabled, this creates a new DX7 synth.

# Chromatic sample - same sample, different pitches (on new synth)

#OFFICIAL #SYNTH

```shortcut
hold(AUDITION) + press(LOAD), press(SELECT), turn(SELECT), press(SELECT)
```

Press Select once to load as a chromatic sample: the same sample is used at different pitches.

Deluge detects the pitch of provided samples regardless of filename, though for multi-samples it is best to order samples on the SD card from low to high where possible.

# Basic sample - load sample with no pitch detection

#OFFICIAL #SYNTH

```shortcut
hold(AUDITION) + press(LOAD), press(SELECT), turn(SELECT), hold(SELECT) + menu(NONE), press(SELECT)
```

Hold Select and turn to choose BASIc, which loads a sample with no pitch detection.

# Multi-sampling

#OFFICIAL #SYNTH

```shortcut
hold(AUDITION) + press(LOAD), press(SELECT), turn(SELECT), hold(SELECT) + menu(NONE), press(SELECT)
```

Scroll through folders to select a sample or parent folder for multi-samples. Hold Select and turn to choose MULTi for multi-sampling.

# Single cycle waveforms

#OFFICIAL #SYNTH

```shortcut
hold(AUDITION) + press(LOAD), press(SELECT), turn(SELECT), hold(SELECT) + menu(NONE), press(SELECT)
```

Single-cycle waveforms should be under 20ms. Deluge automatically transposes to C and sets loop mode. Use the SINGle option to force samples into this mode.

# External sound source

#OFFICIAL #SYNTH

```shortcut
hold(SHIFT) + press(GRID), turn(SELECT), menu(NONE), press(PLAY)
```

Use an external input as an oscillator source by selecting IN. You can pitch-shift around the source or play chords with several sequenced notes at once. A stereo-to-mono adapter can feed one source to OSC1 INL and another to OSC2 INR.
