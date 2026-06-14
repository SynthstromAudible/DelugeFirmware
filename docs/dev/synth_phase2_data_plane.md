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
2. **Envelope decouple.** Replace `noteOn(idx, Sound*, Voice*)` /
   `noteOff(idx, Sound*, ParamManagerForTimeline*)` with value-parameter
   signatures; the `voice.cpp` call sites pass exactly the ADSR/flags the
   overloads read today. Add `envelope_spec`. Bit-exact.
3. **LFO decouple.** Push `getLocalLFOPhaseIncrement`'s `sound.lfoConfig` +
   `paramFinalValues` reads to the call site; the synth-side LFO sees only
   `LFOConfig` + increment (already its `render` signature). Add `lfo_spec`.
4. **`ControlBlock` + patcher param source.** Introduce `ControlBlock`; Patcher
   reads patched-param preset values + cable set from it instead of
   `sound.getSmoothedPatchedParamValue` / `param_manager.getPatchCableSet`.
   `Voice`/`Sound` build the `ControlBlock` at the call site (the proto-adapter).
   This is the largest step — slice by patcher method if needed.
5. **`VoiceParams` POD.** Gather the per-voice data plane into the POD; the
   components take `VoiceParams&` rather than loose `Voice` fields.
6. **Relocate to `deluge::synth`.** `git mv` Patcher/Envelope/LFO (+ the new
   types) into the new namespace/lib; flip the boundary lint to empty.
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
