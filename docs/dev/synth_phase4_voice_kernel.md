# Phase 4 — the L2 voice kernel

> Status: **roadmap / design** (no code yet). Expands Phase 4 of `synth_untangling_plan.md` into an executable
> plan. Builds on `synth_l1_engine_architecture.md` (the engines/components the kernel composes) and the Phase-2
> data plane (`ControlBlock`, `VoiceParams`). Each sub-slice below lands as its own bit-exact, golden-gated commit.

## Goal

A `synth::Voice` that renders one voice from a per-block control input with **no references upward** — no
`Sound&`, `ModelStackWithSoundFlags`, `ParamManager`, or `AudioEngine` reached from the render path — composing
the Phase-1/3 parts (engines/components, envelopes, LFOs, `filterSet`, unison). Old `Voice` / `Sound::render`
shrink to an **adapter** translating ParamManager-world into the kernel's inputs. The end-state is unit-testable
on host with zero firmware scaffolding, and is the body that L3 (the CLAP-shaped C-ABI) later wraps.

**Approach: in-place decouple, lift last.** Sever `Voice::render`'s upward coupling one bit-exact slice at a time
(route config through the existing `ControlBlock` + an expanded per-voice config + precomputed flags). Once the
render path is upward-ref-free, lifting it into `synth::Voice` is near-mechanical. Golden-gated throughout — no
two-implementation window. (This mirrors the relocate-in-place discipline of every Phase-3 slice.)

## The L2 boundary

**Voice-rendering state (belongs in the kernel):** the per-source engines/components
(`{ringmod,subtractive,fm,sample}_engine`, `dx7_engine`, `input_engine`, the VA/wavetable components), the
envelopes, `lfo2`/`lfo4`, `filterSet`, oscillator phase/feedback, and the `VoiceParams` data plane
(`params`, `= ParamPlane<…>`, `synth/param_plane.h`) the patcher fills.

**App state (must NOT be reached from render):** preset config (`Sound`), automation/patch source
(`ParamManager`), scheduling/voice-pool policy, transport, and storage (sample/wavetable/live-input).

**Already done (Phase 2):** the patcher runs off a `ControlBlock` (`synth/control_block.h`) produced by
`Sound::patchControlBlock(paramManager)` (`processing/sound/sound.h:272`); `Voice::render` invokes it at
`voice.cpp:819` (`patcher.performPatching(sourcesChanged, sound.patchControlBlock(*paramManager))`). So the render
is **largely data-plane-driven already** (it reads `params.paramFinalValues`). The remaining gap is the ~40
`sound.*` config reads + a handful of `modelStack` hooks.

## Coupling inventory (the work)

`Voice::render` is `voice.cpp:717`; it pulls `paramManager` + `sound` off `modelStack` at `728`–`729`. The
upward references, categorised:

| Category | Examples (current) | Phase-4 disposition |
|---|---|---|
| **Preset / voice config** | `sound.sources`, `numUnison`, `getSynthMode`, `lfoConfig`, `oscRetriggerPhase`, `modulatorRetriggerPhase`, `unisonPan/Detuners/StereoSpread`, `transpose`, `hasFilters`, `lpfMode`/`hpfMode`/`filterRoute`, `saturate`/`getShiftAmountForSaturation`/`clippingAmount`, `volumeNeutralValueForUnison`, `modulator1ToModulator0`, `modulatorTranspose(rs)`, `renderingOscillatorSyncCurrently` | snapshot into an expanded **`ControlBlock`** / a new per-voice **`VoiceConfig`** the kernel holds |
| **Scheduling / voice-pool / note policy** | `allowNoteTails`+`noteOff` (`735`–`736`), `voiceUnassigned`, `voicePriority`, `numActiveVoices`, `polyphonic` | **stays app-side behind explicit hooks** — the kernel returns signals (e.g. "wants unassign"), the app decides |
| **Transport-synced** | `getSyncedLFOPhaseIncrement` | synced increments **passed in** per block |
| **Source ports** | sample/input/dx7/wavetable reach `storage` / `AudioEngine` | kernel depends on injected **source-component interfaces** (`SampleSource` + a live-input port, per the L1 doc); impls stay app-side — may remain a *port boundary* rather than fully move into the library |
| **`modelStack`** | `->paramManager`/`->modControllable` (`728`/`729`), `checkSourceEverActive`/`isSourceActiveCurrently` (`882`, …), `areAllUnisonPartsInactive` (`1458`, reads `paramManager`) | replaced by passed refs / `ControlBlock` + **precomputed per-source active flags**; `areAllUnisonPartsInactive` becomes kernel-internal |
| **Globals / quality** | `AudioEngine::cpuDireness` (`1177`), file-scope `spareRenderingBuffer` | passed in (cpuDireness already flows via `BlockControls`); scratch **injected** |

Note: most of the heaviest `sound.*` / `modelStack->` coupling lives in **note-on / setup / `calculatePhaseIncrements`**
(`voice.cpp` ~110–882), not the per-block render — those become the adapter's job, not the kernel's.

## Sub-slice sequence (each bit-exact, golden-gated, in-place)

> **Ordering note:** config-snapshot (a) precedes modelStack-drop (b). `modelStack` carries `sound` + `paramManager`,
> which `render` reads pervasively (the patcher + ~40 `sound.*` reads), so `modelStack` cannot leave `render`
> until those config uses are routed through a snapshot first.

a. **Snapshot preset/voice config into a `VoiceConfig` (+ the existing `ControlBlock`)** — the bulk, done in
   grouped bite-sized slices (filters first; then unison; synth-mode + osc-retrigger; LFO config;
   saturation/clipping — relocating `Sound::saturate`'s DSP into the kernel; modulator routing). Each replaces a
   cluster of `sound.*` reads with config-struct reads; the adapter (`Sound::render`) populates it. **`VoiceConfig`
   starts app-side (`model/voice/`)** — its filter-enum fields live under `model/` and `synth/` may not include
   `model/`; it relocates to `synth/` once numeric-agnostic.
b. **Drop `modelStack` from the render hot path** — once config no longer needs it, pass `Sound&` / `ParamManager`
   (or `ControlBlock`) + precomputed source-active flags explicitly; keep `modelStack` only at the adapter edge
   (`sound.cpp:2520`, the `voice->render(...)` call site). Make `areAllUnisonPartsInactive` kernel-internal (note
   `checkSourceEverActive` caches `soundFlags` on the modelStack across setup + render).
c. **Move scheduling / note-tail / voice-pool reads behind app-side hooks** — the kernel emits signals; the app
   (Sound/voice-pool) owns `allowNoteTails`/`noteOff`/`voiceUnassigned`/priority/culling.
d. **Pass transport-synced LFO increments in** — sever `getSyncedLFOPhaseIncrement`.
e. **Formalise the `SourceComponent` interface + sample/input ports** — turn `renderSourceComponent`'s uniform
   seam (`voice.cpp`, returns `deluge::voice::SourceRenderResult`) into a real per-source-component interface, and
   inject sample/input via ports so the source dispatch is no longer app-reached. **This dissolves the
   Subtractive/FM `friend`-couplings.** Largest, deepest slice (the storage boundary).
f. **Lift to `synth::Voice`** — once the render path has no upward refs, move it into a portable `synth::Voice`
   (+ a host unit test constructing it with zero `Song`/`ModelStack`/`AudioEngine`). Old `Voice`/`Sound::render`
   become the adapter (build `ControlBlock`/`VoiceConfig` from ParamManager-world). DX7-as-engine (additive, keep
   the source) and the engine **registry** can land here.

## What stays app-side (out of the kernel)

The ParamManager→`ControlBlock`/`VoiceConfig` adapter; scheduling / culling / the `justCreated` render-spread
(`voice.cpp:722`); transport; preset serialization; the `ModControllableAudio` FX chain; the arpeggiator.

## Numerics & cross-cutting rules

q31 core now; the boundary types stay numeric-agnostic so the float migration is contained later (per
`synth_untangling_plan.md`). No globals below the boundary — `spareRenderingBuffer` becomes injected scratch.
Virtual / port dispatch at **block granularity only**, never per-sample (Cortex-A9 headroom).

## Verification & risks

- **Bit-exact, every slice:** full x86 `tests/golden/run.py` → **byte-identical**, with targeted ARM/qemu on the
  affected families (per `[[golden-arm-runs-targeted]]`). The lift (f) is mechanical once refs are gone.
- **Largest risk = the source-port arc (e):** the sample/input/dx7/wavetable storage coupling is the deepest;
  it may stay a *port boundary* (kernel calls injected interfaces, impls app-side) rather than fully relocating
  into the library — that's acceptable and matches the L1 doc.
- **CPU headroom:** keep indirection at block granularity; the golden masters double as perf canaries
  (host-sim timing + hardware spot-checks per slice).
- **Scope:** Phase 4 is the *voice* kernel only; L3 (the CLAP-shaped C-ABI + voice pool inside the instrument)
  is the next phase, reachable once the kernel is upward-ref-free.
