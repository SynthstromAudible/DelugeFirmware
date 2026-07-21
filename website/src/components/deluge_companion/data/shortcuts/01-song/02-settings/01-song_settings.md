capability: SONG
capability-order: 2
sub-capability: Song (Settings)
sub-capability-order: 2

# Change tempo (coarse)

#OFFICIAL #COMMUNITY

```shortcut
turn(TEMPO)
```

```community
Official: Turn the tempo encoder to change tempo by approximately +/- 4 increments.

Community (default): Turn the tempo encoder to change tempo by +/-1 increments.

Note: You can change the default behaviour in the community firmware.
```

# Change tempo (fine)

#OFFICIAL #COMMUNITY

```shortcut
hold(TEMPO) + turn(TEMPO)
```

```community
Official: Press and turn the tempo encoder to change tempo by +/- 1 increments.

Community (default): Press and turn the tempo encoder to change tempo by approximately +/- 4 increments.

Note: You can change the default behaviour in the community firmware.
```

# Change tempo (secret power up)

#OFFICIAL

```shortcut
hold(LEARN) + turn(TEMPO)
```

Use this to increase tempo from 0 to FAST, FAST!

# Grab tempo from existing audio clip

#OFFICIAL #SESSION

```shortcut
hold(TEMPO) + press(GRID_LIT)
```

# Nudge MIDI clock

#OFFICIAL

```shortcut
hold(X) + turn(TEMPO)
```

# Sync scaling for unusual time signitures

#OFFICIAL #SYNTH #KIT #MIDI #CV

```shortcut
press(SYNC)
```

Sync-scaling is tied to the length of one clip in a song, and tells the Deluge that that clip’s length should be squeezed into 1 bar of incoming MIDI beat clock (or 2 bars, or 4 or 8 bars, depending on how long the clip is; the Deluge will use whatever magnitude of sync-scaling causes the smallest change in tempo).

To enable sync-scaling, enter clip view for the clip that you wish to tie sync-scaling to. Press the sync-scaling button to set this clip as the sync-scaling clip. The button will blink continuously, indicating that this is the sync-scaling clip. If you leave clip view for this clip, the button will remain illuminated, but will cease to blink, indicating that sync-scaling is active, but not for this clip. Sync-scaling may be switched off at any time by pressing the sync-scaling button.

Even while the Deluge is playing synced as a slave, sync-scaling may be switched on or off, and the sync-scaling clip may have its length changed. Despite any such changes, the Deluge will keep the sync-scaling clip playing in time to the syncing master. It will also attempt to keep all other clips in time; this works best if the other clips are of the same time signature as the sync-scaling clip (that is, their lengths are the same, or half our double, or 4 times shorter or longer, etc.)

# Swing

#OFFICIAL

```shortcut
hold(SHIFT) + turn(TEMPO)
```

Swing interval is 1/16th notes by default, but can be adjusted in the settings menu.
