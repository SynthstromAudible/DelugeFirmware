# dsp-next: the component architecture

Status: **design** (2026-06-13). The target shape for `dsp-next`, reconciling
the aborted `dsp_ng` prototype (`~/GitHub/DelugeFirmware-dspng:src/dsp_ng`) with
the bit-exact migration discipline. Refines the relocation-flavoured parts of
[synth_phase1_l0_plan.md](synth_phase1_l0_plan.md) and
[synth_oscillator_plan.md](synth_oscillator_plan.md).

## Prime directive: structural, not behavioural

This rearchitecture changes **code organisation only** — every migration step is
**bit-exact**, gated on the existing golden hashes (x86) and the ARM oracle.
We adopt `dsp_ng`'s component *shape* but populate each component with the
*current exact DSP math*. No analytic-wave swaps, no VCA-arithmetic unification,
no band-limiting changes here — those are real audible improvements, done
**later** as separate, ear-validated, re-baselined steps. The line: a commit in
this effort must not change a single output sample.

## What we take from `dsp_ng`, and what we change

Take: the typed signal vocabulary, the `generator`/`processor`/`mixer`/
`converter` concepts, oscillators as stateful generators that own their phase,
`TableOscillator` holding the table as a `std::span` (magnitude from `.size()`),
the VCA as a `Processor`, and composition into chains.

Change: `dsp_ng`'s `Generator` had a **virtual `render()` called per sample** —
unacceptable on the Cortex-A9 hot path. We make per-sample/per-vector `render()`
**static** (concept-constrained, always-inlined); virtual dispatch appears
**only at block granularity** (`renderBlock`), used solely where the engine is
selected at runtime (the `OscType` registry). And the kernels are the Deluge's
exact band-limited/sync math, not `dsp_ng`'s analytic placeholders.

## Layer 1 — types (`core/types.hpp`)

```cpp
template <class T> using Sample = T;
template <class T> using Buffer = std::span<T>;        // writable block
template <class T> using Signal = std::span<const T>;  // read-only block
template <class T> struct StereoSample { Sample<T> l, r; ... };
```

with per-backend alias bundles in **`fixed`** (q31) and **`fp`** (f32) — the same
namespaces the kernels live in (`fixed::sine`, …). Layered on the existing
`core/sample.h` (the scalar vocabulary: `q31`/`f32`/`SampleType`, `toF32`/`toQ31`):
sample.h = scalars, types.h = blocks/aggregates, then the component framework.

Sample type at the interfaces is **`q31_t` (= `int32_t`)** (decision Q1): zero
friction with the app's int32 buffers and the kernels' raw ops. `FixedPoint<31>`
is layout-compatible and comes later as a type-safety layer.

## Layer 2 — concepts + dispatch (`core/concepts.hpp`, base classes)

The four `dsp_ng` concepts, value-type / static-dispatch:

```cpp
concept generator;  // render() -> Sample/vector ;  renderBlock(Buffer)
concept processor;  // render(Sample) -> Sample   ;  renderBlock(Signal in, Buffer out)
concept mixer;      // render(a, b) -> Sample      ;  renderBlock(Signal, Signal, Buffer)
concept converter;  // render(In) -> Out
```

- `render()` is **non-virtual**, `[[gnu::always_inline]]`, the SIMD/scalar hot
  path. `renderBlock()` is the loop over `render()` (the default, overridable for
  a tighter vectorised fill).
- **Runtime selection** (the `OscType` megaswitch's replacement) uses a thin
  **block-level virtual interface** — `struct BlockSource { virtual void
  renderBlock(Buffer<q31>) = 0; }` — or a `std::variant` over the concrete
  oscillators. Virtual cost is paid once per block (~32–128 samples), never per
  sample. (Decision Q2: a block-level virtual interface, for extensibility to
  future CLAP/foreign engines.)

## Layer 3 — oscillator components

Each oscillator is a **stateful generator** owning its phase:

- `Phasor` / `Periodic` — the phase accumulator (`advance()` → next phase
  vector; `setPhase`/`setPhaseIncrement`). The single home for phase state.
- `SineOscillator` — wraps the existing `fixed::sine` kernel (already migrated).
- `TableOscillator` — holds `std::span<const int16_t> table` (magnitude derived
  from `.size()`); `render()` is the **exact** `waveRenderingFunctionGeneral`
  gather+interp. Backs band-limited saw/square/triangle/analog (the table
  varies; the kernel does not).
- `PulseOscillator` — the exact `waveRenderingFunctionPulse` (PWM dual-square).
- The **crude** low-freq/LFO variants — the exact `getSquare`/`getTriangleSmall`/
  phase-ramp analytic forms (these are *already* analytic in the current code, so
  using `waves::` here is bit-exact, not a change).

Cross-cutting, as composable pieces (not per-type branches):

- `bandLimit(phaseIncrement, quality) -> band` — the exact `getTableNumber` +
  table pick + crude/antialiased decision; **quality injected** (today
  `AudioEngine::cpuDireness`). Selects which table a `TableOscillator` uses.
- `SyncedOscillator<Osc>` — hard sync as a **component**: a generator that *wraps*
  a generator. It owns the resetter + seam state and a wrapped `Osc` (any block
  generator with a seekable `Phasor`, i.e. `seek()` + `renderBlock()`), restarts
  the wrapped oscillator at each resetter crossover, and crossfades the seam.
  Being itself a `BlockGenerator`, it composes into the registry/chains. This is
  the cleaned `renderOscSync` — `renderBlock` is overridden (windows are
  irregular/block-level) and each window is filled to its *exact* length by the
  wrapped osc's tail-safe `renderBlock`, retiring the `storageFunction` overshoot
  contract. (Landed as a node, not the earlier free-function `renderSynced` sketch
  — a free function taking a generator factory didn't compose like the rest of the
  library.) Will be shared with the wavetable path.
- `pulseWidth` — PWM setup (largely reuses sync).

## Layer 4 — processors & mixers

- `Amplitude` (the VCA) — a `Processor`: `mix += raw * amplitude_ramp`. Holds the
  ramped amplitude. **Bit-exact means preserving the two existing arithmetics**
  (`vqdmulhq` on the vector/table paths, `multiply_accumulate…rounded` on the
  crude paths) for now — either two processors or one parameterised; unify later
  (audible step). This is the VCA separation, realised as a node.
- `Mixer` — the accumulate-into-mix used for unison/oscillator summing.
- Filters, FX → the deferred filter arc; same component model.

## Phase ownership (Q3)

`dsp_ng` puts phase **in** the oscillator (stateful generator). Today `Voice`
owns it (`unisonParts[u].sources[s].oscPos`, `modulatorPhase`). Moving phase into
the generators is the cleanest end state and matches the L1 `SourceEngine`, but
it's a non-trivial `Voice` change (unison sub-phases, retrigger, sync reset).
**Staged:** first the generators *can* own phase, but `Voice` keeps driving them
by setting phase/increment per block (bit-exact — identical phase advance);
later, phase state fully relocates into the engine. Don't do the deep `Voice`
surgery in the structural pass.

## Bit-exactness: how the current math maps in

- gen/VCA split → `Oscillator.renderBlock(raw)` then `Amplitude` over `raw`:
  same ops reordered across a temp buffer ⇒ bit-identical (verified for the
  table path). The `applyAmplitude == false` "raw out" mode is just "don't run
  the `Amplitude` processor."
- `TableOscillator::render` = the exact `waveRenderingFunctionGeneral` (gather,
  `ShiftLeftLong<16>`, `MultiplyDoubleAddSaturateLong`). Not `dsp_ng`'s variant.
- overshoot: the generator fills exactly its `Buffer` (clean tail); the
  overshoot garbage was never in the hashed output, so output is unchanged.
- crude paths use the analytic forms they *already* use ⇒ bit-exact.

## Migration path (all bit-exact, golden-gated)

1. **Core** — `types.hpp`, `concepts.hpp`, the static `generator`/`processor`/
   `mixer` bases + the block-level `BlockSource` interface. Header-only, no
   caller change.
2. **VCA processor** — extract `Amplitude` (both exact arithmetics); switch
   `renderOsc`/`renderWave` to gen-into-temp + `Amplitude`. Bit-exact.
3. **Oscillator generators** — `TableOscillator`, `PulseOscillator`,
   `SineOscillator`, crude variants — each the exact current kernel, dual-arch
   spec'd; switch the matching `renderOsc` arm. Bit-exact per arm.
4. **`SyncedOscillator` (+ `Phasor`)** — hard sync as a generator-wrapping-
   generator node; introduces the seekable `Phasor` (the single home for phase
   state) it needs to re-seed the wrapped oscillator. Retires the general table
   path's `storageFunction` overshoot. Bit-exact. (`bandLimit` table selection is
   deferred to the registry step, where the per-type table choice naturally lives.)
5. **Registry** — replace the `OscType` switch with the block-level source
   registry composing generator + bandLimit + sync + Amplitude.
6. **Retire** old `renderOsc`/`basic_waves`/`render_wave.h`.

Later (separate, ear-validated, re-baselined): VCA-arithmetic unification,
analytic-wave options, band-limiting/sync improvements, `FixedPoint<31>` types,
full phase relocation into the engine, the L1 `SourceEngine`/CLAP boundary.

## Decisions (confirmed 2026-06-13)

- **Q1 — sample type: `q31_t` (= `int32_t`)** at component interfaces. Zero
  friction with the app's int32 buffers; `FixedPoint<31>` is a later, layout-
  compatible type-safety layer.
- **Q2 — dispatch: block-level virtual interface** (`virtual renderBlock`).
  Virtual paid once per block, never per sample; extensible to foreign/CLAP
  engines. `render()` stays static/inlined.
- **Q3 — phase ownership: generators own phase, `Voice` drives them per block**
  (identical phase advance ⇒ bit-exact). Full phase relocation is later.
- **Reuse: port `dsp_ng`'s `core/` in and adapt** (don't reinvent) — adjusting
  the dispatch (static `render`, virtual `renderBlock`) and using the Deluge's
  exact kernels rather than `dsp_ng`'s analytic placeholders.
