# Synth Untangling — Phase 1: `dsp-next` (the clean L0 library)

Status: **planned** (2026-06-13, revised). Companion to
[synth_untangling_plan.md](synth_untangling_plan.md) Part 4, Phase 1.
Prerequisite: Phase 0 golden masters (done) — every migration below is gated
on **bit-exact** 319-fixture renders, cross-checked against the ARM/NEON
oracle.

## Strategy: build `dsp-next`, strangle the old `dsp/`

Rather than scrub the accreted `src/deluge/dsp/` in place, stand up a **new,
clean L0 library — `dsp-next` — and migrate primitives into it one at a
time**. Each primitive is ported as idiomatic C++23, its caller is switched
over under golden-master cover, and the old implementation is deleted once no
caller remains. When `dsp/` is empty (its sample-path residue having moved to
the Phase-3 `SampleSource` home), it is removed and `dsp-next` becomes the
canonical DSP library.

Why a parallel library instead of in-place refactor:
- The old code fights you — raw `int32_t*`+len, file-scope scratch, global
  `AudioEngine::` reads, `offsetof` plumbing. A greenfield home is **clean by
  construction**: the include-lint gate has no allowlist, the idioms are
  enforced from commit one.
- Migration is incremental and always shippable: at any point the firmware is
  a mix of old and new primitives, all golden-green.
- It gives the numeric split (below) a real home instead of retrofitting it.

## Library shape: `fixed/` and `float/` behind a numeric-agnostic core

```
src/deluge/dsp-next/
  core/    headers only — numeric-agnostic interfaces, concepts, block/span
           types, strong numeric aliases, q31<->f32 conversion. No globals.
  fixed/   q31 implementations            -> lib  libdeluge-dsp-fixed
  float/   f32 implementations            -> lib  libdeluge-dsp-float
```

- **`core/`** defines the contracts both numeric backends satisfy: typed
  aliases (`Sample`, `ModValue`, `ControlValue` — never bare `int32_t`/`float`
  in a public signature), block types over `std::span`, the engine/primitive
  concepts, and the q31↔f32 edge conversions (one NEON op per vector). This is
  decision 1's "numeric-agnostic interfaces" and the "q31 is an implementation
  detail, never a public type" rule made physical.
- **`fixed/`** is the bit-exact q31 core — that *is* the Deluge sound, and it
  keeps golden masters bit-exact against current firmware. Most current `dsp/`
  kernels migrate here.
- **`float/`** is the float32 track. It is **not** bit-exact with `fixed/`
  (different numerics, validated by tolerance/perceptual, not SHA) and is used
  at the edges (Plaits-style engines, CLAP). Phase 1 stands `float/` up
  structurally and lands the conversion helpers + one reference primitive as
  proof; **populating it is later work** (decision 3 keeps the core q31 for
  now) — the point here is that the home exists and the interface is shared.

Both are STATIC libs mirroring the existing `deluge_dsp` target (unity build,
`-fno-lto -finline-functions`, `-marm` on hardware, link `argon`); the app and
host-sim link whichever primitives have migrated. Targets: `deluge_dsp_fixed`,
`deluge_dsp_float` (output names `libdeluge-dsp-fixed`, `libdeluge-dsp-float`).
Follows the repo's existing multi-lib pattern (`deluge_midi`,
`deluge_foundation`).

## Idioms enforced in `dsp-next` (the migration's core focus)

- **C++23 throughout** (`gnu++23`, as the repo already builds).
- **`std::span` for every buffer** — no `int32_t*, numSamples`, no
  `bufferEnd` sentinels, no out-params via pointer. Block APIs take/return
  spans; lengths come from `.size()`.
- **Value types, no globals.** Scratch buffers, the render-quality input (the
  old `AudioEngine::cpuDireness`), stereo-ness (`renderInStereo`), and the
  allocator are **injected** as parameters/constructor args. `spareRenderingBuffer`
  has no analogue here.
- **Strong numeric typing** via `core/` aliases so the float track is an
  interface-preserving impl swap, not a signature change.
- **`std::array`/constexpr tables**, RAII, `[[nodiscard]]`, `std::optional`
  over sentinel returns where it fits. Match the reverb's existing style
  (header-only value types) — that is already the target idiom in-tree.
- **Unit-testable in isolation**: every primitive constructible and renderable
  in a host test with zero firmware scaffolding.

## What migrates, in order (dependency-led, lowest coupling first)

Source material is the same `dsp/` audit as before — see the violation table
at the end. Migration order:

1. **`core/` foundations** — numeric aliases + `SampleType` concept
   (`core/sample.h`, reusing the firmware-wide `q31_t`) and q31↔f32 edge
   conversion (`core/convert.h`). **Lookup tables migrate later, with their
   consuming primitive** (not up front): the wave tables are entangled in a
   grab-bag `util/lookuptables/lookuptables.h` that also drags `gui/l10n` +
   `model/iterance`, and the FM sine table lives inside `SineOsc` itself — so
   the clean move is per-primitive (the sine table travels with the sine osc in
   S2, which is the first thing migrated anyway), not a speculative
   `lookuptables` surgery. Block/span helpers and primitive concepts are also
   deferred until there are ≥1 primitives to shape them against
   (over-abstraction risk, per Risks). `core/` stays deliberately lean.
2. **Oscillator domain** — the sine kernel (done, S2) plus the basic VA waves
   (saw/square/triangle/analog) and the render kernels that currently live,
   wrongly, in `processing/render_wave.h` + `vector_rendering_function.h`.
   Rewritten into `fixed/oscillators/` as per-wave kernel+LUT primitives over
   `std::span`, with the VCA separated from generation and the `OscType`
   megaswitch broken into a kernel registry. **This is the active thread after
   S2** — detailed plan: [synth_oscillator_plan.md](synth_oscillator_plan.md).
3. **Filters** (SVF, lp/hp ladders) — `cpuDireness` injected; per-filter mode
   enums (not the `gui/l10n`-dragging app `FilterMode`); global `blendBuffer`
   → injected scratch. **Deferred to a later cohesive arc**: it's a coupled
   subsystem (`union{ladder,svf}` in `FilterSet`, shared tan/tanh LUTs), best
   migrated as one unit, not interleaved with the oscillator work.
4. **Wavetable** — `core::WaveTableView` POD (band descriptors + cycle
   metadata); the band-limited render kernel (today a *method of
   `storage::WaveTable`*) moves into `fixed/` operating on the view;
   `storage::WaveTable` keeps loading/stealing and exposes `view()`. Inverts
   the `dsp → storage` edge entirely.
5. **Delay / reverb / compressor FX kernels** — injected scratch span and
   stereo flag; tempo-sync resolved by the caller to a plain rate (the kernel
   stops including `model/sync.h`).
6. **DX7** (`dx/`) — port or thin-wrap into `fixed/`; keep the
   `DELUGE_DX_SCALAR` host/asm split intact (see golden harness notes).

Each primitive: rewrite in `dsp-next` → **write a thorough behavioural spec**
(`tests/dsp/<name>_spec.cpp`, CppSpec) that runs as a CTest test on **both** x86
(SIMDe) and ARM/qemu, locking the kernel's behaviour and cross-checking x86≡ARM
→ switch the caller (oscillator dispatch / `Voice` / `FilterSet` / `WaveTable`)
→ full golden sweep (bit-exact) → delete the old `dsp/` implementation. The spec
is mandatory and lands with (or just before) the migration, not after — it is
the per-primitive regression net. See `tests/dsp/README.md`.

### Deferred to Phase 3 (do not migrate now)
`granular/`, `timestretch/`, and the sample path stay in the old `dsp/` and
keep their `model/sample`, `storage/cluster`, `voice_sample`,
`playback_handler`, granular-reverb-backdoor reaches. They move to the
`SampleSource` home in Phase 3, not into `dsp-next`. `voice.cpp`'s
`spareRenderingBuffer` is app-side (Phase 4). The Sound-level FX chain is a
separate untangling.

## Steps

Bisectable commits; firmware Debug build + full golden sweep (x86 **and** ARM
oracle) green before the next. `git mv`/port commits never carry a behaviour
change.

- **S0 — Scaffold. ✅ done (2026-06-13).** Created `dsp-next/{core,fixed,float}`:
  `core` is a header-only INTERFACE target (`deluge_dsp_core`); `fixed`/`float`
  are STATIC libs (`deluge_dsp_fixed`→`libdeluge-dsp-fixed`,
  `deluge_dsp_float`→`libdeluge-dsp-float`) mirroring `deluge_dsp` (unity,
  `-fno-lto -finline-functions`, `-marm` on HW); an umbrella `deluge_dsp_next`
  INTERFACE links all three and is linked into `deluge_app` (so firmware + sim
  pick it up). Namespaces: `deluge::dsp` (core), `deluge::dsp::fixed`,
  `deluge::dsp::fp` (`float` is a keyword). Include-lint gate
  (`scripts/check_dsp_boundary.py`, **no allowlist**; forbids the app layers,
  legacy `dsp/`, HAL/BSP; `memory/` only via `memory_allocator_interface.h`)
  wired into pre-commit + a CI workflow. Firmware Debug builds green, text
  unchanged (empty libs add no code). The per-primitive unit-spec harness
  (`tests/dsp/`, dual-arch CppSpec via CTest) lands with S2.
- **S1 — `core/` vocabulary. ✅ done (2026-06-13).** Lean header-only
  foundation: `core/sample.h` (`q31`/`f32` aliases reusing the firmware-wide
  `q31_t`, + the `SampleType` concept) and `core/convert.h` (`toF32`/`toQ31`
  edge conversion, with compile-time saturation self-tests — caught and avoided
  the `ONE_Q31f`→2^31 float-rounding overflow at |x|==1). No caller switch, so
  goldens are untouched; firmware text unchanged. LUTs, block helpers, and
  primitive concepts deliberately deferred (see migration item 1).
- **S2 — sine kernel. ✅ done (2026-06-13).** Migrated the sine table evaluator
  into `fixed/sine.{h,cpp}` as free functions (not a class): `sine(Argon<uint32_t>)`
  (the vector evaluator) and `sine_fm_operator(...)` (the scalar FM-feedback step,
  labelled FM-engine-bound). Reframed per review: it's a stateless *evaluator*, not
  an oscillator; vector FM dissolved into `sine(phase + mod)` at the `voice.cpp`
  call sites; phase-advance (the osc concern) decomposed back to the caller; the
  LUT is now a TU-local table; magic numbers became named constants; full `///@`
  Doxygen; snake_case free functions via a scoped `dsp-next/.clang-tidy`. Bit-exact:
  x86 319/319, ARM oracle 5/5 (the sine/fm cases only — the full ARM/QEMU sweep
  is too slow to run per-commit, so it is reserved for phase boundaries). Shipped
  with a thorough dual-arch unit spec: `tests/dsp/sine_spec.cpp` (CppSpec, CTest)
  runs on x86 (SIMDe) and ARM/qemu, both green.
- **Oscillator domain** (the active thread): the VA wave kernels + LUTs, the VCA
  separation, and the megaswitch → kernel-registry rewrite. Staged
  in-place-refactor-first (Phase A, bit-exact vs existing hashes) → migrate
  (Phase B) → unify VCA (Phase C). Its own detailed plan:
  [synth_oscillator_plan.md](synth_oscillator_plan.md). Phase C (unifying the
  VCA arithmetic) is the first **deliberate golden re-baseline** — ear +
  hardware, not SHA. dsp-next kernels are **stateless** (state → L1 SourceEngine).
- **Filters, then FX, then DX7** — later clusters (filters as one cohesive
  subsystem arc), per the migration order above. The first caller switch of
  each can move a fixture — gate hard.
- **Sn — Retire.** When the only thing left in old `dsp/` is the deferred
  sample residue, the lint allowlist for `dsp/` is gone; the final cleanup
  (delete emptied `dsp/`, rename/settle `dsp-next`'s canonical name) is its
  own commit once Phase 3 has relocated the residue.

## Test & gate strategy

- **`fixed/` primitives** are golden-bit-exact gated on caller-switch, and
  each gets a dual-arch CppSpec unit spec (`tests/dsp/`, run as CTest on x86
  SIMDe + ARM/qemu) proving behaviour and x86 ≡ ARM in isolation.
- **`float/` primitives** are validated by tolerance/perceptual comparison,
  never SHA — they are a different numeric track by design.
- **Mechanical commits** (scaffold, LUT move, port-without-switch) expect
  byte-identical fixtures; **caller-switch commits** are the ones that can
  legitimately need a documented rounding waiver (then re-baseline once, per
  the Phase-0 contract).
- The `dsp-next` include-lint gate runs in CI from S0.

## Risks

- **Two implementations of one primitive** (old `dsp/` + new `dsp-next`)
  during migration — mitigated by switching the caller in the *same* commit
  that proves the new one bit-exact, and deleting the old promptly so there is
  never a silently-diverging duplicate.
- **Bit-drift porting q31 kernels** — same risk as before; the per-primitive
  CppSpec unit spec catches host/ARM divergence early, goldens catch
  end-to-end drift. Treat any delta as guilty until shown a faithful reorder.
- **`core/` interface churn** — getting the span/typed-alias contracts wrong
  forces re-touching every migrated primitive. Land `core/` (S1) deliberately;
  port two dissimilar primitives (an oscillator and a filter) early to
  pressure-test the contract before the bulk migration.
- **Over-investing in `float/`** now — keep it to structure + conversions +
  one reference primitive; the real float work is later (decision 3).
- **CPU headroom** — block-granularity only; `std::span` and value types are
  zero-overhead. Goldens double as perf canaries; spot-check on hardware.

## Appendix — current `dsp/` L0 violations (migration source material)

| File / area | Upward reach | Becomes |
|---|---|---|
| `oscillators/oscillator.*` | `render_wave.h`, `audio_engine.h` (`cpuDireness`×5), `wave_table.h` | `fixed/` osc + wavetable view |
| `oscillators/basic_waves.cpp`, `processing/render_wave.h` + `vector_rendering_function.h` | dsp code under `processing/` | rewritten into `fixed/` over spans |
| `filter/lpladder.cpp` | `audio_engine.h` (`cpuDireness`) | injected quality arg |
| `filter/filter*.h` | `model/.../filter_config.h` (drags `gui/l10n`) | POD config in `core/` |
| `delay/delay.*` | `audio_engine.h` (`renderInStereo`), `model/sync.h`, `spareRenderingBuffer` | injected scratch/flag/rate |
| `delay_buffer.cpp`, `dx/engine.cpp` | `memory/general_memory_allocator.h` | the `memory_allocator_interface.h` seam |
| `util/functions.h`, `util/lookuptables/` | export `gui/colour`, `gui/l10n`, `model/iterance` | wave LUTs move into `dsp-next` |
| `granular/`, `timestretch/` | sample path | **deferred to Phase 3**, not migrated |
