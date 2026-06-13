# Synth Untangling — Phase 1: Make L0 True (detailed plan)

Status: **planned** (2026-06-13). Companion to
[synth_untangling_plan.md](synth_untangling_plan.md) Part 4, Phase 1.
Prerequisite: Phase 0 golden masters (done) — every step below is gated on
**bit-exact** 319-fixture renders, cross-checked against the ARM/NEON oracle.

## Goal

Make `src/deluge/dsp/` a true L0: freestanding DSP primitives with **no
upward includes** (no `storage/`, `model/`, `processing/`, `gui/`,
`playback/`, `modulation/`), no global reads (`AudioEngine::*`,
`spareRenderingBuffer`), no global allocator beyond the existing thin
`memory_allocator_interface.h` seam. Buffers, scratch, and render-quality
inputs are *injected*. Each primitive compiles and unit-tests off-target with
zero firmware scaffolding.

### Definition of done

1. Every file under `dsp/` (minus the deferred sample allowlist below) has
   zero includes of `storage|model|processing|gui|playback|modulation`, and
   `memory/` only via `memory_allocator_interface.h`.
2. `AudioEngine::cpuDireness` / `AudioEngine::renderInStereo` are no longer
   read inside `dsp/`; they arrive as parameters.
3. `dsp/` does not reference the file-scope `spareRenderingBuffer`.
4. Wavetable rendering kernels live in `dsp/`; `storage::WaveTable` owns
   loading/stealing and hands the renderer a POD `WaveTableView`.
5. A lint gate (mirroring the libdeluge allowlist approach) fails CI if a
   forbidden include reappears; per-primitive `dsp_diff` drivers exist for
   the oscillator, wavetable, filter, and delay paths.
6. All 319 golden fixtures unchanged on x86 **and** ARM (or any waiver
   documented per-commit and re-baselined once, per the Phase-0 contract).

## Non-goals (deferred, tracked on the lint allowlist)

- `dsp/timestretch/`, `dsp/granular/`, and the sample path generally — these
  belong to the `SampleSource` port (decision 4) and land in **Phase 3**.
  They keep their `model/sample`, `storage/cluster`, `voice_sample`,
  `playback_handler`, and granular-reverb-backdoor reaches for now.
- `voice.cpp`'s use of `spareRenderingBuffer` — app-side, **Phase 4**.
- The Sound-level FX chain (`ModControllableAudio`) — separate untangling.
- The float32 migration — enabled by, not part of, this phase.

## Inventory of current L0 violations (2026-06-13)

| File | Upward reach | Disposition |
|---|---|---|
| `oscillators/oscillator.h` | `storage/wave_table/wave_table.h` (param type `WaveTable*`) | **S7** invert → `WaveTableView` |
| `oscillators/oscillator.cpp` | `processing/render_wave.h`; `audio_engine.h` (`cpuDireness`×5); `wave_table.h` | **S1**, **S3**, **S7** |
| `oscillators/basic_waves.cpp` | `processing/render_wave.h` | **S1** |
| `processing/render_wave.h` + `vector_rendering_function.h` | dsp code in the wrong dir; also included by `voice.cpp`, `wave_table.cpp` | **S1** relocate into `dsp/` |
| `filter/lpladder.cpp` | `audio_engine.h` (`cpuDireness`) | **S3** |
| `filter/filter.h`, `filter/filter_set.h` | `model/.../filter_config.h` (which itself drags `gui/l10n/strings.h`) | **S5** |
| `delay/delay.cpp` | `audio_engine.h` (`renderInStereo`); `model/sync.h`; `spareRenderingBuffer` | **S4** |
| `delay/delay.h` | `model/sync.h` (`SyncType`/`SyncLevel` in `DelayWorkingState`) | **S4** |
| `delay/delay_buffer.cpp`, `dx/engine.cpp` | `memory/general_memory_allocator.h` (vs the interface) | **S2** |
| (transitive) `util/functions.h`, `util/lookuptables/lookuptables.h` | export `gui/colour/colour.h`, `gui/l10n/strings.h`, `model/iterance/iterance.h` | **S6** split L0 subset |

## Ordered steps

Each step is one bisectable commit (or a tight series), green firmware build +
green goldens before the next. Ordered cheapest/safest first so the risky
wavetable inversion (S7) lands on already-clean foundations.

### S1 — Relocate the render kernels into `dsp/` (zero behaviour change)
Move `processing/render_wave.h` and `processing/vector_rendering_function.h`
into `dsp/oscillators/` (e.g. `dsp/oscillators/wave_renderer.h`,
`.../vector_rendering_function.h`). Update the four includers
(`oscillator.cpp`, `basic_waves.cpp`, `voice.cpp`, `storage/wave_table.cpp`)
to the new path. Pure move + include fix; goldens must be byte-identical.
This flips two `dsp → processing` edges into intra-`dsp`, and lets `storage`
and `voice` include *downward* into `dsp`.

### S2 — Downgrade allocator includes to the existing seam
`delay/delay_buffer.cpp`, `dx/engine.cpp` (and any other `dsp/` user) switch
`memory/general_memory_allocator.h` → `memory/memory_allocator_interface.h`
(the arm/thumb boundary header already exposing `allocStealable` etc.). No new
abstraction needed — the seam exists. Mechanical, no behaviour change.

### S3 — Inject `cpuDireness` as a render-quality parameter
`AudioEngine::cpuDireness` is read in `oscillator.cpp` (band-limit table
selection / crude-saw fallback, ×5) and `lpladder.cpp` (oversampling
decision, ×1). Add an explicit quality input to the affected entry points
(`Oscillator::renderOsc`, the ladder filter's per-block setup); callers
(`Voice`, `FilterSet`) pass `AudioEngine::cpuDireness` unchanged. Removes the
`audio_engine.h` include from both files. Behaviour identical → goldens
bit-exact. (This is also the first concrete instance of the cross-cutting
"scheduling/quality policy is an app input, not a library global" rule.)

### S4 — Decouple `delay/` from `audio_engine` and `model/sync`
Three sub-changes:
- `renderInStereo`: pass as a parameter to the delay process call (the only
  read is the ping-pong branch).
- `spareRenderingBuffer`: the delay borrows `spareRenderingBuffer[0]` as a
  working buffer — take an injected `std::span<StereoSample>` scratch arg
  instead; caller supplies it.
- `model/sync.h`: `DelayWorkingState` stores `SyncType`/`SyncLevel` and does
  integer shifts off them. Preferred: the app resolves sync → rate and the
  delay stores a plain rate (delay shouldn't know musical sync semantics).
  Lighter alternative if that ripples too far: relocate the small
  `SyncType`/`SyncLevel` enums to an L0-safe header. Pick during
  implementation; either keeps goldens bit-exact.

Note: `delay/` is FX, and the FX *chain* is out of scope — but `delay` lives
in `dsp/`, so we scrub its includes here without relocating it.

### S5 — L0-clean the filter config dependency
`filter.h`/`filter_set.h` need only `FilterMode` + the mode-count constants
from `filter_config.h`, but that header drags `gui/l10n/strings.h`. Extract
the enum + constants into an L0-clean header (or remove the accidental
`gui/l10n` include if nothing in the config truly needs it) so `dsp/filter/`
depends only on the POD config.

### S6 — Split the L0 subset out of the shared util headers
`dsp/` pulls `multiply_32x32_*` (fine — `util/fixedpoint.h`), but also
`getSine*`, `sineWaveDiff`, `interpolateTableSigned`, and wave lookup tables
from `util/functions.h` + `util/lookuptables/lookuptables.h`, which export
`gui/colour`, `gui/l10n`, and `model/iterance`. Carve the wave/lookup-table
data and helpers the DSP needs into an L0-clean header (e.g.
`util/waves_lut.h`) free of those reaches, and include *that* from `dsp/`.
Scope strictly to symbols `dsp/` references — not a full util untangling.

### S7 — Invert the WaveTable dependency (highest risk)
Today `WaveTable::render()` + `doRenderingLoop[SingleCycle]()` are **methods
of `storage::WaveTable`**, operating directly on its stealable `bands` array;
`oscillator.cpp` only calls `waveTable->render(...)`. Invert it:
- **S7a** Define `dsp::WaveTableView` — a POD snapshot the renderer needs:
  a span/array of band descriptors `{ const int16_t* data, maxPhaseIncrement,
  fromCycleNumber, toCycleNumber, intendedForLinearInterpolation }` plus
  `numCycles`, `numCyclesMagnitude`, `waveIndexMultiplier`. Add
  `WaveTable::view()` producing it. No render move yet.
- **S7b** Move the render kernels into a `dsp/` free function/class operating
  on `WaveTableView` (they already use the S1-relocated `render_wave.h`
  helpers). `WaveTable::render()` becomes a thin shim calling
  `dsp::renderWavetable(view(), ...)` — keeps storage's public API stable.
- **S7c** Switch `Oscillator::renderOsc` to take `const WaveTableView&` (or
  produce the view at the Voice boundary), dropping `wave_table.h` from
  `oscillator.h/.cpp`. `storage` now depends on `dsp`, never the reverse.

Highest bit-drift risk (q31 ops relocating). Land S7b behind the goldens with
the ARM oracle; if a documented rounding waiver is unavoidable, re-baseline
once per the Phase-0 contract. Stealable-memory caveat: the view snapshots
`band->data` pointers at render time, which is safe because rendering is
synchronous within the audio block.

### S8 — Lock it: tests + lint gate
- Grow `tests/dsp_diff` drivers per scrubbed primitive (extend the existing
  `sine_diff.cpp`/`wave_diff.cpp` with osc / filter / delay drivers) so L0
  bit-equivalence is checked in isolation, not only end-to-end.
- Add a lint gate (same shape as the libdeluge include allowlist) that fails
  if any `dsp/` file gains a forbidden upward include. Seed its allowlist
  with the deferred sample/granular/timestretch files so their known reaches
  don't block the gate, and document that the allowlist must shrink to empty
  in Phase 3.

## Test & gate strategy

- **Per step:** firmware Debug build green (`ninja -f build-Debug.ninja
  deluge.elf:Debug`) + full golden sweep on x86 and the ARM oracle.
- **Mechanical steps (S1, S2, S5, S6):** expect byte-identical fixtures.
- **Parameter-injection steps (S3, S4):** identical too — same values,
  different plumbing.
- **S7:** the one place a rounding waiver might be needed; treat any fixture
  delta as guilty until proven a faithful reorder, then re-baseline once.

## Risks

- **Bit-drift on S7** — primary risk; mitigated by landing it last on clean
  foundations and gating hard on the ARM oracle.
- **`renderOsc` signature churn** (S3 + S7c touch the same 13-arg function) —
  sequence them so the signature changes once, not twice; consider folding
  S3's quality arg into the same S7c signature pass.
- **Scope creep on S6** — the util headers are shared app-wide; resist
  cleaning beyond the symbols `dsp/` actually needs.
- **CPU headroom** — all changes are block-granularity plumbing; no new
  per-sample indirection. Spot-check timing under host-sim; the goldens
  double as perf canaries.
