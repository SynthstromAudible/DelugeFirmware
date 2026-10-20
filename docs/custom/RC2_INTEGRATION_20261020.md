# MDD Snake + Plaits on Deluge 1.3.0 RC2

This custom OLED firmware merges upstream `v1.3.0-rc2` (`465a30985fad185beddc639e14667c86f97e89b9`, published 19 October 2026) into the previously published MDD/Plaits source. It is an unofficial build, not the upstream RC2 binary.

## User interface

- MDD speed selection is ordered from slow to fast, interleaving straight, dotted and triplet values. Existing stored tick ratios and initial 1/16 behavior are preserved.
- All 15 speeds share one name table. The previous main OLED status indexed a six-entry table with up to 15 values; triplet/dotted selections could read outside the table.
- Custom steps show 1/2/3 when their assigned targets are exclusively Plaits Engine targets, in any of the four Map slots. Mixed Engine/filter/effect mappings retain continuous percent editing so another target does not lose its resolution. These are source levels; base Engine and signed Depth still determine the resulting model.
- WRITE confirmation explicitly mentions both notes and Custom automation. The full title scrolls.
- MIDI/CV Map attempts display "Map needs Synth" instead of incorrectly asking the user to select Custom again.
- The MDD status hint fits within the OLED width, including dotted/triplet speeds.
- Plaits/DX7 horizontal labels use the oscillator type directly, avoiding allocation and a potentially stale index into a list whose size changes when inputs are connected.
- The MDD long-press timer remains responsive, but no longer requests redraws every 20 ms. Playback redraws are requested when the visible cell changes or playback starts/stops; edits still request immediate redraws.

## Plaits

- A new note initializes the selected, patched model on first render. It no longer initializes FM and immediately discards it when Wave/VA is selected.
- A bounded pool retains at most eight unused raw Plaits blocks for reuse. It imposes no voice limit, falls back to the existing allocator and frees excess blocks. Song-deletion unassignment drains the cache. Each reused object is freshly constructed, so prior-note DSP state cannot leak into a new note.
- Engine selection uses exact signed integer rounding instead of double division and `lround` in the render path.
- A dead band of 1/1024 of a model interval on either side of a selection boundary prevents small modulation fluctuations from repeatedly resetting the engine. Discrete model selections still switch immediately; the initial selection uses the original rounding rule.
- Existing OUT-only synthesis, pitch caching, output level, 4x FM oversampling, sample rate, tuning calculation and native overload voice handling are retained. Polyphony, unison and two active Plaits oscillators still multiply synthesis work. No hardware CPU percentage improvement is claimed.

## Merge resolution

- The upstream granular rate/density recalculation replaces the equivalent custom implementation. Shutdown timing is evaluated after grain settings are updated, retaining the earlier custom fix.
- Upstream reverb changes and the custom stereo Width control are both retained.
- Tests use the RC2 Argon dependency revision.
- MDD, its automation WRITE/Undo, Tie/Overlap, the three Plaits engines, boot greeting and existing custom features remain included. Copyright and license notices are preserved.

## Validation and limits

Host checks cover the real MDD menu writes, clock synchronization, all 15 speeds, 71,680 grid states, change-driven redraws, serialization, WRITE allocation rollback and Undo/Redo, tie/overlap, exclusive Custom destinations, Plaits output/tuning/model switching and effects regressions. Additional tests compare integer rounding with the old calculation, verify deferred output against direct initialization, exercise all Map slots and check bounded pool allocation/free accounting. AddressSanitizer and UndefinedBehaviorSanitizer are used where supported; LeakSanitizer is unavailable in the execution environment.

The exact ARM build identity, binary checksum and validation status are recorded in `firmware/BUILD_INFO.json`. No physical Deluge testing or CPU profiling has been performed. The previously reported preset-loading silence and song-save error do not have a confirmed reproduction or dedicated fix in this update.
