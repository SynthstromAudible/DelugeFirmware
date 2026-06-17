# Phase 4 — the `synth::Voice` lift

> Status: **roadmap / in progress.** The Phase-4 endgame (sub-slice **f** of `synth_phase4_voice_kernel.md`).
> Slices 1–24 made `Voice::render`'s *signature* free of `Sound&` and `ModelStackWithSoundFlags*`, and removed
> every `Sound&` read from the render helpers/engines. This doc frames the remaining work: severing the render
> path's residual upward reaches, then relocating to a portable `synth::Voice`. Each step lands as its own
> bit-exact, golden-gated commit (x86 byte-identical + targeted ARM/qemu).

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

`Voice::render`'s signature is `render(ParamManagerForTimeline* paramManager, int32_t* soundBuffer, …flags…,
int32_t externalPitchAdjust, const VoiceConfig& cfg)`. In its **body**, `sound` survives in only two spots
(`combineExpressionValues` + the patcher input); the rest of the residual coupling is a handful of other reaches
across the render path. The data-plane vehicles already exist (Phase 2): `ControlBlock` (`synth/control_block.h`,
the patcher input) and `ParamPlane`/`VoiceParams` (`synth/param_plane.h`, the per-voice param data).

## The lift surface (remaining render-path coupling)

| # | Reach (current) | Disposition |
|---|---|---|
| ~~L-i~~ ✅ | patcher input `sound.patchControlBlock(*paramManager)` (`voice.cpp:826`) | **DONE (slice 25)** — `ControlBlock` built at the adapter, passed into `Voice::render` as a `const ControlBlock&` param |
| ~~L-ii~~ ✅ | `combineExpressionValues(sound, i)` (~`voice.cpp:802`, MPE bend/expression smoothing) | **DONE (slice 26)** — takes the `synthLevelValue` (= `cfg.monophonicExpressionValues[i]`) instead of `Sound&`; the dead `Sound&` render-body local removed → **`Voice::render`'s body is `sound`-free** |
| ~~L-iii~~ ✅ | `getLocalLFOPhaseIncrement` (`voice.cpp:718`) → `sound.lfoConfig[id]` + `sound.getSyncedLFOPhaseIncrement(config)` | **DONE (slice 27)** — synced increments snapshotted into `cfg`; the helper takes `(LFOConfig, param, syncedIncrement)`, no `sound` reach |
| **L-iv** | `AudioEngine::renderInStereo` (`voice.cpp:1173`), `AudioEngine::cpuDireness` (sample interpolation), `AudioEngine::nextVoiceState` (envelope-stage stamp) | inject / pass per block (a small "render context") |
| **L-v** | file-scope `spareRenderingBuffer[0/2/3]` | injected scratch buffers |
| **L-vi** | the storage-reaching engines (sample / input / DX7) + `getPriorityRating` (`sound.voicePriority` / `numActiveVoices`, used by the sample engine for caching priority) | the deferred **`SourceComponent` / `SampleSource` port** — the largest arc |
| **L-vii** | `Voice::noteOff`'s internal `this->sound` (the deferred-note-off edge) | note-policy hook |
| **L-viii** | relocate `Voice` → `synth::Voice` under `synth/` (behind `check_synth_boundary.py`) + a host unit test | the move itself |

### Open decision — L-vi port strategy (settle when L-i…L-v are done, not now)

The storage-reaching engines (`SampleEngine`, `InputEngine`, `DX7Engine`) are app-coupled (reach `storage`,
`AudioEngine`, the allocator). Two ways to take them out of the kernel's upward refs:

1. **Full abstract interface** — a `SourceComponent` base + `SampleSource` / live-input ports the kernel depends
   on; impls live app-side. The L1-doc target (`synth_l1_engine_architecture.md`). Biggest.
2. **Port boundary** — the kernel calls injected interfaces whose implementations stay app-side, *without* fully
   relocating them into the library. The L1 doc explicitly allows this as acceptable.

Decide once L-i…L-v are landed and the remaining surface is concrete. This arc is where the Subtractive/FM
`friend`-couplings dissolve.

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
