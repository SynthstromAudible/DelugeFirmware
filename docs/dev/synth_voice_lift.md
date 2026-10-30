# Phase 4 — the `synth::Voice` lift

> **Status — the in-place decoupling is essentially DONE; the physical relocation (L-viii) is not started.**
> `Voice::render` is now upward-reference-free in the ways that matter: its signature carries no `Sound&` /
> `ModelStackWithSoundFlags*`; its body reads no `Sound` (though it calls `noteOff` and, via
> `renderSourceComponent`, `getPriorityRating`, which both do); the render helpers/engines hold no `Sound&`; the
> scratch is injected (L-v); and **`Voice` has no `friend`s** (the Subtractive/FM couplings are dissolved). That
> decoupling **was the value** of the lift — the voice kernel is now logically separable and render-driven by
> `VoiceConfig` + `ControlBlock`/`ParamPlane` + injected scratch.
>
> The remaining step — physically `git mv`-ing `Voice` into `synth/` (L-viii) so `check_synth_boundary` enforces
> the boundary — is **not started**. The legacy `dsp/` includes are not what blocks it: `dsp/timestretch` is an
> unused include, `dsp/util` supplies one fold function, and `dsp/oscillators` is a scratch array plus an
> `OscType`→VA-shape mapper. The real blockers are the voice's remaining App reaches outside the render body —
> `Sound`/`paramManager`/the cable set in several methods, `Sample*`/`WaveTable*` casts, the `AudioEngine`
> globals, the port adapters held by value, `voice_config.h`'s `model/` include and `ControlBlock`'s param-set
> pointers. See "What blocks L-viii" below.
>
> ---
> Earlier status (still accurate for the decoupling history): L-vi is done — all five sources are polymorphic
> `synth::Source`s (the dispatch seam); the source **ports** (`synth_source_ports.md`) made them library-resident.
> Slices 1–28 made `Voice::render`'s signature `Sound&`/`ModelStack`-free and removed every `Sound&` read from the
> render helpers/engines. Each step landed bit-exact (x86 byte-identical + targeted ARM/qemu).

## Goal

A `synth::Voice` under `synth/` that renders one voice from per-block inputs (`VoiceConfig` + the Phase-2 data
plane `ControlBlock`/`ParamPlane` + injected scratch/globals) with **no upward references** — no `Sound&`,
`ModelStackWithSoundFlags`, `ParamManager`, `AudioEngine`, or file-scope globals reached from the render path —
and unit-testable on host with zero `Song`/`ModelStack`/`AudioEngine` scaffolding. Old `Voice`/`Sound::render`
become the adapter that builds those inputs from ParamManager-world.

**Approach (decided): in-place decouple, lift last.** Continue the proven cadence — one bit-exact slice per
residual reach — until the render path is upward-ref-free, then the relocation to `synth/` is near-mechanical.
No two-implementation window. Golden-gated throughout.

## Where we are

`Voice::render`'s signature is `render(ParamManagerForTimeline* paramManager, const ControlBlock& controlBlock,
int32_t* soundBuffer, …flags…, int32_t externalPitchAdjust, const VoiceConfig& cfg, std::array<int32_t*, 4>
spareBuffers)`. Its **body** names no `sound`. `paramManager` survives only to feed `noteOff` and the per-source
render (`subtractiveEngine.render` → `renderBasicSource` → `renderSourceComponent`'s cable scan). The data-plane
vehicles already exist (Phase 2): `ControlBlock` (`synth/control_block.h`,
the patcher input) and `ParamPlane`/`VoiceParams` (`synth/param_plane.h`, the per-voice param data).

## The lift surface (remaining render-path coupling)

| # | Reach (current) | Disposition |
|---|---|---|
| ~~L-i~~ ✅ | patcher input `sound.patchControlBlock(*paramManager)` (`voice.cpp:826`) | **DONE (slice 25)** — `ControlBlock` built at the adapter, passed into `Voice::render` as a `const ControlBlock&` param |
| ~~L-ii~~ ✅ | `combineExpressionValues(sound, i)` (~`voice.cpp:802`, MPE bend/expression smoothing) | **DONE (slice 26)** — takes the `synthLevelValue` (= `cfg.monophonicExpressionValues[i]`) instead of `Sound&`; the dead `Sound&` render-body local removed → **`Voice::render`'s body is `sound`-free** |
| ~~L-iii~~ ✅ | `getLocalLFOPhaseIncrement` (`voice.cpp:718`) → `sound.lfoConfig[id]` + `sound.getSyncedLFOPhaseIncrement(config)` | **DONE (slice 27)** — synced increments snapshotted into `cfg`; the helper takes `(LFOConfig, param, syncedIncrement)`, no `sound` reach |
| ◐ L-iv | `AudioEngine::renderInStereo`, `AudioEngine::cpuDireness` (sample interpolation), `AudioEngine::nextVoiceState` (envelope-stage stamp) | **render-body `renderInStereo` DONE (slice 28)** → `cfg.engineRenderingInStereo`. Remaining: `nextVoiceState` (`stampEnvelope0StageEntry`; scheduling — stays app-side), `cpuDireness` (`voice.cpp:992`, `source_component_render.cpp:38`), and `Source::renderInStereo`'s `renderInStereo`/mic/lineIn reads (`processing/source.cpp:193-205`) |
| ~~L-v~~ ✅ | file-scope `spareRenderingBuffer[0/2/3]` | **DONE (`853414f8`)** — `Sound::render` passes the buffers in as `std::array<int32_t*,4>`; held in a transient `spareBuffers_` member (no engine threading). Global stays App-side for its 6 other users |
| ~~friend-couplings~~ ✅ | `SubtractiveEngine`/`FMEngine` were `friend`s of `Voice` | **DONE (`c8c9f35f`, `6ca138d9`)** — FMEngine owns its operator renders (`renderSineWaveWithFeedback`/etc., moved from Voice); `adjustPitch` → public static; Subtractive's `renderBasicSource`/`areAllUnisonPartsInactive` made public. **`Voice` has no `friend`s.** (The per-source loop relocating *into* the engine layer is later.) |
| ~~VoiceConfig enum dep~~ ✅ | `VoiceConfig` stores `FilterMode`/`FilterRoute` from `gui/l10n`-laden `filter_config.h` | **DONE (`a506e060`)** — `filter_config.h` made gui-free (forward-declared `l10n::String`, `getMorphName` body → `.cpp`). VoiceConfig's enum dependency is synth-clean |
| #54 priority/scheduling | `getPriorityRating` (`sound.snapshot().voicePriority`/`numActiveVoices`) + `nextVoiceState` stamp in render (`stampEnvelope0StageEntry`) | **Resolve at L-viii, not in place.** `numActiveVoices()` mutates mid-loop (`freeActiveVoice` per-voice in `Sound::render`), so it can't be a loop-stable `VoiceConfig` snapshot; `nextVoiceState` is a global counter. Scheduling stays App-side; the adapter supplies the cull context at relocation |
| **L-viii (not started)** | relocate `Voice` → `synth::Voice` under `synth/` (behind `check_synth_boundary.py`) + a host unit test + the L-vii noteOff hook | **Blocked on the voice's remaining App reaches** (see below), not on legacy `dsp/` |

### What blocks L-viii

The decoupling slices above made `Voice::render`'s body input-driven. A literal `synth::Voice` still needs the
rest of `Voice` to stop naming App types. `check_synth_boundary` forbids the `dsp/` prefix, but the voice's legacy
`dsp/` includes are small:

- `dsp/timestretch/time_stretcher.h` is included by `voice.cpp` and not used.
- `dsp/util.hpp` supplies `foldBufferPolyApproximation` only. It belongs in a dsp-next shaping component.
- `dsp/oscillators/oscillator.h` is `oscSyncRenderingBuffer` (a scratch array) plus `isVirtualAnalog`/`vaWaveFor`
  (an `OscType` mapper). Neither is DSP; both move out with the scratch and VA shapes.

The real blockers:

- **Methods that still reach `Sound`, `paramManager` or the cable set.** `Voice` holds `Sound& sound`. `noteOff`
  reads `sound.envelopeHasSustainCurrently`; `getPriorityRating` reads `sound.snapshot().voicePriority` and
  `sound.numActiveVoices()`; `noteOn`, `unassignStuff`, `expressionEventImmediate`, `changeNoteCode`,
  `setupPorta`, `randomizeOscPhases` and `calculatePhaseIncrements` read `Sound`/`Source` state. The render body
  calls `noteOff` (`voice.cpp:742`), and `renderSourceComponent` calls `getPriorityRating` (`:1514`) and walks
  `paramManager->getPatchCableSet()` (`:1451`). These split into a kernel half and an adapter half.
- **Sample and wavetable casts.** `render`'s auto-release reads `SampleHolderForVoice*`/`Sample*` (`:886`,
  `:915`); `renderSourceComponent` casts to `WaveTable*` (`:1483`) and calls `SampleControls`.
- **`AudioEngine` globals.** `cpuDireness` (`voice.cpp:992`, `source_component_render.cpp:38`) and
  `nextVoiceState` (`stampEnvelope0StageEntry`, `resetEnvelope0TimeEntered`).
- **Port adapters held by value.** `voice_unison_part_source.h` holds `SamplePlaybackAdapter`,
  `DxVoiceAdapter` and `AudioInputAdapter` as members. The kernel should hold the ports, injected.
- **`voice_config.h` includes `model/mod_controllable/filters/filter_config.h`** for `FilterMode`/`FilterRoute`.
  The dsp-next filter enums replace it.
- **`ControlBlock` carries `PatchedParamSet*` and `PatchCableSet*`** (`synth/control_block.h`), so `synth/`
  includes `modulation/params/param_set.h`. The patcher walks `PatchCableSet` directly.

When these are gone, L-viii (+ the L-vii noteOff hook + the #54 priority/scheduling re-home) is the
near-mechanical move it was always meant to be.

## Strategy / sequencing

Keep the in-place cadence: **L-i → L-ii → L-iii → L-iv → L-v** as individual bit-exact slices (each routes one
reach onto an existing data-plane input or an injected value), **then L-vi** (the port arc, its own plan + the
decision above), **then L-viii** (the mechanical relocation + host test). `paramManager` is dropped once its last
consumer (the patcher input + `noteOff` + `renderBasicSource`/engines) is rerouted; the kernel's inputs converge on
`VoiceConfig` + `ControlBlock`/`ParamPlane` + injected globals/scratch.

Prime directive throughout: **bit-exact** (full x86 golden byte-identical + targeted ARM/qemu per slice).

## L-i (DONE, slice 25) — pass the patcher `ControlBlock` in

`patcher.performPatching(sourcesChanged, sound.patchControlBlock(*paramManager))` (`voice.cpp:826`) is the only
per-voice patcher-input build. `ControlBlock` is a tiny POD — `{patched_params, patch_cable_set, paramLPF.p,
paramLPF.currentValue}` — all **sound-level and stable across the voice loop** (the param sets are fixed pointers;
`paramLPF` is only mutated *after* the loop, `sound.cpp:~2648`). So building it once at the adapter edge and
passing it to every voice is identical to the per-voice rebuild (and a small perf win).

- **`Voice::render`**: add a `const ControlBlock& controlBlock` parameter (next to `paramManager`).
- **Migrate** `voice.cpp:826` → `patcher.performPatching(sourcesChanged, controlBlock)`.
- **Adapter** (`Sound::render`): build `const ControlBlock controlBlock = patchControlBlock(*paramManager);` before
  the voice loop; pass it at the `voice->render(…)` call (`sound.cpp:2561`).
- Pass it as a **separate param**, not in `VoiceConfig` — it holds *live pointers* (the patcher's data plane), not
  a config snapshot; it travels with the data-plane inputs at the relocation.

**Verification:** x86 `tests/golden/run.py --jobs 16` → 336/336 byte-identical; targeted ARM/qemu on a
heavily-patched spread (e.g. `two_osc_detune,velocity_dynamics,sample/mono,community/Warm DX7 Pad,community/Ringmod
Arp`); both host-sim builds + the 3 boundary lints green.

## What stays app-side (the adapter — out of the kernel)

The ParamManager→`ControlBlock`/`VoiceConfig` adapter; voice scheduling / culling / `getPriorityRating` /
`justCreated` render-spread; transport; preset serialization; the `ModControllableAudio` FX chain; the arpeggiator.
Numerics stay q31 (the boundary types stay numeric-agnostic so the float migration is contained later). Port /
virtual dispatch at block granularity only (Cortex-A9 headroom; the golden masters double as perf canaries).

## Pointers

- Phase-4 roadmap: `docs/dev/synth_phase4_voice_kernel.md` (slices 1–24 + the arc ledger).
- L1 engine/component architecture (the L-vi target): `docs/dev/synth_l1_engine_architecture.md`.
- Overall plan + the two extensibility boundaries: `docs/dev/synth_untangling_plan.md`.
