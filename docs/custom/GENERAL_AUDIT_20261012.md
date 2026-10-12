# General audit and comparison with published beta — 12 October 2026

## Exact scope

- Latest published official beta verified via the beta tag: be3e993c4ae78719d3940f334848aa77973ea585, release “Deluge beta Release 20261012”.
- Distributed custom BIN: firmware/deluge-mdd-plaits-oled-a13bf0fb.bin, 1,811,032 bytes; source a13bf0fba59f13217129a90c20b714223deef6a5.
- Repository code reviewed at e5892463c9e4012d0ab2872433c3971a601f0169. Its later changes are formatting, Python probe maintenance and vendor formatting configuration.
- Comparison is against the published beta, not every subsequent upstream main commit.
- This is source review and host regression coverage, not a complete hardware qualification or proof of absence of bugs.

## Result

No new reproducible critical defect was found in the reviewed custom paths. Existing tests passed as listed below. One contradictory usage-document sentence was found and corrected: WRITE preserves native undo history and stores a snapshot; it does not clear the existing history as an earlier paragraph claimed.

CPU saturation remains possible. No Plaits-specific voice budget, preallocated voice pool, hardware CPU telemetry or automatic economy mode has been implemented.

## Functional differences from official beta

### MDD clip sequencer

- OLED Sequencer selection: Normal or MDD Snake. No new SHIFT+SELECT entry shortcut.
- Seven lanes: Notes, Gates, Velocity and four independent Custom sources.
- Synth, MIDI and CV melodic clips are supported; Kit clips are excluded.
- The live Notes stream is monophonic. Gate determines note duration, not amp decay/sustain; Velocity sets note velocity.
- 16 cells per lane; length 1–16; independent speed, shift, direction and shape.
- Direction: Forward, Reverse, Pendulum, Random. Shapes: Rows, Columns, Snake Rows, Snake Columns, Spiral.
- Straight, triplet and dotted rates. First activation of a fresh or loaded clip normalizes all seven lanes to actual 1/16. Later switches in the same session retain edited rates.
- Native transport integration, mode switching, note-off cleanup and duplicate-tick prevention.
- Normal note rows and MDD data are retained separately when switching modes.
- A 4x4 editor, moving playhead, dimmed muted cells, short press mute, held-pad SELECT editing/popups and Step1+N length gesture.
- Ten control pads: Notes, Gates, Velocity, Custom, Random, Reset, Root/Scale, Map, MDD Snake, WRITE.
- Native horizontal OLED menu controls and synth/preset access while MDD remains selected.
- Random/Random All, Reset/Reset All and editable range.
- Song root and scale integration, including SCALE-button quantization. Root/scale edits can change the whole Song; this is not a separate private MDD key.
- Custom sources are unipolar with signed Depth (-100% to +100%) and up to four Map slots per source. They modulate a base value through Deluge's patch system; they are not absolute parameter assignments.
- Custom mute holds the previous lane output. It does not force the source to zero.
- Patchable filter/envelope/oscillator/effect targets are available. This does not make every global or unpatched parameter routable. Inactive targets can remain editable and show a warning.
- WRITE replaces the Normal rows with one clip loop of Notes/Gates/Velocity, preserving the previous rows and direction in an Undo/Redo snapshot. It sets stock playback direction Forward because lane traversal is already baked.
- WRITE does not bake Custom modulation into parameter automation. It also replaces row-specific MPE/automation in the new rows; Undo restores the old rows.
- WRITE services audio every 32 candidate iterations and limits work to 1,048,576 iterations and 65,536 emitted notes. Allocation failure aborts before replacing the old grid.
- Clip serialization stores MDD lane settings, routes and playback state.

### Plaits

- An extra oscillator source alongside stock sources, for the subtractive synth path.
- Three engines only: FM, Waveshaping, Virtual Analog. Engine display 1–3.
- OSC1 and OSC2 engine parameters, automation/patch support and MDD Custom routing.
- Existing oscillator macro parameters provide harmonics/timbre/morph controls.
- Deluge sample-rate/pitch conversion, cached pitch calculation and engine state initialization.
- Same-model rendering preserves state; changing model reuses reserved engine storage and applies a 64-sample transition.
- Unused AUX synthesis is skipped, optimized compiler flags retained, output amplitude scaled and non-finite output rejected using bit checks compatible with fast-math.
- Each active oscillator/unison voice allocates a PlaitsVoice at note-on and releases it on unassignment. Model switching itself does not allocate on the heap.
- Plaits oscillator sync is disabled; Ringmod and stock FM-mode operation are not the Plaits adapter path.

### Shared UI, startup and effects

- Holdontoentropy greeting for four seconds after startup loading, without replacing an existing error popup.
- Startup F4 failsafe marker cleanup and return.
- OLED submenu traversal no longer constructs a before-begin iterator.
- SHIFT encoder acceleration uses 32-bit arithmetic.
- Selected long MDD Map/Scale text and WRITE headers can scroll during playback; this is not universal scrolling for all menus.
- Delay copying retains sync type.
- Granular allocation/setup occurs before shutdown-wrap calculation; grain size uses the updated rate.
- Song loading applies the saved reverb HPF.
- Digital reverb gets custom Width mid/side processing (0 mono, 1 original stereo).
- Upstream Reverb PR #4990 is added beyond the beta: damping/filter-state fixes, LFO/state handling, safe saturating output conversion, parameter initialization and bounds/test coverage. Katherine Whitlock's upstream contribution is attributed.
- Legacy sound-level Snake Custom menu/configuration and snakeCustomV1/V2 loading/saving remain. Default is disabled. When MDD owns the sound its clip sources take precedence.

### What follows upstream

Voice culling and MIDI/gate scheduling use the published beta code. Relative to the beta, audio_engine.cpp differs only in applying the Song HPF. No new overload-culling algorithm, stock oscillator DSP replacement or automatic voice-budget policy was introduced. Shared modulation/menu/clip code was extended to support the new modules, so claiming that no stock-path code was touched would be inaccurate.

GPL/MIT notices, corresponding source, build instructions, installation instructions, guide PDF and tests are included. Later formatting fixes do not replace the distributed BIN, whose exact source remains pinned.

## Validation

Rerun against repository e5892463:
- MDD regression: seven-lane startup timing, independent clocks, shapes/directions, mapping, scale, native menus/grid, codec, playback, WRITE/Undo and allocation-failure paths; 1,280 bake combinations.
- Plaits ASan/UBSan regression: all three engines, macro corners, buffer guards, reset behavior, 1,000 live model switches, AUX equivalence and A4 approximately 440.011 Hz.
- Plaits menu probe: 16 configurations and saved oscillator type round trips.
- All eight effects/menu regression groups.
- Actual audio-culling body and startup/error-popup host probes.
- Legacy Snake traversal/mapping regression.
- WRITE service/no-service event equivalence and bounded fully muted long-loop work.
- NaN/infinity guard under -O3 -ffast-math.

Prior integration verification also passed upstream reverb_spec with delay-line bounds checks, an ARM Release build, ELF/BIN matching and download checksum verification. Formatting follow-up passed GitHub Format Check and an ARM rebuild.

## Remaining validation and behavioral limits

- No physical-device timing, SD load, audio clipping, long-session stability or CPU/polyphony measurement is available.
- More Plaits voices, unison, two Plaits oscillators and long releases raise synthesis load.
- MDD asks the transport to revisit every native tick; multiple active clips add scheduling work.
- Notes are monophonic and every new trigger releases the previous note. No explicit tie/overlap lane is implemented; strict legato glide is not guaranteed. Regular portamento follows the preset.
- Fractional musical rates are scheduled on native integer ticks, so very fast/triplet rates have tick-resolution quantization.
- Mapping uses base value plus modulation and destination bounds. A saturated destination can sound unchanged.
- Shared reverb remains Song-level; #4990 does not create independent per-instrument reverb instances.
- Old official firmware does not implement these new sources or MDD serialization. Backups remain necessary when returning to it.
