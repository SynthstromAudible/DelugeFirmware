Manages round-robin sample variants for the oscillator's current sample zone. On an oscillator with
several keyboard zones, entering this menu first opens the note-range picker to choose which zone's
variants to edit - the same zone-selection step the flat FILE/START/END/TRANSPOSE items use.

Each zone can hold up to 4 samples: the primary sample (slot 1, also loadable through the regular
<string-for name="STRING_FOR_FILE_BROWSER">FILE</string-for> browser) plus up to 3 alternates. When
more than one slot is loaded, <string-for name="STRING_FOR_MODE">MODE</string-for> decides which slot
each new trigger of the zone plays:

* <string-for name="STRING_FOR_CYCLE">CYCLE</string-for> — play the slots in order, wrapping around (classic round-robin).
* <string-for name="STRING_FOR_RANDOM">RANDOM</string-for> — pick any slot at random.
* <string-for name="STRING_FOR_NO_REPEAT">NO REPEAT</string-for> — pick at random, but never the same slot twice in a row.
* <string-for name="STRING_FOR_VELOCITY">VELOCITY</string-for> — pick by how hard the note was played, from each slot's own velocity range.

Every slot has its own horizontal menu: `FILE` to load a sample into that slot, `STRT`/`END` to open
the sample marker editor focused on that marker (loop points stay reachable from the same editor
screen), `TRANSPOSE` to adjust that slot's pitch as a single combined semitones+cents value, and
`VOL` to trim that slot's level (0-50, where 50 is unity - attenuation only) so takes can be
balanced against each other.

In <string-for name="STRING_FOR_VELOCITY">VELOCITY</string-for> mode each slot gains `VMIN` and
`VMAX`, the two ends of the velocity range (1-127) that slot answers to. A note plays a slot whose
range contains its velocity; where several slots overlap, those slots round-robin among themselves,
so velocity layers and round-robin combine rather than exclude each other. A velocity matching no
slot falls back to slot 1. `VMIN` and `VMAX` are hidden in the other three modes.

Loading a whole folder can build velocity layers for you. When every file landing on the same note
carries a velocity tag in its name - `vel` (or `velocity`) followed by a number from 1 to 127, as in
`Kick_C1_vel100.wav` - those files stack into one zone, the zone switches to
<string-for name="STRING_FOR_VELOCITY">VELOCITY</string-for> mode, and the ranges are split at the
midpoints between the tagged values. A bare `v` is ignored, since `Kick_v2.wav` normally means
version 2. If any file in a group lacks a tag, that group stays in
<string-for name="STRING_FOR_CYCLE">CYCLE</string-for> mode.

While a slot's menu (or its marker editor) is open, auditioning plays that exact slot without
advancing the cycle. Alternates must be filled in order; pressing SHIFT + SAVE/DELETE on an
alternate slot clears it. The whole feature can be disabled in SETTINGS > COMMUNITY FEATURES;
variant data in songs is preserved while off, and folder loads then keep only one sample per note.
