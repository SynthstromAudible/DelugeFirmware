# Lanes Mode (Generative Lane Sequencer)

Lanes Mode is an alternative sequencing mode for melodic instrument clips (Synth, MIDI, CV). Instead of the traditional note-per-row approach, it uses **8 independent parameter lanes** that combine to generate notes. Each lane controls one aspect of the note (pitch, velocity, gate, etc.) and can have its own length, direction, and clock division — creating polymetric and generative patterns from simple ingredients.

## Entering and Exiting

- **Enter:** Press **SHIFT + CLIP VIEW** while in an instrument clip view. The display shows `"LANES"`.
- **Exit:** Press **SHIFT + CLIP VIEW** again. The display shows `"SEQNCE"` and any playing notes are stopped.
- **Not available on Kit clips** — attempting to enter shows `"NO KIT"`.

When entering for the first time, the engine initializes with default values (velocity from the instrument's default, probability at 100%, gate at 75%).

## Grid Layout

The grid shows 8 lanes (one per row), with the **trigger lane at the top** and **probability at the bottom**:

| Row | Lane | Color | Values |
|-----|------|-------|--------|
| Top (row 7) | **TRIGGER** | Amber | 0 (off) or 1 (on) |
| Row 6 | **PITCH** | Cyan | -24 to +24 scale degrees from base note |
| Row 5 | **OCTAVE** | Purple | -4 to +4 octave shifts |
| Row 4 | **VELOCITY** | Red | 1–127 |
| Row 3 | **GATE** | Green | 5–100%, 101=TIE, 102=LEGATO |
| Row 2 | **INTERVAL** | Blue | -12 to +12 scale degrees (accumulated) |
| Row 1 | **RETRIG** | Orange | 0–8 subdivisions per step |
| Bottom (row 0) | **PROB** | Yellow | 0–100% chance of note firing |

### Brightness

Each pad's brightness encodes the step's value — higher absolute values appear brighter. Steps with a value of 0 are dark (off). Steps beyond a lane's length appear as a very dim ghost of the lane color.

### Sidebar

The audition column (right edge) shows each lane's base color. The mute column is unused in lanes mode.

### Playhead

Each lane displays its own independent playhead column, showing which step was most recently played. When lanes have different lengths or clock divisions, their playheads move independently — this is what creates polymetric patterns.

## How Notes Are Generated

**All lanes advance independently on every tick** — they free-run regardless of whether the trigger lane fires. The trigger lane only gates the output (whether a note sounds), not the advancement of other lanes. This is what creates true polymetric phasing when lanes have different lengths.

On each step tick (16th note by default):

1. All lane values are read at their current positions
2. All lanes advance one step (independently, according to their own length, direction, and clock division)
3. **Trigger** lane determines if a note fires — if trigger is 0, the step is a rest (but all lanes have already advanced)
4. **Pitch** lane provides a scale-degree offset from the base note (C4 by default)
5. **Interval** lane adds an accumulated value (wraps between min/max bounds) — only accumulates when trigger fires
6. **Octave** lane shifts the result up or down by octaves
7. The combined pitch is clamped to MIDI range (0–127)
8. **Probability** lane rolls a random check — if it fails, the note becomes a rest
9. **Velocity**, **Gate**, and **Retrigger** lanes set the note's expression

Pitch and interval values are **scale-degree offsets** — they follow the current musical key's scale. For example, in C major, a pitch value of +2 means "2 scale degrees up" = E, not D (which would be +2 semitones).

### Per-Step Probability

Some lanes support per-step probability (PITCH, OCTAVE, VELOCITY, INTERVAL, RETRIG). When a step's probability is less than 100%, there's a chance the lane falls back to its default value for that step instead of using the programmed value. This adds controlled randomness to individual parameters without silencing the note entirely.

### Special Gate Values

- **TIE (101):** Holds the note through the next step without retriggering (same pitch only)
- **LEGATO (102):** Glides smoothly to the next pitch without a gap between notes

## Grid Pad Interactions

### Tap a pad — Toggle step on/off

- Tapping a pad toggles its value between 0 and the lane's default
- For **TRIGGER**: toggles between 0 and 1
- For other lanes: toggles between 0 and the lane's default value (e.g., velocity 100, gate 75)
- Tapping beyond a lane's current length **extends** the lane to reach that step

### Hold pad + turn horizontal encoder — Adjust step value

- For most lanes: changes the step's value by ±1 per click, clamped to the lane's range
- For **TRIGGER**: changes the euclidean pattern **length** (the held step doesn't matter)
- For **PITCH**: shows the resulting note name (e.g., `"C#4"`)
- For **GATE**: shows `"TIE"`, `"LEGATO"`, or `"GATE: 75%"`

### Hold pad + turn vertical encoder — Per-step probability / Euclidean pulses

- For lanes with per-step probability (PITCH, OCTAVE, VELOCITY, INTERVAL, RETRIG): adjusts that step's probability in 5% increments. Display: `"PITCH PROB:85%"`
- For **TRIGGER**: changes the euclidean **pulse count**. Display: `"TRIG LEN:16 P:5"`
- For GATE, PROB: no effect

### Hold pad + press horizontal encoder — Randomize step

Randomizes the held step's value within musically-useful bounds (tighter than the full min/max range). Display: `"RND C4"` (pitch) or `"VELOCITY: RND 87"` (others).

### Hold pad + press vertical encoder — Reset step to default

Resets the step to its lane's default value and per-step probability to 100%. Display: `"VELOCITY: DEF 100"`.

## Audition Pad Interactions

The audition pads (right sidebar) are repurposed in lanes mode — they select lanes for editing instead of auditioning notes.

### Hold audition pad — Show lane info

Displays the lane name, length, and clock division. Examples:
- `"PITCH L:16"` (no division)
- `"TRIGGER L:8 D:/2"` (clock division active)

### Hold audition + turn horizontal encoder — Change lane length

Adjusts the lane's step count from 1 to 64. New steps are filled with lane-appropriate defaults (e.g., velocity 100, gate 75; trigger lane fills with 0). If the playhead was past the new end, it wraps to 0. Display: `"TRIGGER LEN:24"`.

### Hold audition + turn vertical encoder — Change clock division

Sets how often the lane advances: `1/16` (every step), `1/8` (every 2nd step), `1/4`, `1/2`, `1 BAR`, up to `/32`. This is the core of polymetric sequencing — giving lanes different divisions creates evolving, phasing patterns. Display: `"PITCH DIV:1/4"`.

### Hold audition + turn select encoder — Change lane direction

Cycles between **Forward**, **Reverse**, and **Pingpong**. Display: `"TRIGGER: Pingpong"`.

### Hold audition + press horizontal encoder — Randomize entire lane

Randomizes all steps in the lane within musically-useful bounds. Display: `"PITCH: RANDOMIZED"`.

### Hold audition + press SELECT — Open lane editor menu

Opens a submenu for advanced lane settings. Currently available for:
- **PITCH**: base note and scale settings
- **INTERVAL**: accumulator range and scale-awareness toggle

Other lanes show the lane name but have no submenu yet.

## Euclidean Rhythm Generation

The **TRIGGER** lane supports euclidean rhythm generation — evenly distributing a number of pulses across a given length.

- **Hold any trigger pad + turn horizontal encoder** to change the pattern **length**
- **Hold any trigger pad + turn vertical encoder** to change the number of **pulses**

The display shows both values: `"TRIG LEN:16 P:5"`. The pattern regenerates immediately when either value changes.

## Polymetric Sequencing

The power of lanes mode comes from giving lanes **different lengths and clock divisions**. For example:

- Trigger: 8 steps at 1/16 (straight eighth notes)
- Pitch: 5 steps at 1/16 (5-note melodic cycle)
- Velocity: 3 steps at 1/8 (3-element accent pattern, advancing half as fast)

The lanes phase against each other, creating a long, evolving sequence from short patterns. The independent playheads on the grid make this visible in real time.

## Quick Reference

| Action | What it does |
|--------|-------------|
| **SHIFT + CLIP VIEW** | Toggle lanes mode |
| **Tap pad** | Toggle step on/off |
| **Hold pad + horiz encoder** | Adjust step value (trigger: euclidean length) |
| **Hold pad + vert encoder** | Adjust step probability (trigger: euclidean pulses) |
| **Hold pad + press horiz encoder** | Randomize step |
| **Hold pad + press vert encoder** | Reset step to default |
| **Hold audition** | Show lane info |
| **Hold audition + horiz encoder** | Change lane length (1–64) |
| **Hold audition + vert encoder** | Change clock division |
| **Hold audition + select encoder** | Change lane direction |
| **Hold audition + press horiz encoder** | Randomize entire lane |
| **Hold audition + press SELECT** | Open lane editor menu |
