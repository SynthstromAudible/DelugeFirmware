Offsets the sample start position as a fraction of the sample region (between start and end markers). Supports per-step automation (parameter locks) so each note can start at a different position within the sample.

- **Positive offset**: shifts the start forward into the region (plays less of the sample)
- **Negative offset**: moves the start backward before the start marker into available audio, then:
  - **LOOP mode**: wraps remaining offset from the end of the region
  - **ONCE/CUT modes**: pads with silence (pre-roll) before playback begins
  - **GATE/STRETCH modes**: applies offset as a tick shift for bar-phase alignment
