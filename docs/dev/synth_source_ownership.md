# The OO Source model — owned params + dsp-next residence (end-state design)

> Status: **design / target architecture.** Prompted by the question: *is the `SourceComponent` dispatch boundary
> the right way, when do Sources own their own parameters, and when do components migrate to dsp-next?* This doc
> defines the end-state so the `synth::Voice` lift (`synth_voice_lift.md`) doesn't bake in the legacy flat-param
> model, and so the OO/dsp-next migration has a target. It separates three concerns the earlier
> `synth_source_component_interface.md` conflated: **dispatch boundary** vs **param ownership** vs **dsp-next
> residence**.

## The three concerns (kept separate)

1. **Dispatch boundary** — how the voice calls a source's render (polymorphic `SourceComponent` vs the oscType
   switch) and how app-coupled sources reach storage/`AudioEngine` (ports). *Needed for the lift.*
2. **Param ownership** — whether a source reads its modulated params from the Voice's **flat plane** by global
   index, or **owns typed params** fed to it. *The OO question; gated on the patcher.*
3. **dsp-next residence** — whether the DSP primitive lives in `dsp-next/` owning its state + config. *The dsp-next
   track.*

These are orthogonal. The lift only needs (1)-as-ports. (2) and (3) are the OO migration and should not be
retrofitted onto the lift over the legacy layout.

## Current model (what we'd be migrating from)

- **Config** lives on `Source` (`processing/source.h`: oscType, sampleControls, repeatMode, transpose, dxPatch,
  ranges) and `Sound` (lfoConfig, modulator routing, …) — preset state.
- **Modulated per-block values**: the **Patcher** reads a `ControlBlock` (patched preset values + cables) and writes
  a **flat** `paramFinalValues[]` (`ParamPlane`/`VoiceParams`, `synth/param_plane.h`), indexed by the global
  `LOCAL_*` enum. Components read their slice by **A/B offset**: `paramFinalValues[LOCAL_OSC_A_PHASE_WIDTH + s]`,
  `LOCAL_CARRIER_0_FEEDBACK + s`, etc.
- The flat plane is consumed by **distinct concerns** that a typed model would separate:
  - per-source oscillator — `OSC_A/B_{VOLUME, PHASE_WIDTH, PITCH_ADJUST, WAVE_INDEX}`
  - FM — `MODULATOR_0/1_{VOLUME, FEEDBACK, PITCH_ADJUST}`, `CARRIER_0/1_FEEDBACK`
  - filter (voice-level, post-source) — `LPF/HPF_{FREQ, RESONANCE, MORPH}`
  - envelopes — `ENV_0/1_{ATTACK, DECAY, SUSTAIN, RELEASE}`
  - voice-level — `VOLUME, PAN, PITCH_ADJUST, NOISE_VOLUME, FOLD`

The patcher is **built around this flat `LOCAL_*` layout** — that's why param ownership is a patcher-era change,
not a lift change.

## End-state: typed, source-owned params

- Each **L0 dsp-next component** owns its DSP state + config and consumes a **typed per-block param input** — its
  modulated values as a struct (e.g. a VA osc takes `{ pulseWidth, pitchIncrement, … }`, a wavetable adds
  `waveIndex`), *not* a flat array + `LOCAL_*` offset. The component has **no knowledge of the Deluge param enum**.
  (This is the L1-doc `BlockControls` split: oscillator-only params move to the component layer; amplitude/
  cpu-direness stay engine-level.)
- The **L1 `SourceComponent`** composes the L0 component + (for app-coupled sources) the `SampleSource`/live-input
  **ports** + the **param mapping** that produces the typed input.
- The **L2 voice kernel** composes `SourceComponent`s and owns the voice-level concerns (volume/pan/pitch, the
  filter, the envelopes) — which stay voice-level, not source-owned.
- **Modulation sources** (`sourceValues[]`: env/LFO/velocity/…) stay a voice/patcher concern; the patcher combines
  them into the typed param inputs.

## How the patcher feeds typed params (the crux)

Two stages, strangler-fig:

- **Stage A — bridge (cheap, bit-exact, no patcher change):** the patcher keeps writing the flat plane; a small
  per-component **param view** maps the flat slice → the typed struct at the source seam (exactly the
  snapshot-from-source-of-truth pattern `VoiceConfig` already uses). Components consume the typed struct. This
  proves the typed shapes + lets components stop indexing `LOCAL_*` — *behind* the interface, with the patcher
  untouched.
- **Stage B — native (the modulation rework):** the patcher writes per-component typed structs directly and the
  flat local plane is retired. Invasive (the patcher/cable engine is built on `LOCAL_*`); its own phase, gated on
  Stage A. May never be fully necessary if the bridge cost is negligible.

## dsp-next residence

The L0 primitives are the dsp-next citizens — `VirtualAnalogEngine` (already `synth::`), `WavetableEngine`
(`dsp::`), the band-limited oscillators (migrated). The **typed-param-input shape is precisely what lets them be
pure dsp-next** (no Voice / param-plane / `LOCAL_*` coupling). The dsp-next track (`[[wavetable-storage-port]]`,
`[[dsp-next-fixed-layout]]`) reworks each primitive to own its bits + consume typed params; this OO model is the L1
seam that sits above them.

## Relationship to the lift (important)

**The lift does NOT need param ownership or the polymorphic interface.** The oscType switch reads `cfg.oscType[s]`
(already snapshotted) — it doesn't trip the boundary. What trips `check_synth_boundary` is the app reaches inside
the sample/input engines. So **L-vi = the ports only** (`SampleSource` + live-input + the `renderInStereo`
globals), keeping the oscType dispatch; that makes `synth::Voice` portable + bit-exact. Building the param-owning
`SourceComponent` over the flat plane during the lift would be premature (reshaped when ownership lands).

## Proposed phase ordering (each bit-exact, golden-gated)

1. **Lift (L-vi ports + L-viii relocate)** — portability only; `synth::Voice` upward-ref-free over the *existing*
   flat plane. (`synth_voice_lift.md`.)
2. **OO Source phase A** — per component: define its typed param struct, bridge it from the flat plane at the seam,
   make the component consume typed (stop indexing `LOCAL_*`). Introduce the `SourceComponent` base + polymorphic
   dispatch *here* (now it carries typed params — OO-correct). Per-component slices.
3. **OO Source phase B (optional/with the modulation rework)** — patcher writes typed natively; retire the flat
   local plane.
4. **dsp-next residence** — fold the now-typed-param components into `dsp-next/` proper (ongoing on the dsp-next
   track).

This makes the **L1 engine API** real: a third-party engine receives typed params, not the Deluge's `LOCAL_*`
layout (`[[extensibility-two-boundaries]]`).

## Open questions

- The typed per-component param struct shapes (VA / wavetable / FM / sample) — derive from the `LOCAL_*` reads
  enumerated above.
- Where the param-view/mapping lives (a `synth/` adapter producing the struct from the flat span).
- Whether voice-level params (filter/envelope) also get typed structs, or stay flat (they're voice-level, not
  source-owned).
- How patch-cable de-zippering / smoothing interacts with typed structs (the patcher's per-block smoothing).
- Numerics: q31 now; the typed structs stay numeric-agnostic so the float migration is contained.

## Pointers

- The lift: `docs/dev/synth_voice_lift.md`. The (now-refined) dispatch-boundary sketch:
  `docs/dev/synth_source_component_interface.md`. L1 architecture: `docs/dev/synth_l1_engine_architecture.md`.
  Overall plan: `docs/dev/synth_untangling_plan.md`.
