# Synth Phase 2 — Data plane & modulation-core extraction

> **Status:** Planning (2026-06-14). Re-scoped from the original
> `synth_untangling_plan.md` §Phase-2 ("Kill the offsetof data plane") after
> auditing the current tree — see §1. Prerequisite Phase 1 (the `dsp-next` L0
> library + full oscillator migration) is complete.
> **Goal:** Make the per-voice modulation core — **Patcher, Envelope, LFO** —
> data-driven and free of `Sound&` / `Voice&` / `ParamManager&` back-references,
> so it can move into a portable `deluge::synth` library and be unit-tested with
> zero Song/ModelStack/AudioEngine. Bit-exact at every step, golden-gated.

---

## 1. Where we are (the premise shifted)

The original Phase-2 framing was "replace the raw-index/`offsetof` parameter
plumbing." **That work is already largely done.** PR #3366 ("Cleanup and
modernize patcher", commit `4af28d77`) converted the Patcher to take
`std::span<int32_t>` views over the source/param arrays plus a `Patcher::Config`
struct, and the param tables are now plain `std::array`s keyed off the
`deluge::modulation::params` enum:

- `Sound::paramFinalValues` — `std::array<int32_t, kNumParams - FIRST_GLOBAL>`
- `Sound::globalSourceValues` — `std::array<int32_t, kFirstLocalSource>`
- `Voice::paramFinalValues` — `std::array<int32_t, params::LOCAL_LAST>`
- `Voice::sourceValues` — `std::array<int32_t, kNumPatchSources>`
- `Patcher(const Config&, std::span<int32_t> source_values, std::span<int32_t> param_final_values)`

A tree-wide grep finds **no `offsetof` in the param/patcher/voice/sound code**
(the only `offsetof` left in `src/deluge` is unrelated intrusive-container use
in `midi_device_manager`, `multi_range_array`, `audio_file_vector`).

**What remains** is the entanglement the offsetof work didn't touch — the
back-references that stop the modulation core from being a standalone library:

| Component | File | Back-references it still carries |
|---|---|---|
| `Patcher` | `modulation/patch/patcher.{h,cpp}` | `performPatching(uint32_t, Sound&, ParamManagerForTimeline&)` / `performInitialPatching(Sound&, ParamManager&)` — reads `sound.getSmoothedPatchedParamValue(p, pm)` and `param_manager.getPatchCableSet()` |
| `Envelope` | `modulation/envelope.{h,cpp}` | `noteOn(uint8_t, Sound*, Voice*)`, `noteOff(uint8_t, Sound*, ParamManagerForTimeline*)` — the clean `render(numSamples, attack, decay, sustain, release, table)` already takes plain values |
| `LFO` | `modulation/lfo.{h,cpp}` | `render(numSamples, LFOConfig, phaseIncrement)` is already value-driven; the coupling is at the call site — `Voice::getLocalLFOPhaseIncrement` reads `sound.lfoConfig[...]` + `paramFinalValues[...]` |

So Phase 2 is now a **decoupling + extraction** phase, not a data-plane rewrite.

### 1.1 The #2660 canary

`sound.h:98-111` still documents the December-2024 release bug (#2660): removing
some LFO `uint8_t`s re-voiced the synth, "fixed" by `alignas(8) modKnobs` with
the note *"I don't know why this fixes the problem… we think the issue relates
to the use of offsetof in the param and patcher system."* Now that offsetof is
gone, this workaround is very likely vestigial. Removing it (and confirming the
golden masters do not budge) is both a cleanup and the final proof that the
alignment-bug class is dead. **This is the one concrete behaviour-risk in the
phase and gets its own gated step (§3, step 7).**

---

## 2. Target design

Two new value types own the boundary between ParamManager-world (app) and the
synth core:

### `VoiceParams` (the owned per-voice data plane)
A POD gathering the per-voice modulation state currently smeared across `Voice`
members: `paramFinalValues` (LOCAL range), `sourceValues`, `sourcesChanged`, and
the `Patcher::Config`. The synth components read/write *this*, not loose `Voice`
fields. (`Sound` keeps its own global-scope equivalent; the same POD shape
serves both scopes via the existing `Config.globality`.)

### `ControlBlock` (the per-block inputs from the app)
What the Patcher reads *instead of* `Sound&` + `ParamManager&`:
- the **smoothed patched-param preset values** (today `sound.getSmoothedPatchedParamValue(p, pm)`, which folds automation/MPE/knob state) — precomputed into a span the patcher indexes;
- a **patch-cable-set view** (today `param_manager.getPatchCableSet()`).

The app fills a `ControlBlock` per render block from ParamManager-world; the
synth core never sees `Sound`/`ParamManager`. This is the seed of the Phase-4
adapter ("Old `Voice`/`Sound::render` shrink to an adapter translating
ParamManager-world into `ControlBlock`s").

### `deluge::synth` (the new home)
Patcher/Envelope/LFO (+ `VoiceParams`/`ControlBlock`) move into a `deluge::synth`
namespace and, eventually, a `libdeluge-synth` static lib that — like `dsp-next`
— may not include `model/`, `processing/`, `storage/`, `gui/`. A boundary lint
(mirroring `check_dsp_boundary.py`) enforces it and measures progress.

---

## 3. Phased steps (each bisectable, bit-exact, golden-gated)

Discipline matches the dsp-next migration: structural-only, no mid-move
arithmetic changes; every step keeps the golden masters bit-identical on **x86
(SIMDe) and ARM/QEMU**, and lands with a dual-arch CppSpec for the decoupled
unit where practical. Run the goldens `--filter`-targeted to the families a step
can affect during the iterate cycle; reserve the full sweep (both arches) for
phase boundaries.

1. **Boundary lint + baseline.** Add `scripts/check_synth_boundary.py` listing
   the app back-refs reachable from `patcher/envelope/lfo` (seed allowlist that
   only shrinks). Capture the golden baseline. No code change.
2. **Envelope decouple — DONE.** Rewrote `Envelope` to first principles: a
   self-contained ADSR generator driven purely by sample progression, with no
   `Sound`/`Voice`/`ParamManager`/`AudioEngine`. `noteOn`/`noteOff`/`render` take
   plain values; internal state is fully private behind intent-revealing
   accessors (`stage()`, `outputLevel()`, `fastReleaseRate()`, `isAtAttackStart()`)
   plus one documented transitional seam (`setOutputLevel`, to be removed by the
   `ControlBlock` step). `EnvelopeStage` moved out of `definitions_cxx.hpp` into
   the class as `Envelope::Stage` (values unchanged — `getPriorityRating` packs
   the stage into 3 bits). The voice-cull timestamp (`timeEnteredState`, stamped
   from `AudioEngine::nextVoiceState`) was *lifted to the Voice*
   (`Voice::envelope0TimeEntered`, stamped via `takeStageChanged()`), so the
   envelope is fully global-free; this slightly shifts the cull-stamp timing for
   intra-render multi-transitions (accepted — voice-stealing isn't golden-covered).
   The render switch was decomposed into named per-stage step methods
   (`stepAttack`/`stepDecay`/…) driven by a small dispatch loop (no `goto`). All
   bit-exact (x86 golden 333/333, ARM oracle, both arches). **Curve helpers +
   `envelope_spec` DONE (`5a97499e`):** the blocking curve helpers (`getDecay4/8`,
   `interpolateTable`, `interpolateTableInverse`) were extracted verbatim out of the
   monolithic `util/functions.cpp` into a new leaf `util/interpolation.{h,cpp}` (only
   dep = the decay/release LUTs); 5 callers + `functions.cpp` re-include it. (`getSine`
   already lived in the leaf `util/waves.h`.) That unblocked a dual-arch
   `tests/dsp/envelope_spec.cpp` — 8 cases driving the `Envelope` in isolation
   (`envelope.cpp` + `interpolation.cpp` + `lookuptables.cpp`, no fatfs/gui/hid), green
   on x86 (SIMDe) and ARM/qemu. Pure relocation, bit-exact (x86 333/333, ARM subset).

   > **Future (the audible/float pass, NOT this phase): data-driven segments.**
   > The most modular envelope is a generic stepper over an array of
   > `{target, rate, curve, exit}` segments (trivially gives DAHDSR / looping /
   > custom curves). It does *not* fit today's envelope under the bit-exact
   > constraint — the stages have idiosyncratic, non-uniform math (attack
   > `getDecay4` curve, decay layered over a smoothed sustain, the sustain
   > note-off handling, a release lookup table, a *sine*-shaped fast-release), so
   > a uniform segment model would be a pile of special cases and can't be
   > bit-exact. Revisit it with the float/curve-unification work, where
   > re-baselining + a listening test are expected anyway.
3. **LFO decouple — DONE.** The audit found `LFO::render(numSamples, LFOConfig&,
   phaseIncrement)` was *already* value-driven (no Sound/Voice/engine reads); the
   increment derivation (`getLocal/GlobalLFOPhaseIncrement`) is the legitimate
   app-side proto-adapter for step 4 and stays put. So the literal "push the reads"
   was already satisfied. The realized work was the o-o + testability debt: each
   waveform is now its own typed generator in a `namespace lfo_wave`
   (`Saw`/`Square`/`Sine`/`Triangle`/`SampleHold`/`RandomWalk`/`Warbler`) held in a
   `std::variant` and dispatched by `std::visit` — the closed-sum, no-vtable,
   value-semantics form (C++ mirror of a Rust data-carrying `enum`, not `dyn Trait`).
   `LFO` state is fully private behind `phase()`/`setPhase()`/`advancePhase()` seams
   (the lone firmware poke `globalLFO.phase += offset` in `resyncGlobalLFOs` became
   `advancePhase`). `LFOType`/`LFOConfig` stay app-level (serialized + UI). Bit-exact
   hazards preserved verbatim: the warbler's 2× phase advance and the S&H/walk/warbler
   pre-advance overflow detection. One flagged micro-change (golden-gated, like the
   envelope cull-timing shift): per-struct state means a *live local-LFO shape change
   between two stateful waves* resets the accumulators (an accidental cross-type state
   leak in the old shared-fields code), unobservable in the corpus. Added a dual-arch
   `tests/dsp/lfo_spec.cpp` (exact per-waveform values ported from the retired x86-only
   `tests/unit/lfo_tests.cpp`, which is deleted) — 12 cases green on x86 + ARM/qemu.
   Bit-exact: x86 golden 333/333, ARM oracle 8/8 LFO-exercising cases.
4. **`ControlBlock` + patcher param source — DONE.** The audit showed the Patcher's
   only app-coupling is `sound.getSmoothedPatchedParamValue(p, pm)` (= `paramLPF`
   override ∥ `getPatchedParamSet()->getValue(p)`, the latter already
   automation/MPE-folded *upstream* before patching) plus `getPatchCableSet()` — and
   it never actually used `ParamManagerForTimeline`'s timeline features. So a small
   value type carries both: `struct ControlBlock { PatchedParamSet* patched_params;
   PatchCableSet* patch_cable_set; int32_t smoothing_param; int32_t smoothing_value;
   presetValue(p); }` (`modulation/patch/control_block.h`), built once per call by
   `Sound::patchControlBlock(ParamManager&)` (mirrors `getSmoothedPatchedParamValue`;
   safe to snapshot `paramLPF` since `doParamLPF` runs after `performPatching`). The
   Patcher's `performPatching`/`performInitialPatching`/`recalculateFinalValue…`/
   `combineCables*` now take `ControlBlock` by value instead of `Sound&` +
   `ParamManager(ForTimeline)&` — so `patcher.{h,cpp}` reference **no** `processing/`
   or `model/` types (verified: includes are `modulation/` + `util/functions.h` only).
   ~12 call sites build the block (the proto-adapter), e.g.
   `patcher.performPatching(sourcesChanged, sound.patchControlBlock(*paramManager))`.
   `getSmoothedPatchedParamValue` stays (it has non-patcher callers). Lazy
   `presetValue` keeps the per-voice hot path identical (the eager preset-value span is
   deferred to step 5). Bit-exact: x86 golden 333/333, ARM oracle 10/10 patching-stress
   cases. A `patcher_spec` is deferred — `PatchedParamSet`/`PatchCableSet` are still
   heavy for a leaf spec; the goldens fully cover the patcher meanwhile.
5. **`VoiceParams` POD — DONE.** Introduced `template <size_t NumSources, size_t
   NumParams> struct ParamPlane { std::array sourceValues; std::array
   paramFinalValues; }` (`modulation/param_plane.h`) — one shape, two aliases:
   `VoiceParams = ParamPlane<kNumPatchSources, LOCAL_LAST>` and
   `GlobalParams = ParamPlane<kFirstLocalSource, kNumParams - FIRST_GLOBAL>`. `Voice`
   and `Sound` each replace their two loose arrays with a `params` member; the Patcher
   is constructed with spans over `params.sourceValues` / `params.paramFinalValues`
   (no Patcher change — it already takes `std::span`). ~107 accessors repointed to
   `params.…` (Sound's `globalSourceValues` → `params.sourceValues`; voice.cpp's 3
   cross-object reads → `sound.params.sourceValues`). `sourcesChanged` (asymmetric —
   Voice render-local vs Sound member) and the compile-time `Patcher::Config`s stay
   out of the POD. Pure structural move, bit-exact: x86 golden 333/333, ARM oracle
   10/10 broad spread; both host-sims build clean. (The eager preset-value span stays
   deferred — the lazy `ControlBlock::presetValue` is unchanged.)
6. **Relocate to `src/deluge/synth/` + boundary lint — DONE (relocation + lint;
   namespace deferred).** `git mv`'d the five components — `envelope.{h,cpp}`,
   `lfo.{h,cpp}`, `param_plane.h`, `patcher.{h,cpp}`, `control_block.h` — out of
   `modulation/` into a new `src/deluge/synth/` (flat). Added
   `scripts/check_synth_boundary.py` (mirrors `check_dsp_boundary.py` but ALLOWS
   `modulation/` + `util/` + `dsp-next/`; forbids the app layers model/processing/
   storage/gui/playback/hid/io, the legacy dsp/, and the HAL/BSP) + a shrink-only
   `synth_boundary_allowlist.txt` (seeded **EMPTY**), a `synth-boundary` pre-commit
   hook, and `.github/workflows/synth-boundary.yml`. The one direct violation —
   `lfo.h → model/sync.h` — was cleared by relocating the small sync-enum leaf
   `model/sync.{h,cpp}` → `modulation/sync.{h,cpp}` (10 includer edits), so the lint
   is clean with an empty allowlist. The app `GLOB_RECURSE` picked the moved sources
   up automatically; only the two dual-arch specs' `_DEPS`/includes + the unit
   suite's sync path needed CMake edits. Pure relocation, bit-exact: x86 golden
   333/333, ARM oracle 10/10, both specs green both arches, both host-sims clean.

   > **Deferred (NOT this step):** the `namespace deluge::synth` wrap (~44 files /
   > 327 refs of qualification reaching gui/model/processing) and a `libdeluge-synth`
   > static-lib target — premature while the synth core still pulls model/storage
   > *transitively* via `modulation/params/param_set.h` (PatchedParamSet) and gui
   > via `util/functions.h`. A direct-include lint can't see those; cutting them (a
   > lean `util/param_functions.h`; lightening `PatchedParamSet`/`PatchCableSet`) is
   > the prerequisite for both the namespace wrap and a true standalone lib.
7. **Retire the #2660 `alignas(8)` workaround.** Remove it; confirm the goldens
   do not move on x86 **and** ARM (the alignment bug, if it were still live,
   would re-voice the synth — so this is the gated proof it's dead). If anything
   moves, stop and investigate rather than re-baseline.

Steps 2/3 are independent and small; 4 is the crux; 5–7 follow once the back-refs
are severed.

---

## 4. Verification

- **Golden masters** bit-exact x86≡ARM at every step (they double as the perf
  canary — spot-check render time under host-sim, per the plan's CPU-headroom
  risk). Step 7 is the only step where a *non*-move is itself the assertion.
- **Dual-arch CppSpecs** for the decoupled Envelope, LFO, and (where feasible)
  the Patcher run in isolation — the payoff being a `deluge::synth` unit
  constructible with no Song/ModelStack/AudioEngine.
- **`check_synth_boundary.py`** allowlist shrinks to empty by step 6.
- Firmware + host-sim compile clean; `check_dsp_boundary.py` still green.

---

## 5. Risks

- **#2660 / alignment (step 7):** the one real behaviour risk. Gated by the
  golden A/B above; keep the workaround until the no-move is proven on both
  arches.
- **Smoothed-value semantics (step 4):** `getSmoothedPatchedParamValue` folds
  automation, MPE, and knob state — the `ControlBlock` must reproduce it exactly.
  Bit-exact goldens over automation/MPE/expression presets are the gate.
- **Arithmetic-reorder bit-drift:** moving q31 ops can change rounding; golden
  masters catch it immediately, waivers documented (none expected — structural).
- **Scope:** step 4 touches the patcher's hot path; slice per method and keep
  each commit golden-green.

---

## 6. Out of scope (later phases)

L1 `SourceEngine` interface + the OscType megaswitch (Phase 3), the L2
`synth::Voice` pool (Phase 4), and the L3 CLAP C-ABI (Phase 5) — see
`synth_untangling_plan.md`. Sample playback stays behind the `SampleSource` port.
The float-core migration is enabled by these interface rules but executed later.
