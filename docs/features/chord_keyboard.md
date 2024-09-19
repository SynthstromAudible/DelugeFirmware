# Chord Keyboard and Chord Library Layouts

## Description:

Two new chord focused keyboards as an additional keyboard options.

The first `CHORD` keyboard is a layout that facilitates the creation and exploration of chords within a scale. It offers two modes—Row Mode (similar to the Launchpad Pro's chord mode) and Column Mode—each (providing in-scale harmonically similar chords for substitutions).

The second `CHORD LIBRARY` keyboard is a library of chords where each column is a note (chromatic, all 12 notes), with chords laid out vertically within each column. 

## Usage

### Community Feature Setting

Enable the `Chord Keyboards (CHRD)` community setting for full access to the relevant UI behaviors. As the UI and implementation is still experimental, a community setting has to be activated to access the `CHORD` keyboard.

### Change Keyboard Layout

To access the Chord Keyboard Layout:
1. Enable the `Chord Keyboards (CHRD)` community feature
2. While in `SYNTH`, `MIDI`, or `CV` mode, press `KEYBOARD` to enter a keyboard view.
3. Turn `SELECT` + `KEYBOARD` to cycle through layouts until you find the `CHORD` keyboard or `CHORD LIBRARY` keyboard.

#### Chord Keyboard Layout

The Chord Keyboard Layout is designed to facilitate the creation and exploration of chords within a scale. It offers two modes—Row Mode and Column Mode—each providing a different approach to chord arrangement and selection.

##### Features

- **Modes:**
  - **Row Mode:** Inspired by the Launchpad Pro, this mode lays out chords horizontally across the pads, with generally simpler intervals on the left and more complex ones on the right. 
  - **Column Mode:** This mode arranges harmonically similar chords vertically. Chords in the same column can often be substituted for each other to explore different harmonic flavors. As an example, if your in a major scale, the first column will contain various option for the I chord, the second column will contain various options for the ii chord, and so on. 

- **Color Coding:**
  - Chords are colored based on their quality:
    - Major: Blue
    - Minor: Purple
    - Dominant: Cyan
    - Diminished: Green
    - Augmented: Greyish Blue
    - Other: Yellow

- **Controls:**
  - **Embedded Controls:**
    - Because the Chord Keyboard has more features than other keyboard layouts, embedded into the right couple columns of the main grid is are controls. Currently it includes controls for switching between Row Mode and Column Mode, with more features planned for the future.
    - At the top of the right most main grid column, you can select the current mode (`ROW` and `COLUMN`) by pressing the corresponding pad. It defaults to Row Mode.
      - In addition, you can use `SHIFT` + `◀︎▶︎` to also cycle through the `ROW` and `COLUMN` mode.
  - turning either `◀︎▶︎` or  `▼︎▲︎` will move up or down by scale degrees.

##### Scale Considerations

The Chord Keyboard is currently limited to scales that are modes of the major scale. (Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, and Locrian scales).

---

#### Chord Library Layout

The Chord Library Layout provides a comprehensive library of chords, making it easy to explore and use a wide variety of chord types.

##### Features

- **Layout:**
  - Each column represents a note (chromatic, all 12 notes), with chords laid out vertically within each column. The first row contains root notes, the second row contains major triads, the third row contains minor triads, and so on.
  - Each column is a different color with the colors repeating on the octave. This is to help you quickly identify the root note of the chord you are looking for, no matter how high or low you have scrolled in the layout.
  - In scale mode, the chords with notes entirely within the scale are highlighted, while the chords with notes outside the scale are dimmed.
  - When out of scale mode, the final 2 columns change colors each 8 chords you scroll up to help you keep track of where you are in the layout as you scroll up and down. Other than these columns, the column of the root note of the key you are in is highlighted, while the all other columns are dimmed.
  
- **Controls:**
  - The horizontal encoder scrolls through the scale, allowing you to raise or lower the chords. The vertical encoder scrolls up to access more chords.

- **Voicing Adjustment:**
  - Holding a pad on a row and pressing the horizontal encoder allows you to change the voicing for all the chords in that row simultaneously.

##### Scale Considerations

The Chord Library will show chords utilizing both notes entirely in your current scale and not. With the current implementation of accidentals in the Deluge, if you record chords while in scale mode with out of scale notes, these will be added to your scale. A future update will hopefully make this work better.
