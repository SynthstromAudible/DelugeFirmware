# Phase 4 — the `synth::Voice` lift

> **Update (2026-06-19) — the in-place decoupling is essentially DONE; the physical relocation (L-viii) is PAUSED.**
> `Voice::render` is now upward-reference-free in the ways that matter: its signature carries no `Sound&` /
> `ModelStackWithSoundFlags*`; its body reaches no `Sound`; the render helpers/engines hold no `Sound&`; the
> scratch is injected (L-v); and **`Voice` has no `friend`s** (the Subtractive/FM couplings are dissolved). That
> decoupling **was the value** of the lift — the voice kernel is now logically separable and render-driven by
> `VoiceConfig` + `ControlBlock`/`ParamPlane` + injected scratch.
>
> The remaining step — physically `git mv`-ing `Voice` into `synth/` (L-viii) so `check_synth_boundary` enforces
> the boundary — is **deliberately deferred**, because it is gated not on any voice coupling but on the **legacy
> `dsp/` DSP subsystems the voice uses** (the filter `dsp/filter/`, plus `dsp/dx`, `dsp/timestretch`, `dsp/util`)
> migrating to `dsp-next`. Those are clean DSP; their relocation is the **dsp-next migration track**, separate from
> and independent of the voice decoupling. Forcing the move now would mean either a large filter/DSP rewrite or
> parking legacy-`dsp/` deps inside the "clean" library (defeating the lint). **Decision (Kate, 2026-06-19): stop
> the lift here.** Resume the physical relocation once the voice's DSP deps are dsp-next-resident; track those on
> the dsp-next migration, not as voice-lift slices. See "Why the lift is paused" below.
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
| ◐ L-iv | `AudioEngine::renderInStereo` (`voice.cpp:1173`), `AudioEngine::cpuDireness` (sample interpolation), `AudioEngine::nextVoiceState` (envelope-stage stamp) | **render-body `renderInStereo` DONE (slice 28)** → `cfg.engineRenderingInStereo`. Remaining: `nextVoiceState` (scheduling — stays app-side) + `cpuDireness`/mic/lineIn (engine-internal → L-vi) |
| ~~L-v~~ ✅ | file-scope `spareRenderingBuffer[0/2/3]` | **DONE (`853414f8`)** — `Sound::render` passes the buffers in as `std::array<int32_t*,4>`; held in a transient `spareBuffers_` member (no engine threading). Global stays App-side for its 6 other users |
| ~~friend-couplings~~ ✅ | `SubtractiveEngine`/`FMEngine` were `friend`s of `Voice` | **DONE (`c8c9f35f`, `6ca138d9`)** — FMEngine owns its operator renders (`renderSineWaveWithFeedback`/etc., moved from Voice); `adjustPitch` → public static; Subtractive's `renderBasicSource`/`areAllUnisonPartsInactive` made public. **`Voice` has no `friend`s.** (The per-source loop relocating *into* the engine layer is later.) |
| ~~VoiceConfig enum dep~~ ✅ | `VoiceConfig` stores `FilterMode`/`FilterRoute` from `gui/l10n`-laden `filter_config.h` | **DONE (`a506e060`)** — `filter_config.h` made gui-free (forward-declared `l10n::String`, `getMorphName` body → `.cpp`). VoiceConfig's enum dependency is synth-clean |
| #54 priority/scheduling | `getPriorityRating` (`sound.voicePriority`/`numActiveVoices`) + `nextVoiceState` stamp in render (`voice.cpp:768/977`) | **Resolve at L-viii, not in place.** `numActiveVoices()` mutates mid-loop (`freeActiveVoice` per-voice in `Sound::render`), so it can't be a loop-stable `VoiceConfig` snapshot; `nextVoiceState` is a global counter. Scheduling stays App-side; the adapter supplies the cull context at relocation |
| **L-viii (PAUSED)** | relocate `Voice` → `synth::Voice` under `synth/` (behind `check_synth_boundary.py`) + a host unit test + the L-vii noteOff hook | **Deferred — gated on the voice's legacy-`dsp/` deps migrating to `dsp-next`** (filter `dsp/filter/`, `dsp/dx`, `dsp/timestretch`, `dsp/util`). Not a voice-coupling problem; see below |

### Why the lift is paused here (2026-06-19)

The decoupling slices above delivered the lift's actual value — `Voice::render` is render-input-driven and holds
no upward control coupling. The only thing standing between that and a literal `synth::Voice` is the
`check_synth_boundary` lint, which forbids the **`dsp/`** prefix: `voice.{h,cpp}` include `dsp/filter/filter_set.h`,
`dsp/dx/engine.h`, `dsp/timestretch/time_stretcher.h`, `dsp/util.hpp`. None of these is a coupling to *sever* —
they are clean DSP the voice legitimately uses. Their only stray reaches are incidental (e.g. `lpladder.cpp` reads
`AudioEngine::cpuDireness` for a CPU-load shortcut; the filter enums live under `model/`). So forcing the
relocation now would mean either a **large idiomatic rewrite of those DSP subsystems into `dsp-next`** (the right
long-term move, but a big separate effort) or **allowlisting legacy `dsp/` inside the library** (which defeats the
boundary's purpose).

**Decision: stop the lift here.** Treat the filter/dx/timestretch → `dsp-next` migration as its own track (where
the oscillators, interpolator, wavetable, and timestretch-crossfade helpers already went). When the voice's DSP
deps are `dsp-next`-resident, L-viii (+ the L-vii noteOff hook + the #54 priority/scheduling re-home) becomes the
near-mechanical move it was always meant to be. Tracked as a dsp-next-migration concern, not a voice-lift slice.

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
