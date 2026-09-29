# Oscillator-domain migration (dsp-next, Phase 1)

Status: **planned** (2026-06-13). Companion to
[synth_phase1_l0_plan.md](synth_phase1_l0_plan.md). Follows the sine kernel (S2,
done). The filter subsystem is deferred to a later cohesive arc — see that plan.

Goal: migrate the rest of the oscillator domain into `dsp-next/fixed/oscillators/`
as clean, modular, per-wave **kernel + LUT** primitives (the shape sine took),
breaking the rendering out of the `OscType` megaswitch and **separating the VCA
(amplitude) from wave generation**.

## Current architecture (what we're fixing)

`Oscillator::renderOsc` (`dsp/oscillators/oscillator.cpp`, ~480 lines) is one
function switching on `OscType`, interleaving five concerns in nested branches:
1. **band-limit table selection** (`getTableNumber` → `sawTables[n]` etc.),
2. **crude-vs-antialiased** choice, gated on `AudioEngine::cpuDireness`,
3. **osc sync** (`renderOscSync` crossover), 4. **pulse width**, and
5. **amplitude (VCA)**.

Problems:
- **VCA baked in early.** Amplitude is applied *inside* every render loop via
  fused multiply-accumulate — `renderWave`/`renderPulseWave` (`vqdmulhq` + `vadd`
  into the mix buffer), the crude scalar paths
  (`multiply_accumulate_32x32_rshift32_rounded(*thisSample, wave, amp)`) — and it
  accumulates into the destination (for unison/osc summing). The oscillator can
  never emit a clean wave; gain + mix are welded to generation. Every path is
  doubled by an `if (applyAmplitude)` branch.
- **Megaswitch.** Adding/altering an engine means surgery across the switch; the
  `OscType` enum conflates true oscillators with `SAMPLE`/`INPUT_*`/`WAVETABLE`.
- **L0 violations.** Render kernels live in `processing/render_wave.h` +
  `vector_rendering_function.h`; `AudioEngine::cpuDireness` read directly; wave
  LUTs sit behind the gui-dragging `util/lookuptables`; a global
  `oscSyncRenderingBuffer` + `SSI_TX_BUFFER`/`CACHE_LINE_SIZE` board constants.
- **Two sine paths.** The FM/vector `SineOsc` (`sineWaveDiff`) is migrated (S2);
  `renderOsc`'s band-limited `SINE` case (a separate `sineWaveSmall` table via
  `renderWave`) is **not** — they are different tables and code.

## Decision: stateless kernels (state lives one layer up)

The fixed DSP library holds **stateless kernels** — pure functions, the caller
owns the phase accumulator, exactly like `sine()`. Wave generation, band-limit
table selection, the osc-sync crossover render, pulse-width math, and the VCA
are all pure-given-inputs and live here. **No owned state in `dsp-next/fixed`.**

The **stateful oscillator** — owning phase / unison sub-phases / feedback across
blocks, advancing them, composing the kernels — is the **L1 `SourceEngine`**
(Phase 3, Plaits-style: `Init(allocator)` + `Render`). It is a higher layer, not
in `dsp-next/fixed`; putting state here would blur L0/L1 and cost
composability/testability. Today that stateful role is `Voice` (it holds
`oscPos`/`modulatorPhase`); it becomes `SourceEngine` later. So the kernels
migrated here are phase-in/out functions — the same kernel can back an
oscillator engine, an FM operator, or an LFO without inheriting a state model.

## Decision: separate the VCA from the oscillator kernels

The early-baked VCA is too rigid. Target:
- **Kernel:** `(phase, increment, …) → raw wave block` (`std::span`), no
  amplitude, no mix — exactly sine's shape today (caller owns the phase).
- **VCA + mix stage:** one shared, vectorised op `mix[i] += wave[i] * amp[i]`
  with a ramped amplitude (generalising the existing
  `applyAmplitudeVectorToBuffer`). The single place gain is applied.
- The `applyAmplitude` flag disappears: "raw" is just "don't run the VCA stage."

Why: modularity (composable osc → gain → mix graph), the L1 SourceEngine model
(foreign engines emit a wave; the voice owns the envelope/VCA), and it deletes
the dual-path branching. Sine already works this way.

**Bit-exactness caveat (important).** The codebase applies the VCA with *two*
different arithmetics today — `vqdmulhq_s32` on the vector/table paths and
`multiply_accumulate_32x32_rshift32_rounded` on the crude scalar paths. Unifying
them is a rounding-level change → **not bit-exact** on some paths. This is the
migration's first deliberate golden **re-baseline**, validated by ear +
hardware, not SHA (allowed by the Phase-0 contract). Staged to contain it:

- the *structural* raw/VCA split is done **bit-exact first**, each path keeping
  its current arithmetic (gen into a scratch block, then that path's exact MAC
  over it — reordering identical ops across a temp is bit-identical);
- **then** a separate, clearly-flagged commit unifies the VCA arithmetic, with a
  one-time re-baseline + an ear check. The diff is isolated and reviewable.

Performance: the fused MAC's ~20% win (per the in-code comment) is real on the
simple non-sync paths, but the complex paths already two-pass via
`oscSyncRenderingBuffer`; the unified VCA stage is tight 4-wide NEON over a
cache-hot scratch block. Validate with a hardware voice-count/CPU spot-check
(the goldens double as a perf canary). Only if a real regression appears do we
add a fused `generate+scale+mix` fast path — built as an explicit composition
on top of the clean kernel, never welded back into the kernel contract.

## Target structure (`dsp-next/fixed/oscillators/`)

Per-wave **kernel + LUT** combos (each owns its band-limited table set, like
sine owns `sineWaveDiff`):

| Kernel | LUT(s) | crude path |
|---|---|---|
| `saw` | `sawTables[]` (`sawWave215…1`) | phase ramp (`phase >> 1`) |
| `square` | `squareTables[]` | `getSquare`/`getSquareSmall`, + pulse-width variant |
| `triangle` | `triangleWaveAntiAliasing*` | `getTriangleSmall` |
| `analog_saw` | `analogSawTables[]` (mystery-synth A/B) | falls back to `saw` |
| `analog_square` | `analogSquareTables[]` | — (no crude swap) |
| `sine` (band-limited) | `sineWaveSmall` | — (distinct from the FM `SineOsc`) |

Shared machinery, factored **out** of the megaswitch into reusable pieces (not
per-type branches):
- the vector wave-render core (`waveRenderingFunctionGeneral`/`Pulse`, the
  `renderOscSync` crossover) relocated from `processing/` into `dsp-next`;
- `bandLimit` — `getTableNumber` + table pick + the crude/antialiased decision,
  taking an **injected quality** input (today `AudioEngine::cpuDireness`);
- `pulseWidth` setup; the osc-sync wrapper;
- the **VCA+mix stage** (the one amplitude application).

**Dispatch:** replace the `OscType` megaswitch with a per-type kernel selection
(a small registry / `kernelFor(type)` table, not a giant switch). This is the
seed of the L1 `SourceEngine` interface — each wave is a kernel the dispatcher
composes with band-limiting, sync, PW, and the VCA stage.

## Target render architecture (the composition)

`renderOsc` today fuses five orthogonal concerns into one megaswitch, and
`storageFunction` is a leaky bridge between two of them. The target separates
them into composable, mostly-pure stages over `std::span` blocks:

| Concern | Today (tangled) | Clean component (stateless L0) |
|---|---|---|
| wave generation | per-`OscType` arms + `waveRenderingFunction*` + `basic_waves` | one **kernel per wave**, each owning its LUT(s): `phase → sample(s)` |
| band-limiting / quality | `getTableNumber` + scattered `cpuDireness` checks | `bandLimit(pitch, quality) → band`, pure, quality injected |
| hard sync | `renderOscSync` + the `storageFunction` overshoot loop | a generic **sync combinator** over a block-fill (below) |
| pulse width | `doPulseWave`/`doOscSync=true`/dual-square inline | a PW transform (largely *reuses* sync) |
| amplitude / VCA | fused `vqdmulhq`/MAC in every loop | the separate **VCA+mix stage** |

Pipeline: `phase ─▶ wave kernel ─▶ [sync] ─▶ [pulse width] ─▶ VCA+mix ─▶ out`.
Each stage is a pure function over span blocks; the orchestrator only composes.

**Retiring `storageFunction` (the crux).** The mess is that hard-sync needs
"fill a contiguous run of samples from phase P" while the wave kernel is "sample
at phase", so the caller hand-writes an overshooting `do-while` callback (writes
4-at-a-time past the window; the `+4` pad on `oscSyncRenderingBuffer` exists to
absorb it). Replace with one clean contract — a `WaveRenderer` that fills a block
**respecting its span bounds** (mask the partial tail; no overshoot, no pad):

```cpp
concept WaveRenderer = requires { fill(std::span<q31> out, uint32_t& phase, ...); };

void renderSynced(WaveRenderer auto&& fill, SyncParams sp, std::span<q31> out, uint32_t& phase);
```

`renderSynced` slices `out` into per-sync windows and calls `fill(window, phase)`
per window, applying the crossover fade between windows. This keeps the existing
*higher-order* instinct (which is correct — wavetable's cross-cycle fill is also a
`WaveRenderer`, the very reason the hook exists) but: the overshoot and `+4` pad
disappear, the general/pulse/wavetable copy-paste collapses to one `fill` each,
and `phase` is threaded once instead of computed-and-discarded.

State: everything here is **stateless L0** (pure over spans). The phase
accumulator + unison/feedback state belong to the L1 `SourceEngine` (Phase 3),
which owns phase and composes these stages into `Render(block)`; `renderOsc`
dissolves into that composition.

This redesign is **Phase B work** — it is relocation-coupled, and the overshoot
is load-bearing, so it is not a safe in-place bit-exact edit. It stops being
scary once the wave kernels exist as clean `WaveRenderer`s: `renderSynced` is
then a small generic combinator, not a callback with a hidden overshoot contract.

## The shared `core/` concept

With sine (done) plus these wave kernels, the generator concept is now real:
a wave kernel advances phase and fills a raw `std::span` block. Define it in
`core/` here. (The *processor* concept — stateful, in-place — lands with the
deferred filters; don't force a unified concept before then.)

## Migration sub-steps

Sequenced to isolate the *behavioural* restructure from the *relocation*: do the
risky structural work **in place first**, gated bit-exactly on the existing x86
golden hashes (no relocation/SIMDe/ARM/namespace confounds), and only then move
clean code into `dsp-next`. Each step is a bisectable commit.

### Phase A — in-place structural refactor (old `dsp/`, gated on the existing x86 hashes)
Pure behaviour-preserving restructuring of `oscillator.cpp`/`basic_waves`; the
existing fixtures are the ground truth, so any hash change is a refactor bug,
caught with the cleanest possible signal.
- **A1 — de-switch `renderOsc`.** Remove the dead labels and replace the
  `callRenderWave` goto target with a `renderFromTable` helper, so `renderOsc`
  is a goto-free `if/else-if` dispatcher. (Done — two bit-exact commits.) The
  remaining inline per-wave arms need not be extracted in place; they become
  `dsp-next` kernels in Phase B.
- **A1b — untangle `renderOscSync`.** The hard-sync crossover engine (in
  `render_wave.h`, shared by oscillators **and** wavetables — 4 call sites) has
  a backward `goto startRenderingASync` loop. Convert it to a structured loop in
  place; bit-exact, gated on the sync golden cases (Sync Bass, Saw/Square Sync,
  Sync Squared, …). It is stateless (phase in/out by ref) and a higher-order
  function (parameterised by a per-window wave renderer) — keep that factoring;
  in Phase B it becomes a first-class `dsp-next` osc-sync component with its own
  spec, used by both the oscillator kernels and the wavetable renderer.
- **A2 — separate the VCA, per-path-exact.** Each wave generates a raw block into
  scratch, then that path's **existing** VCA op runs over it; delete the
  `applyAmplitude` dual-path (raw == "skip the VCA stage"). Reordering identical
  ops across a temp is bit-identical → still gated on the same hashes.

### Phase B — migrate to `dsp-next/fixed/oscillators/` (dual-arch spec + golden gate)
Now near-mechanical: relocate the already-split, already-clean pieces.
- **B1 — render core + LUTs.** Move the vector wave-render core
  (`waveRenderingFunctionGeneral`/`Pulse`, `renderOscSync`) and the per-wave LUTs
  in as `dsp-next`-owned data (out of `processing/` and `util/lookuptables`).
- **B2 — per-wave kernel+LUT primitives.** `saw`, `square`(+PW), `triangle`,
  `analog_saw`, `analog_square`, band-limited `sine` — each a stateless
  `std::span` kernel + its table; plus the shared `bandLimit` (injected quality),
  osc-sync wrapper, PW helper, and the **VCA+mix stage**. A dual-arch CppSpec per
  kernel. Switch `renderOsc`/`Voice`; delete old `oscillator.cpp`,
  `basic_waves`, `render_wave.h`. The dispatcher becomes a kernel registry
  (`kernelFor(type)`), not a switch.

### Phase C — unify the VCA arithmetic (the one deliberate re-baseline)
Collapse the two VCA arithmetics (`vqdmulhq` vs `MAC-rounded`) into one. **Not
bit-exact** → one-time golden re-baseline + an ear check + a hardware
voice-count/CPU spot-check. Isolated, clearly-flagged commit.

### Later — wavetable
The `WaveTable` → `core::WaveTableView` inversion (kernel moves to `dsp-next`,
storage keeps loading). Its own arc.

## Deferred / out of scope here
The filter subsystem (its own cohesive arc), `SAMPLE`/`INPUT_*` sources (the
`SampleSource` port, Phase 3), and the L1 `SourceEngine` C-ABI shaping (Phase 3+).
