Manages round-robin sample variants for the oscillator's current sample zone.

Each zone can hold up to 4 samples: the primary sample (slot 1, also loadable through the regular
<string-for name="STRING_FOR_FILE_BROWSER">FILE</string-for> browser) plus up to 3 alternates. When more than one
slot is loaded, each new trigger of the zone picks a slot according to the selected mode:

* <string-for name="STRING_FOR_CYCLE">CYCLE</string-for> — play the slots in order, wrapping around (classic round-robin).
* <string-for name="STRING_FOR_RANDOM">RANDOM</string-for> — pick any slot at random.
* <string-for name="STRING_FOR_NO_REPEAT">NO REPEAT</string-for> — pick at random, but never the same slot twice in a row.

Every slot has its own horizontal menu: `FILE` to load a sample into that slot, `STRT`/`END` to open
the sample marker editor focused on that marker (loop points stay reachable from the same editor
screen), and `TRANSPOSE` to adjust that slot's pitch as a single combined semitones+cents value.
While a slot's menu (or its marker editor) is open, auditioning plays that exact slot without
advancing the cycle. Alternates must be filled in order; pressing SHIFT + SAVE/DELETE on an
alternate slot clears it. The whole feature can be disabled in SETTINGS > COMMUNITY FEATURES;
variant data in songs is preserved while off.
