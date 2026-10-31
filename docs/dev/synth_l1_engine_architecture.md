# L1 engines and sources

The Deluge instrument's voice (`deluge::synth::Voice`, `src/synth/voice/`, see `synth_voice_lift.md`) keeps the
envelopes, local LFOs, modulation, porta, filters, wavefold and output stage. What it sounds like is made below it by
two tiers, in the synth library:

- **Sources** (`src/synth/sources/`): one generator in one of a unison part's two slots — a virtual-analog wave, the
  wavetable, the supersaw, a sample, the live input, a DX7 patch.
- **Engines** (`src/synth/engines/`): one voice's timbre generator over all its unison parts. An engine family
  decides how the two slots relate.

Engines are loaded by the Deluge instrument, never by the host: what the host loads is a plugin
(`src/plugin-api/`). The CLAP boundary stays at the instrument (L3); this tier is a lean native C++ interface.

## The tiers

| Tier | What it is | Here | To come |
|---|---|---|---|
| L0 kernels (`dsp-next`) | DSP building blocks | band-limited tables, oscillators, sinc interpolator, DX7 operator graph, filters | — |
| Sources | one generator in one slot of one unison part | VA, wavetable, supersaw, sample, live input, DX7 | Karplus-Strong, Plaits models, granular from a sample, additive |
| Dual-source engines | the relationship between the two slots, plus noise | Subtractive (mix, A→B sync), RingMod (multiply, A→B sync) | AM, phase modulation A→B, exciter→resonator (Rings, a Clouds-style granular slot B) |
| Whole-voice engines | one generator is the voice | FM (two modulators into two carriers, with feedback) | a whole-voice DX7, Plaits as the voice, physical models (a piano: exciter and strings per voice) |
| Per-sound stages | run once per sound, after its voices (the instrument tier) | the sound FX chain | a physical model's soundboard and sympathetic resonance |

Hard sync, ring-mod pairing and the FM operator graph are relationships between generators, so they live in the
engine that owns both, not in a source.

## The contracts

Both contracts are templates over a **numeric backend** (`src/synth/sources/backend.h`). `Fixed` is the q31 Deluge
instrument's — `int32_t` samples and controls, `uint32_t` phases, the Deluge's per-type source blocks, voice types
and control structs — and is the only one instantiated. The templates are named `Basic*<B>`; the plain names
(`Source`, `SourceBlock`, `Engine`, `EngineBlock`, …) are their `Fixed` instantiations. Neither contract names a
Deluge param index: the voice derives each engine's controls from its param planes.

### Source (`src/synth/sources/source.h`)

`BasicSource<B>`: stateful (phase, feedback, playback position live in the source).

| Member | Does |
|---|---|
| `note_on(const SourceNote&) → bool` | starts the source for a note (a sample's reader, a DX7's pooled voice, the input's channel); false when it cannot play |
| `render(const BasicSourceBlock<B>&) → SourceRenderResult` | adds one block into the buffer: `ACTIVE`, `UNASSIGN` (it ended), or `SKIP` (this part sits out the block) |
| `release(const SourceRelease&) → bool` | the note-off (a sample plays past its loop end; a DX7's key-up); false when it ends now |
| `unassign(bool)` | lets go of what it holds |
| `current_phase()`, `advance_phase(delta)` | an oscillator's phase, which osc sync reads from the master and advances on a skipped master |
| `takes_input()` | declared: a processor source that renders the other slot's audio (a resonator); false everywhere |

`BasicSourceBlock<B>` holds the common inputs (buffer, stereo layout and pan, the amplitude ramp, the pitch, an
optional `SyncMaster`, scratch, the noise stream) and a small block per source type (wavetable bands and morph, DX7
patch state, a sample's porta and cache inputs, the live input's interpolation size).

### Source slots (`src/synth/sources/source_slot.h`)

A unison part (`UnisonPart`, `src/synth/voice/unison_part.h`) has two `SourceSlot`s. A slot holds:

- the note's **oscillator source** in place, in a `std::variant` of the port-free sources (VA, wavetable, supersaw),
  built at note-on and kept across notes while its class is unchanged, so a free-running oscillator keeps its phase;
  a VA wave change on a re-triggered voice keeps the built wave;
- the **port-backed sources** (sample, DX7, input), bound to the firmware's ports for the voice's life;
- the phase state every mode shares (`SlotPhase`: `osc_pos`, the stored increment, FM's carrier feedback, `active`).

`source_for(type)` is the source that renders a slot's current type: the port-backed one by type, else the held
oscillator, whatever its class; null when the slot holds none (a mode change under a live voice can leave it so), which renders nothing.

### Engine (`src/synth/engines/engine.h`)

`BasicEngine<B>`:

| Member | Does |
|---|---|
| `note_on(const BasicEngineNote<B>&) → bool` | starts the note over the parts the voice lends; false when it cannot play |
| `calculate_phase_increments(note)` | each part's increments, at note-on and on a note change |
| `randomize_part(u, noise)` | a new voice's random phases beyond its sources' (drawn after theirs) |
| `render(const BasicEngineBlock<B>&) → EngineRenderResult` | adds one block into the voice's buffer; reports a stereo buffer and whether every part's sources ended |
| `release(parts, info, rating)` | the note-off's source releases |
| `owns_envelope()` | declared: the engine shapes the amplitude itself (a physical model's decay); false everywhere, not read yet |
| `sound_stage()` | declared: a `BasicSoundStage<B>` the instrument would run once per sound after its voices; null everywhere |

`BasicEngineBlock<B>` carries the voice's buffer and scratch, the unison parts and the shared `SourceLevels` it lends,
the voice config and per-voice block, the noise stream, and one control struct per family (`DualSourceControls`,
`FmControls`, `engine_controls.h`), of which the voice fills its family's.

## The engines

- **`SlotEngine`** (`slot_engine.h`): the per-source note-on every engine here shares — each source marked active in
  every part where it can sound, its phase increments worked out, its slot started — over the Deluge voice's slots.
- **`DualSourceEngine`** (`dual_source_engine.h`): the per-source loop through the `Source` contract, the A→B osc-sync
  capture, the noise source, the level pre-roll helpers and the sample/DX7 releases.
  - **`SubtractiveEngine`**: the mono sources, then the stereo ones over a stereo copy, source 1 optionally synced to
    source 0, plus noise; the voice filters afterwards.
  - **`RingModEngine`**: each part's two raw waves (source 1 optionally synced) multiplied and panned, plus noise; a
    source that cannot sound fails the note.
- **`FMEngine`** (`fm_engine.h`): a whole-voice engine. It ignores the slots' sources: its carriers render over each
  slot's `osc_pos` and carrier feedback, and its modulator grid (phases, increments, feedback, level de-zipper) is its
  own. It seeds and randomizes the modulators and works out their increments.

The voice holds **one of each** in an `EngineSet` (`engine_set.h`), whose `for_mode()` is the factory. A sound's mode
can change under a sounding voice (undo, a preset applied, a reprojection from the record — `Sound::setSynthMode` is
the only path that stops the voices), so every engine keeps its state while another plays, and the unison parts and
the source levels (`SourceLevels`, the de-zipper FM and the dual engines share) stay the voice's, lent per call.

## What stays in the voice

Envelopes, local LFOs, the patcher and param planes, MPE smoothing, porta, the pitch adjust, the play-once sample's
auto-release, the overall amplitude (with FM's unison compensation, which FM works into its carriers), the filter set,
wavefold, saturation, pan and the output stage, the cull stamp and rating, and the decision to unassign or fast-release
when every part's sources ended. It derives each engine's controls from its param planes
(`Voice::dual_source_controls`, `Voice::fm_controls`), including the sample-cache test over the pitch cables.

## Adding to it

- **A port-free source** (Karplus-Strong, a Plaits model): a class deriving `Source`, an alternative in
  `SourceSlot::Oscillator`, and a case in `SourceSlot::build_oscillator`. The voice and the engines are unchanged.
- **An engine**: a class deriving `Engine` (or `SlotEngine` to reuse the slot lifecycle) and a case in
  `EngineSet::for_mode`, with its control struct if it needs one; `tests/dsp/synth_engine_spec.cpp` drives an engine
  and a source written against the contracts alone.
- **The declared hooks** come into use with the engines that need them: `takes_input` with exciter→resonator,
  `owns_envelope` and `sound_stage` with a physical model.

## The float instrument plugin

A float instrument instantiates the same contracts over its own backend (float samples and controls, its phase type,
its own per-type blocks, voice and controls), with its own voice deriving controls from its own params (no Deluge param
indices, no legacy format, per-block ramps) and sources built from dsp-next's `float/` backend. The tiers, the source
lifecycle, the slots, the engine families and the declared hooks carry over. See `TODO.md`.

## Tests

- **Golden masters** (`golden-synth`, `golden-synth-arm`): byte-identical on x86 and ARM. The
  `voice/` and `stereo/` cases pin the engines' own paths: ring-mod sync (mono and spread), osc sync to a silent master,
  noise without a filter and across a spread unison, a DX7 patch in osc 2, a retriggered supersaw, a mono voice's osc 1
  turned saw → square (keeps its saw) and saw → supersaw (builds one) between notes (`RENDER_SETTING`), and FM
  re-triggered on a mono sound.
- **`tests/dsp/synth_engine_spec.cpp`**: an engine and a source the voice does not know, started over the voice's slots
  and rendered; each family through a voice (subtractive mixing and syncing, ring-mod multiplying, FM modulating); a
  voice whose sound changes mode while it sounds. `tests/dsp/synth_voice_spec.cpp` plays the voice's lifecycle.
