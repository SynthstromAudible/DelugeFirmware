# Chord Keyboard and Chord Library Layouts

## Description:

Two new chord focused keyboards as an additional keyboard options.

The first `CHORD` keyboard is a layout that facilitates the creation and exploration of chords within a scale. It offers two modes, Column mode (providing in-scale harmonically similar chords for substitutions) and Row Mode (based largely on the Launchpad Pro's chord mode that spreads notes out on a row to allow users to build and play interesting chords across the rows).

The second `CHORD LIBRARY` keyboard is a library of chords where each column is a note (chromatic, all 12 notes), with chords laid out vertically within each column.

### Community Feature Setting

Enable the `Chord Keyboards (CHRD)` community setting for full access to the relevant UI behaviors. As the UI and implementation is still experimental, a community setting has to be activated to access the `CHORD` keyboard.

### Change Keyboard Layout

To access the Chord Keyboard Layout:
1. Enable the `Chord Keyboards (CHRD)` community feature
2. While in `SYNTH`, `MIDI`, or `CV` mode, press `KEYBOARD` to enter a keyboard view.
3. Turn `SELECT` + `KEYBOARD` to cycle through layouts until you find the `CHORD` keyboard or `CHORD LIBRARY` keyboard.

## Chord Keyboard Layout

The Chord Keyboard Layout is designed to facilitate the creation and exploration of chords within a scale. It offers two modes, a Column Mode and Row Mode, each providing a different approach to chord arrangement and selection.

### Modes

#### Column Mode

This mode arranges harmonically similar chords vertically. Chords in the same column can often be substituted for each other to explore different harmonic flavors. For example, if you are in a major scale, the first column will contain various options for the I chord, the second column will contain various options for the ii chord, and so on.

#### Row Mode

Based largely on the Launchpad Pro, with the notes spread out horizontally across the pads to allow users to build and play interesting chords across the rows. Generally simpler and more standard intervals for chords are on the left and less standard ones on the right.

Each row starts from the next scale degree up or down, and then moving across the pads, the intervals are the following in scale intervals (if a 3rd up from a scale degree is a major 3rd, it will be a major 3rd in the row, if it is a minor 3rd, it will be a minor 3rd in the row).
  - Root, 5th, 3rd + Octave (Compound 3rd), 7th + Octave (Compound 7th), 5th + Octave (Compound 5th), 3rd + 2 * Octave (Double Compound 3rd), 2nd + 2 * Octave (Double Compound 2nd), 6th + Octave (Compound 6th), Octave, 5th, 7th, 3rd - Octave, 2nd
  - The final column is a triad based on the Root of the row with 5th, Compound 3rd above it.

**As the Chord Keyboard is still in development, the intervals chosen for each row are not final and may change in the future. We welcome feedback on the intervals chosen for each row.**


### Color Coding

In Column mode, Chords are colored based on their quality, and in Row mode, the Rows are based on the quality of the chord from that scale degree:

  - Major: Blue
  - Minor: Purple
  - Dominant: Cyan
  - Diminished: Green
  - Augmented: Greyish Blue
  - Other: Yellow

### Controls

turning either `◀︎▶︎` or  `▼︎▲︎` will move up or down by scale degrees.

#### Embedded Controls:

Because the Chord Keyboard has more features than other keyboard layouts, controls are embedded into the two rightmost columns of the main grid. Currently it includes controls for switching between Row Mode and Column Mode, with more features planned for the future.
  - At the top of the rightmost main grid column, you can select the current mode (`ROW` and `COLUMN`) by pressing the corresponding pad. It defaults to Column Mode.
    - In addition, you can use `SHIFT` + `◀︎▶︎` to also cycle through the `ROW` and `COLUMN` mode.

### Scale Considerations

The Chord Keyboard is currently limited to scales that are modes of the major scale. (Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, and Locrian scales).

---

## Chord Library Layout

The Chord Library Layout provides a comprehensive library of chords, making it easy to explore and use a wide variety of chord types.

### Features

#### Layout

Each column represents a note (chromatic, all 12 notes), with chords laid out vertically within each column. The first row contains root notes, the second row contains major triads, the third row contains minor triads, and so on.

Each column is a different color with the colors repeating on the octave. This is to help you quickly identify the root note of the chord you are looking for, no matter how high or low you have scrolled in the layout.

In scale mode, the chords with notes entirely within the scale are highlighted, while the chords with notes outside the scale are dimmed.

When out of scale mode, the final 2 columns change colors each 8 chords you scroll up to help you keep track of where you are in the layout as you scroll up and down. Other than these columns, the column of the root note of the key you are in is highlighted, while the all other columns are dimmed.

#### Controls

Turning `◀︎▶︎` scrolls through the scale, allowing you to raise or lower the chords. Turning `▼︎▲︎` scrolls up or down to access more chords.

#### Voicing Adjustment

Holding a pad on a row and press and turning `◀︎▶︎` allows you to change the voicing for all the chords in that row simultaneously.

### Scale Considerations

The Chord Library will show chords utilizing both notes entirely in your current scale and not. With the current implementation of accidentals in the Deluge, if you record chords while in scale mode with out of scale notes, these will be added to your scale. A future update is in development to address this limitation.

### Notation

In the interest of brevity, and to display as much of the chord name as possible for the 7SEG Deluge, the Chord names use  a concise notation. The chords list the note name with no octave number, followed by the chord symbol. The main types of chord symbols used are:
  - Major: `M`
  - Minor: `-`
  - Diminished: `DIM`
  - Augmented: `AUG`
  - suspended: `SUS`

Then the chord would be followed by any additional information such as the extension (7, 9, 11, 13) or alterations (b5, #5, b9, #9, #11, b13). Finally, if the chord has a well known nickname, it may be included as well.

So for example, a C Major chord would be displayed as `CM`, a C Minor chord would be displayed as `C-`, and a C Diminished chord would be displayed as `CDIM`. A C Dominant 7 chord would be displayed as `C7`, a C Minor 7 Flat 5 chord would be displayed as `C-7FLAT5`.

