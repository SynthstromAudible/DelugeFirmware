# Arpeggiator

The arpeggiator has been completely redesigned from scratch. It can be used on Synth clips, Midi clips, and on any kind of
Kit rows (Sound, Midi and CV).

## Arpeggiator Modes

Arpeggiator Modes were used to set the arpeggiator to play in different ways. On the official firmware you just had the
possibility to select 4 modes. Now you can customize much more deeply the arpeggiator behaviour, with independent direction
settings for Note and for Octave. That's why we changed the `Arp Mode` grid shortcut to an `Arp Preset` shortcut.
A preset sets at the same time 3 parameters: `Arp Mode`, `Octave Mode` and `Note Mode`. The available presets are:
  - `Off` will disable arpeggiator.
  - `Up` will setup Mode to `Arpeggiator`, Octave Mode to `Up` and Note Mode to `Up`. This is equivalent to `Up` in official firmware.
  - `Down` will setup Mode to `Arpeggiator`, Octave Mode to `Down` and Note Mode to `Down`. This is equivalent to `Down` in official firmware.
  - `Both` will setup Mode to `Arpeggiator`, Octave Mode to `Alternate` and Note Mode to `Up`. This is equivalent to `Both` in official firmware.
  - `Random` will setup Mode to `Arpeggiator`, Octave Mode to `Random` and Note Mode to `Random`. This is equivalent to `Random` in official firmware.
  - `Custom` will setup Mode to `Arpeggiator`, and enter a submenu to let you edit `Octave Mode` and `Note Mode`.

## Arpeggiator Parameters

Now the menu options are the following:

### Mode
- **`Enabled (ON)`**: enables the arpeggiator, that is, it internally sets `Mode` to `Arpeggiator`. In the future, this will
  be replaced by the `Mode` setting, which will have other modes other than `Arpeggiator`.

### Rate
The following parameters define the rate, that is, the speed of the notes:
- **`Sync`**: allows you to set the synced rate between `Off`, `2-Bar`, ..., `128th` notes.
- **`Rate`**: manually sets a sync rate when `Sync` is set to `Off`.

### Pattern
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
- **`Note Mode (NMOD)`**:
    - `Up (UP)` will walk the notes up.
    - `Down (DOWN)` will walk the notes down. Tip: this mode also works in conjunction with Octave Mode
      Alternate, which will walk all the notes and octaves all the way down, and then up reversing it.
    - `Up & Down (UPDN)` will walk the notes up and down, repeating the highest and lowest notes.
    - `As played (PLAY)` will walk the notes in the same order that they were played. Tip: this mode
      also works in conjunction with Octave Mode Alternate, which will walk all the notes and octaves
      all the way up (with notes as played), and then down reversing the order of played notes.
      Note: this produces the same effect as Up for Kit Rows.
    - `Random (RAND)` will choose a random note each time. If the Octave Mode is set to something
      different than Random, then the pattern will play, in the same octave, the same number of random
      notes as notes are in the held chord and then move to a different octave based on the Octave Mode.
      Tip: Set also Octave Mode to Random to have the equivalent to the old `Random` mode.
- **`Chord Simulator (CHRD)`**: is only available for Kit Rows. This allows you to simulate a chord so you
  can use `Note Mode` on the Kit Row.
- **`Step Repeat (REPE)`**: allows you to set how many times the same note in the pattern is repeated
  before moving on to the next note of the pattern.

### Automatable parameters
All the following parameters define other aspects of the arpeggiator and can be **automated** and **learned** to golden knobs and MIDI CC:
- **`Gate`**: This changes the length of the note, as a relative percentage of the time window set between this note and the next.
  A gate of 25 will make the notes last for 50% of the available time window.
- **`Rhythm (RHYT)`**: This parameter will play silences in some of the steps. This menu option shows zeroes and dashes,
  "0" means "play note", and "-" means "don't play note" (or play a silence). The available options are:
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
- **`Limits (LIMI)`**: is a group of parameters to define limitations for the sequence and for the randomizer.
    - **`Sequence Length (SEQ)`**: It defines the length of the pattern in number of steps.
      - If set to zero, the arpeggiator pattern will play fully without restrictions.
      - If set to a value higher than zero, the pattern will play up to the set number of steps, and then
        reset itself to start from the beginning. Tip: You can use this in combination with the Rhythm
        parameter to create longer and more complex rhythm patterns.
    - **`Chord Polyphony (POLY)`**: This will set the maximum number of notes that will play at the same time when a
      step is played as a chord. Increase parameter `Chord Probability` to make some steps play a chord instead
      of a single note.
        - From values 0 to 4, just the root note, no chord.
        - From 5 to 19, a 2-notes chord is played, using the 5th.
        - From 20 to 34 a 3-notes chord is played, using a triad (3rd + 5th).
        - From 35 to 50, a 4-notes chord is played, usign a seventh chord (3rd + 5th + 7th).
    - **`Ratchet Amount (RATC)`**: A ratchet is when a note repeats itself several times in the same time interval
      that the original note has to play. This will set the maximum number of notes that a single tep can have (each step will
      randomize the number of ratchet notes between 1 and this maximum value set).
        - From values 0 to 4, no ratchet notes.
        - From 5 to 19, up to 2 ratchet notes.
        - From 20 to 34, up to 4 ratchet notes.
        - From 35 to 50, up to 8 ratchet notes.

### Randomizer
The following parameters are also automatable and learnable to golden knobs and MIDI CC:
- **`Randomizer (RAND)`**: this group of parameters define the probability of some parameters to vary.
  - **`Lock`**: this flag will lock the current sequence of randomized values so the sequence has a repeatable pattern.
    Tip: In case you have drone notes, make use of the `Sequence Length` parameter to further adjust the repeated sequence.
    If you want to re-roll the dice for a parameter, slightly change its value and a new set of random values will be
    generated and locked.
  - **`Note Probability (NOTE)`**: It applies a probability for notes to play or not (after Rhythm has been evalutated,
    that is, only for note steps, not for silent steps).
  - **`Bass Probability (BASS)`**: It applies a chance of replacing the current note to be played with the lowest note of the played notes.
    The higher the value, the more likely that the note played is the bass note of the arpeggiated chord.
    This can be used as a performance tool to open up or close the arpeggio pitch range.
  - **`Chord Probability (CHRD)`**: This paramater will allow you to control the chance of a note to play, at the same time, itself and the following notes in the available
    notes set, like a chord.
  - **`Ratchet Probability (RATC)`**: This sets how likely a step is to be ratcheted. The ratchet amount is randomized on each step between 1 and
    the maximum value set with the `Ratchet Amount` parameter within the `Limits` group.
  - **`Velocity Spread (VELO)`**: The velocity of the arp step will get a random **negative** deviation from the base velocity.
  - **`Gate Spread (GATE)`**: The gate of the arp step will get a random **positive** or **negative** deviation from the base gate.
  - **`Octave Spread (OCTA)`**: The note will get a change in pitch of a random amount of octaves, going from 0 up to a maximum of +3 octaves.
      - From values 0 to 4, no changes in octaves.
      - From 5 to 19, up to +1 octave changes.
      - From 20 to 34, up to +2 octave changes.
      - From 35 to 50, up to +3 octave changes.

### MPE
This submenu contains parameters that are useful if you have an MPE controller connected to the Deluge:
- **`MPE`**:
  - **`Velocity`**: It will allow you to control the velocity of each new arpeggiated note with an MPE expression parameter.
    - `Off`: This disables control of velocity with MPE.
    - `Aftertouch (AFTE)`: The pressure applied to the key sets the velocity of the note.
    - `MPE Y (Y)`: The Y position on the MPE controller sets the velocity of the note.
Tip: For the synth preset to play correctly, it is recommended to disable the modulation of `Master Volume` with `Velocity`, that is,
bring the value to zero (which is normally set by default to 50), and add a modulation of `Master Volume` with `Aftertouch` or `MPE Y`
(depending on your MPE controller).
