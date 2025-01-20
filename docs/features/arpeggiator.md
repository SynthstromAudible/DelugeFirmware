# Arpeggiator

The arpeggiator has been completely redesigned from scratch. It can be used on Synth clips, Midi clips, and on any kind of
Kit rows (Sound, Midi and CV).

## Arpeggiator Modes (or Presets)

Arpeggiator Modes were used to set the arpeggiator to play in different ways. On the official firmware you just had the
possibility to select 4 modes. Now you can customize much more deeply the arpeggiator behaviour, with independent direction
settings for Note and for Octave. That's why we changed the `Arp Mode` grid shortcut to an `Arp Preset` shortcut.
A preset sets at the same time 3 parameters: `Arp Mode`, `Octave Mode` and `Note Mode`. The available presets are:
  - `Off` will disable arpeggiator.
  - `Up` will setup Mode to `Arpeggiator`, Octave Mode to `Up` and Note Mode to `Up`. This is equivalent to `Up` in official firmware.
  - `Down` will setup Mode to `Arpeggiator`, Octave Mode to `Down` and Note Mode to `Down`. This is equivalent to `Down` in official firmware.
  - `Both` will setup Mode to `Arpeggiator`, Octave Mode to `Alternate` and Note Mode to `Up`. This is equivalent to `Both` in official firmware.
  - `Random` will setup Mode to `Arpeggiator`, Octave Mode to `Random` and Note Mode to `Random`. This is equivalent to `Random` in official firmware.
  - `Walk` will setup Mode to `Arpeggiator`, Octave Mode to `Alternate` and Note Mode to `Walk2`.
  - `Custom` will setup Mode to `Arpeggiator`, and enter a submenu to let you edit `Octave Mode` and `Note Mode`.

## Arpeggiator Parameters (submenus)

Now the menu options are the following:

### Mode
- **`Enabled (ON)`**: enables the arpeggiator, that is, it internally sets `Mode` to `Arpeggiator`. In the future, this will
  be replaced by the `Mode` setting, which will have other modes other than `Arpeggiator`.

### Basic Parameters (BASI)
The following parameters define the basic parameters of the arpeggiator:
- **`Gate`**: This changes the length of the note, as a relative percentage of the time window set between this note and the next.
  A gate of 25 will make the notes last for 50% of the available time window. It can be **automated** and **learned** to
  golden knobs and MIDI CC (not automatable in MIDI clips).
- **`Sync`**: allows you to set the synced rate between `Off`, `2-Bar`, ..., `128th` notes, etc.
- **`Rate`**: manually sets a sync rate when `Sync` is set to `Off`. It can be **automated** and **learned** to
  golden knobs and MIDI CC (not automatable in MIDI clips).

### Pattern (PATT)
The following parameters will define the pattern of the arpeggio, by ordering the notes you have played, adding extra octaves,
setting how many times they are repeated, and setting the direction in which they are walked:
- **`Octaves (OCTA)`**: sets the number of octaves
- **`Octave Mode (OMOD)`**:
    - `Up (UP)` will walk the octaves up.
    - `Down (DOWN)` will walk the octaves down.
    - `Up & Down (UPDN)` will walk the octaves up and down, repeating the highest and lowest octaves.
    - `Alternate (ALT)`  will walk the octaves up, and then down reversing the Notes pattern (without
      repeating notes). Tip: Octave Mode set to Alternate and Note Mode set to Up is equivalent to
      the old `Both` mode.
    - `Random (RAND)` will choose a random octave every time the Notes pattern has played.
      Tip: Set also Note Mode to Random to have the equivalent to the old `Random` mode.
- **`Chord Simulator (CSIM)`**: is only available for Kit Rows. This allows you to simulate a chord so you
  can use `Note Mode` on the Kit Row. It will take the base pitch of the sample and add extra "notes" to
  the sequence by pitching the sample up several semitones to form a chord.
- **`Note Mode (NMOD)`**:
    - `Up (UP)` will walk the notes up.
    - `Down (DOWN)` will walk the notes down. Tip: this mode also works in conjunction with Octave Mode
      Alternate, which will walk all the notes and octaves all the way down, and then up reversing it.
    - `Up & Down (UPDN)` will walk the notes up and down, repeating the highest and lowest notes.
    - `Random (RAND)` will choose a random note each time. If the Octave Mode is set to something
      different than Random, then the pattern will play, in the same octave, the same number of random
      notes as notes are in the held chord and then move to a different octave based on the Octave Mode.
      Tip: Set also Octave Mode to Random to have the equivalent to the old `Random` mode.
    - `Walk1 (WALK1)` is the "slow" walk and next note is selected this way:
        - 30% of probability to walk the sequence a step in reverse.
        - 30% of probability to just repeat the same step of the sequence.
        - 40% of probability to walk the sequence a step forward (as normal).
    - `Walk2 (WALK2)` is the "normal" walk and next note is selected this way:
        - 25% of probability to walk the sequence a step in reverse.
        - 25% of probability to just repeat the same step of the sequence.
        - 50% of probability to walk the sequence a step forward (as normal).
    - `Walk3 (WALK3)` is the "fast" walk and next note is selected this way:
        - 20% of probability to walk the sequence a step in reverse.
        - 20% of probability to just repeat the same step of the sequence.
        - 60% of probability to walk the sequence a step forward (as normal).
    - `Played Order (PLAY)` will walk the notes in the same order that they were played. Tip: this mode
      also works in conjunction with Octave Mode Alternate, which will walk all the notes and octaves
      all the way up (with notes as played), and then down reversing the order of played notes.
      Note: this mode is not available for Kit Rows.
    - `Pattern (PATT)` each time you select this `Note Mode` on the menu it will generate a new pattern for you.
      The pattern is like if you had selected `Played Order` and the Deluge had decided the order for you.
      Tip: as this mode is the last selectable one, if you try to scroll past the end of the menu, the Deluge
      will interpret it as a re-selection of the `Pattern` mode and so it will generate a new pattern for you.
      Note: the pattern is saved to the song so you can recreate it after reloading the song.
      Note: this mode is not available for Kit Rows.
- **`Step Repeat (REPE)`**: allows you to set how many times the same note in the pattern is repeated
  before moving on to the next one.
- **`Rhythm (RHYT)`**: This parameter will play silences in some of the steps. This menu option shows zeroes and dashes,
  "0" means "play note", and "-" means "don't play note" (or play a silence). It can be **automated** and **learned** to
  golden knobs and MIDI CC (not automatable in MIDI clips). The available options are:
  <details>
  <summary>Rhythm Options</summary>
    <ul>
      <li> 0: None (play all notes)</li>
      <li> 1: 0--</li>
      <li> 2: 00-</li>
      <li> 3: 0-0</li>
      <li> 4: 0-00</li>
      <li> 5: 00--</li>
      <li> 6: 000-</li>
      <li> 7: 0--0</li>
      <li> 8: 00-0</li>
      <li> 9: 0----</li>
      <li>10: 0-000</li>
      <li>11: 00---</li>
      <li>12: 0000-</li>
      <li>13: 0---0</li>
      <li>14: 00-00</li>
      <li>15: 0-0--</li>
      <li>16: 000-0</li>
      <li>17: 0--0-</li>
      <li>18: 0--00</li>
      <li>19: 000--</li>
      <li>20: 00--0</li>
      <li>21: 0-00-</li>
      <li>22: 00-0-</li>
      <li>23: 0-0-0</li>
      <li>24: 0-----</li>
      <li>25: 0-0000</li>
      <li>26: 00----</li>
      <li>27: 00000-</li>
      <li>28: 0----0</li>
      <li>29: 00-000</li>
      <li>30: 0-0---</li>
      <li>31: 0000-0</li>
      <li>32: 0---0-</li>
      <li>33: 000-00</li>
      <li>34: 0--000</li>
      <li>35: 000---</li>
      <li>36: 0000--</li>
      <li>37: 0---00</li>
      <li>38: 00--00</li>
      <li>39: 0-00--</li>
      <li>40: 000--0</li>
      <li>41: 0--00-</li>
      <li>42: 0-0-00</li>
      <li>43: 00-0--</li>
      <li>44: 000-0-</li>
      <li>45: 0--0-0</li>
      <li>46: 0-000-</li>
      <li>47: 00---0</li>
      <li>48: 00--0-</li>
      <li>49: 0-0--0</li>
      <li>50: 00-0-0</li>
    </ul>
  </details>
- **`Sequence Length (LENG)`**: It defines the length of the pattern in number of steps.
  - If set to zero, the arpeggiator pattern will play fully without restrictions.
  - If set to a value higher than zero, the pattern will play up to the set number of steps, and then
    reset itself to start from the beginning. Tip: You can use this in combination with the Rhythm
    parameter to create longer and more complex rhythm patterns.
  - It can be **automated** and **learned** to golden knobs and MIDI CC (not automatable in MIDI clips).

### Randomizer (RAND)
The following parameters are all **automatable** and **learnable** to golden knobs and MIDI CC (not in MIDI clips), and they define how probable a value is to change on each step:
- **`Lock Randomizer (LOCK)`**: this flag will lock the current set of randomized values for the arpeggiator, so the sequence has a repeatable pattern.
  - Tip 1: In case you have drone notes, make use of the `Sequence Length` parameter to further adjust the repeated sequence.
  - Tip 2: If you want to re-roll the dice for a parameter, slightly change its value and a new set of random values will be
  generated and locked.
  - Tip 3: If you want some parameters to be locked, but not others, add some automation variation in the clip to those,
  so the dice is re-rolled and hence you always get fresh new random values for them, while the others have fixed values.
  Note: you can find this parameter also at the root level of the sound menu, under `Randomizer`, because this flag is also applied for
  both sequenced notes and arpeggiated notes.
- **`Octave Spread (OCTA)`**: The note will get a change in pitch of a random amount of octaves, going from 0 up to a maximum of +3 octaves.
- **`Gate Spread (GATE)`**: The gate of the arp step will get a random **positive** or **negative** deviation from the base gate.
- **`Velocity Spread (VELO)`**: The velocity of the arp step will get a random **positive** or **negative** deviation from the base velocity.
  Note: you can find this parameter also at the root level of the sound menu, under `Randomizer`, because this parameter affects
  both sequenced notes and arpeggiated notes.
- **`Ratchet Amount (RATC)`**: A ratchet is when a note repeats itself several times in the same time interval
  that the original note has to play. This will set the maximum number of notes that a single step can have (each step will
  pick a random number of ratchet notes between 1 and this maximum value set). Ratchet bursts can contain 2, 4 or 8 notes.
  By increasing this parameter, it is more likely that bigger ratchet bursts can occur.
- **`Ratchet Probability (RPRO)`**: This sets how likely a step is to be ratcheted. The ratchet amount is randomized on each step
  between 1 and the maximum value set with the `Ratchet Amount` parameter.
- **`Chord Polyphony (CHRD)`**: is not available for Kit Rows. This will set the maximum number of notes that will play at the same time
  when a step is played as a chord. Increase parameter `Chord Probability` to make some steps play a chord instead
  of a single note. Values range from no chord, to 5th, to triad (3rd + 5th), to seventh chord (3rd + 5th + 7th) as the maximum size of
  a chord. By increasing this parameter, it is more likely that bigger chords can occur.
- **`Chord Probability (CPRO)`**: is not available for Kit Rows. This paramater will allow you to control the chance of a note to play
  as a chord. The size of the chord is determined by the `Chord Polyphony` parameter.
- **`Note Probability (NOTE)`**: It applies a probability for notes to play or not (after Rhythm has been evalutated,
  that is, only for `note` steps, not for `silent` steps).
  Note: you can find this parameter also at the root level of the sound menu, under `Randomizer`, because this parameter affects
  both sequenced notes and arpeggiated notes.
- **`Bass Probability (BASS)`**: It applies a chance of replacing the current note to be played with the lowest note of arpeggiator
  pattern. The higher the value, the more likely that the note played is the bass note of the arpeggiated chord.
  This can be used as a performance tool to open up or close the arpeggio's pitch range.
- **`Reverse Probability (RVRS)`**: It applies a chance of inverting the `Reverse` sample setting just for the current note to be played.
  This probability only affects the oscillator whose type is set to `Sample`.
  Note: you can find this parameter also at the root level of the sound menu, under `Randomizer`, because this parameter affects
  both sequenced notes and arpeggiated notes.

The randomizer's parameters that are also available for non-arpeggiated notes are:
- `Lock Randomizer (LOCK)`
- `Velocity Spread (VELO)`
- `Note Probability (NOTE)`
- `Reverse Probability (RVRS)`

### MPE
This submenu contains parameters that are useful if you have an MPE controller connected to the Deluge:
- **`Velocity`**: It will allow you to control the velocity of each new arpeggiated note with an MPE expression parameter.
  - `Off`: This disables control of velocity with MPE.
  - `Aftertouch (AFTE)`: The pressure applied to the key sets the velocity of the note.
  - `MPE Y (Y)`: The Y position on the MPE controller sets the velocity of the note.
