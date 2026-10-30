# Phase 4 — the L2 voice kernel

> Status: **in progress — sub-slices (a), (b), (d), (e) done, (c) partial, (f) the lift not started.**
> `Voice::render`'s signature and body are free of `Sound&` and `ModelStackWithSoundFlags*`, and `Voice` has no
> `friend`s; slices 25–28 and the source ports are tracked in `synth_voice_lift.md` and `synth_source_ports.md`.
> The remaining work is the lift itself. Expands Phase 4 of `synth_untangling_plan.md`; builds on
> `synth_l1_engine_architecture.md` (the
> engines/components the kernel composes) and the Phase-2 data plane (`ControlBlock`, `VoiceParams`). Each slice
> lands as its own bit-exact, golden-gated commit (x86 byte-identical + targeted ARM/qemu).

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

## Progress (slices 1–20, all bit-exact / golden-gated)

The strangler-fig is well advanced. `Voice::render` now takes
`render(ParamManagerForTimeline* paramManager, const ControlBlock& controlBlock, int32_t* soundBuffer, …flags…,
int32_t externalPitchAdjust, const VoiceConfig& cfg, std::array<int32_t*, 4> spareBuffers)` — **no `Sound&`, no
`ModelStackWithSoundFlags*`**. A new app-side `VoiceConfig`
(`model/voice/voice_config.h`), built once at the adapter edge (`Sound::render`), carries the snapshotted config.

- **Config-snapshot arc — DONE** (slices 1–13): filter set, synthMode + LFO config, `hasFilters`, MPE expression
  state, saturation/`clippingAmount` (DSP relocated into `voice.cpp` file-local helpers), `doingOscSync`, unison
  (numUnison/spread/pan — first engine-threaded), `oscRetriggerPhase`, and the per-source enums `oscType` /
  `repeatMode` / `interpolationMode` (the INPUT branch went fully `sound.sources`-free).
- **Source-component port arc — DONE (mechanically)** (slices 14–15): `renderSourceComponent` is **fully
  `Sound&`-free** (all five source branches decoupled). DX7 patch is injected at note-on (`DX7Engine` caches it,
  render takes no `DxPatch&`); the sample engine reads `voice.sound` internally and `Voice::renderSourceComponent`
  dropped its `Sound&` param. The abstract interface followed later: all five sources now render through
  `synth::Source` on ports (sub-slice e, below).
- **modelStack-drop arc — DONE** (slices 16–19): `sound` deref → `this->sound`; source-active flags, the
  noise-source predicate, and `numUnison` snapshotted so `areAllUnisonPartsInactive` + `SubtractiveEngine::render`
  are `modelStack`-free; `paramManager` passed explicitly; `Voice::noteOff` reworked to take
  `(paramManager, noteTailsAllowed)` — after which the `ModelStackWithSoundFlags*` parameter was removed from
  `Voice::render`.
- **Shrink-`paramManager` arc — easy-snapshot phase DONE** (slices 20–21): the local patched-sources bitmask, the
  auto-release noise read (reuses the snapshot), MPE bend ranges, and portamento are all snapshotted. The render
  body's remaining `paramManager` reads were the two data-plane ones. The patcher `ControlBlock` now comes in as
  a parameter (slice 25); the `paramManager` passed to `noteOff` and the engines resolves at the lift.
- **Engine-weaning arc — DONE** (slices 22–24): the render helpers/engines no longer reach `Sound&`. FM
  `modulator1ToModulator0` + subtractive `isSourceActiveCurrently` snapshotted into `VoiceConfig`;
  `Source::renderInStereo` became a static `Source`/`Sound`-free predicate; then the now-dead `Sound&` params were
  dropped from `renderBasicSource` / `SubtractiveEngine::render` / `FMEngine::render`. **No render helper/engine
  carries or reads `Sound&`.** The last render-body `sound` contact, the patcher `ControlBlock`, moved to the
  adapter in slice 25.

## The L2 boundary

**Voice-rendering state (belongs in the kernel):** the per-source engines/components
(`{ringmod,subtractive,fm,sample}_engine`, `dx7_engine`, `input_engine`, the VA/wavetable components), the
envelopes, `lfo2`/`lfo4`, `filterSet`, oscillator phase/feedback, and the `VoiceParams` data plane
(`params`, `= ParamPlane<…>`, `synth/modulation/param_plane.h`) the patcher fills.

**App state (must NOT be reached from render):** preset config (`Sound`), automation/patch source
(`ParamManager`), scheduling/voice-pool policy, transport, and storage (sample/wavetable/live-input).

**Already done (Phase 2 + slices 1–20):** the patcher runs off a `ControlBlock` (`synth/modulation/control_block.h`) produced
by `Sound::patchControlBlock(paramManager)`, built once in `Sound::render` and passed to `Voice::render` as a
`const ControlBlock&`. The render was
**largely data-plane-driven already** (it reads `params.paramFinalValues`); the ~40 `sound.*` config reads + the
`modelStack` hooks listed below have now been **routed through `VoiceConfig` / explicit params** (slices 1–20), so
`Voice::render`'s signature no longer carries `Sound&` or `modelStack`. What's left is the methods the render
calls that still reach `this->sound` (`noteOff`, `getPriorityRating`), the `paramManager` passed on to `noteOff`
and the engines, and the relocation.

## Coupling inventory (the work)

> **Historical map** — this is the *original* inventory that drove slices 1–20; the line numbers predate the work
> and most rows are now resolved (see the per-row disposition + the Progress section). Kept as the categorisation
> of the coupling.

`Voice::render` was `voice.cpp:717` (now `723`); it pulled `paramManager` + `sound` off `modelStack`. The upward
references, categorised:

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

a. ✅ **DONE — Snapshot preset/voice config into a `VoiceConfig`** (slices 1–13). Done in grouped bite-sized
   slices (filters, synth-mode, LFO config, hasFilters, expression, saturation/clipping — `Sound::saturate`'s DSP
   relocated to `voice.cpp` file-local helpers, doingOscSync, unison, oscRetriggerPhase, oscType/repeatMode/
   interpolationMode). `VoiceConfig` lives **app-side (`model/voice/voice_config.h`)** — its filter-enum fields are
   under `model/`; it relocates to `synth/` at the lift once numeric-agnostic.
b. ✅ **DONE — Drop `modelStack` from the render hot path** (slices 16–19). Source-active flags + the
   noise-source predicate snapshotted; `paramManager` passed explicitly; `areAllUnisonPartsInactive` +
   `SubtractiveEngine::render` made `modelStack`-free; `Voice::noteOff` reworked to `(paramManager,
   noteTailsAllowed, …)`. The `ModelStackWithSoundFlags*` parameter is **removed** from `Voice::render`; the
   adapter (`Sound::render`) no longer passes it.
c. ◐ **PARTIAL — Note-tail / scheduling hooks.** `allowNoteTails` is snapshotted (`cfg.noteTailsAllowed`) and the
   deferred note-off no longer needs `modelStack`. `voiceUnassigned`/priority/culling/`getPriorityRating` **stay
   app-side** (the adapter) by design. `Voice::noteOff` still reaches `this->sound` internally — revisited at the
   lift.
d. ✅ **DONE — Pass transport-synced LFO increments in.** `Sound::render` snapshots them into
   `cfg.lfo2SyncedPhaseIncrement`/`cfg.lfo4SyncedPhaseIncrement` (`sound.cpp` ~1923–1928);
   `getLocalLFOPhaseIncrement` takes the increment and no longer reaches `sound`.
e. ✅ **DONE — source dispatch and ports.** `renderSourceComponent` is `Sound&`-free and dispatches through the
   polymorphic `synth::Source`. All five sources live in `synth/source/` on ports (`synth_source_ports.md`); only
   the adapters (`SamplePlaybackAdapter`, `AudioInputAdapter`, `DxVoiceAdapter`) stay App-side. The
   Subtractive/FM `friend`-couplings are dissolved: **`Voice` has no `friend`s.** The engines still take `Voice&`.
f. ☐ **TODO — Lift to `synth::Voice`** (the endgame). The render body's `ControlBlock`, expression, porta and
   `renderInStereo` reads are done. Prerequisites remaining first (detail in `synth_voice_lift.md`): drop the last
   `paramManager` uses (`noteOff`, and the engine pass into `renderSourceComponent`'s cable scan); split the
   methods that still reach `Sound` (`noteOff`, `getPriorityRating`, `noteOn`, `calculatePhaseIncrements`, …) into
   kernel and adapter halves; drop the `Sample*`/`WaveTable*` casts; inject `cpuDireness` and `nextVoiceState`;
   hold ports instead of adapters in `voice_unison_part_source.h`; move `voice_config.h` off `model/`'s
   `filter_config.h`; and take `PatchedParamSet*`/`PatchCableSet*` out of `ControlBlock`. Then move the render
   path into a
   portable `synth::Voice` (+ a host unit test with zero `Song`/`ModelStack`/`AudioEngine`); old `Voice`/
   `Sound::render` become the adapter. DX7-as-engine + the engine **registry** can land here.

## What stays app-side (out of the kernel)

The ParamManager→`ControlBlock`/`VoiceConfig` adapter; scheduling / culling / the `justCreated` render-spread
(`voice.cpp:731`); transport; preset serialization; the `ModControllableAudio` FX chain; the arpeggiator.

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
