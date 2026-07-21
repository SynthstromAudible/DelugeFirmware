capability: SYSTEM
capability-order: 1
sub-capability: System (Controls)
sub-capability-order: 2
sub-sub-capability: System (MIDI)
sub-sub-capability-order: 3

# External controller to play synth or kit

#OFFICIAL #SYNTH #KIT #MIDI #CV

```shortcut
hold(LEARN) + hold(AUDITION) + press(EXTERNAL)
```

In Synth, any audition / row pressed assigns all. In Kit, only the pressed instrument / row is learned. Deluge learns incoming MIDI note data and maps it to the clip.

# External controller to trigger clip (from song mode)

#OFFICIAL #SESSION

```shortcut
hold(LEARN) + hold(AUDITION) + press(EXTERNAL)
```

Can trigger clip mutes from Song mode via an external MIDI controller. Deluge uses MIDI notes, not CC values, for mapping.

# External controller to trigger play (from song mode)

#OFFICIAL #SESSION

```shortcut
hold(LEARN) + hold(PLAY) + press(EXTERNAL)
```

Can trigger Play from Song mode via an external MIDI controller. Deluge uses MIDI notes, not CC values, for mapping.

# External controller to trigger record (from song mode)

#OFFICIAL #SESSION

```shortcut
hold(LEARN) + hold(RECORD) + press(EXTERNAL)
```

Can trigger Record from Song mode via an external MIDI controller. Deluge uses MIDI notes, not CC values, for mapping.

# Unlearn external controller to play synth or kit

#OFFICIAL #SYNTH #KIT #MIDI #CV

```shortcut
hold(SHIFT) + hold(LEARN) + press(AUDITION)
```

Any already learned MIDI functions flash when the Learn/Input button alone is held.

# Unlearn external controller to trigger clip (from song mode)

#OFFICIAL #SESSION

```shortcut
hold(SHIFT) + hold(LEARN) + hold(AUDITION)
```

# Unlearn external controller to trigger play (from song mode)

#OFFICIAL #SESSION

```shortcut
hold(SHIFT) + hold(LEARN) + hold(PLAY)
```

# Unlearn external controller to trigger record (from song mode)

#OFFICIAL #SESSION

```shortcut
hold(SHIFT) + hold(LEARN) + hold(RECORD)
```

# External control of parameter - first select parameter

#OFFICIAL #SYNTH #KIT

```shortcut
hold(LEARN) + press(EXTERNAL)
```

Learns external control of the selected Deluge parameter.

# Unlearn external control of parameter - first select parameter

#OFFICIAL #SYNTH #KIT

```shortcut
hold(SHIFT) + hold(LEARN)
```

# Mute by external MIDI for individual kit rows - in kit mode

#OFFICIAL #KIT

```shortcut
hold(LEARN) + hold(LAUNCH) + press(EXTERNAL)
```

Learns an external MIDI note to mute individual kit instruments / rows.

# MIDI Follow Unlearn

#COMMUNITY #MIDI

```shortcut
hold(SHIFT) + hold(LEARN)
```

Unlearns a MIDI Follow channel or device from the current MIDI Follow submenu.
