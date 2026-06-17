# Phase 4 L-vi — the `SourceComponent` interface + source ports

> Status: **REFINED — see `synth_source_ownership.md`.** This doc sketched a `SourceComponent` interface as
> dispatch-polymorphism *over the legacy flat param plane*. That conflated three separable concerns (dispatch
> boundary / param ownership / dsp-next residence). The reframe: **the lift (L-vi) needs only the ports** — the
> oscType switch doesn't trip the boundary; the app reaches inside the sample/input engines do. So **L-vi = the
> `SampleSource` + live-input ports + the `Source::renderInStereo` `AudioEngine` globals**, keeping the oscType
> dispatch. The **polymorphic, param-owning `SourceComponent`** moves to the OO Source phase (`synth_source_ownership.md`,
> phase A) — designed *with* typed per-component params, not over `LOCAL_* + s`. The `SourceComponent` contract +
> port sketches below remain useful inputs for that phase. Each step bit-exact + golden-gated.

## Why

To lift `Voice`→`synth::Voice` under `synth/`, the per-source render dispatch must not reach
`model`/`processing`/`storage`. Today `Voice::renderSourceComponent` is an **oscType switch** over five
heterogeneous per-source objects on `VoiceUnisonPartSource`:

```
std::optional<synth::VirtualAnalogEngine> vaEngine;   // pure DSP (L0) — phase owned here
std::optional<dsp::WavetableEngine>       wavetableEngine; // pure DSP (L0)
SampleEngine                              sampleEngine; // app-coupled: model/sample, storage/cache, AudioEngine, playbackHandler, getPriorityRating
dsp::DX7Engine                            dx7Engine;    // dsp/dx (L0) — pooled DxVoice + injected patch (slice 14)
InputEngine                               inputEngine;  // app-coupled: AudioEngine live-input ring + mic/line + LivePitchShifter
```

`renderSourceComponent` already returns the uniform `SourceRenderResult {ACTIVE, UNASSIGN, SKIP}`
(`model/voice/source_component_render.h`). **Boundary-blocking reaches** = SampleEngine + InputEngine (and the
`Source::renderInStereo` `AudioEngine` globals + `getPriorityRating`→`sound`). **DX7 + VA + wavetable are already
L0** (dsp) — they need no port, only to adopt the base interface.

## The `SourceComponent` contract

A polymorphic per-source render replacing the oscType switch:

```cpp
class SourceComponent {
public:
    virtual ~SourceComponent() = default;
    // Render one unison part of one source for this block, folding the outcome into the uniform result.
    virtual deluge::voice::SourceRenderResult render(const SourceRenderContext& ctx) = 0;
};
```

`SourceRenderContext` bundles `renderSourceComponent`'s current ~16 per-block args (u, s, oscBuffer, numSamples,
stereoBuffer, sourceAmplitude/Increment, phaseIncrement, doOscSync + oscSync arrays, waveIndexIncrement,
stereoUnison, amplitudeL/R) + the `VoiceConfig`-derived per-source config it already reads. The phase/sync state
stays owned by the component (as it is today on `vaEngine`/`wavetableEngine`). **Open:** whether to pass the
context by struct-ref (clean) vs keep the wide arg list (zero ABI risk); struct-ref preferred.

## Source ports (the app-coupled dependencies — impls stay app-side)

The two app-coupled components depend on **injected ports**, never reaching `model`/`processing`/`storage`
directly:

- **`SampleSource` port** — wraps the existing `voice_sample` path: solicit/release the pooled `VoiceSample`
  (note-on/unassign), the per-block read (pitch/speed/loop/caching), `cpuDireness`-driven interpolation choice,
  cache setup, and the caching-priority input (from `getPriorityRating`, passed in). The biggest port; the sources
  config it needs (`sampleControls`/`repeatMode`/etc.) arrives via the context/config, not `voice.sound`.
- **live-input port** — the `AudioEngine` input ring + mic/line state + the `LivePitchShifter` lifecycle/alloc.

Pure components (`VirtualAnalogEngine`, `WavetableEngine`) need **no port** (L0 already). `DX7Engine` stays
`dsp/dx` (its pooled `DxVoice` via `getDxEngine()` is L0-internal — *allowed* below the `synth/` boundary); it
adopts the base but needs no port.

## How a source holds its component

Today the five engines are concrete members + an oscType switch picks one. Target: dispatch via a
`SourceComponent*` (or `std::variant`) selected at note-on by oscType. **Open design choice:**
1. keep the concrete members, add a `SourceComponent* active` set at note-on → `renderSourceComponent` becomes
   `active->render(ctx)`. Minimal storage change; the members stay (so `phase()`/`seek()`/lifecycle stay typed).
2. a `std::variant`/owning base-pointer holding exactly the active component.
Lean **(1)** — least churn, keeps the existing per-component lifecycle hooks, and the base call is the only new
seam.

## `getPriorityRating`

Voice-culling (`sound.voicePriority`/`numActiveVoices`) — **stays app-side** (the adapter/scheduler owns it). The
SampleSource port's caching decision takes the priority as an injected value (not `voice.getPriorityRating()`).

## Bit-exactness strategy

The render *code* moves verbatim into the component `render()` impls (the bodies already live in
`SampleEngine::render` / `InputEngine::render` / `DX7Engine::render` / `renderVaSource` / `renderWavetableSource`).
The only behavioural change is **switch → virtual dispatch** — same math, same order, byte-identical. Dispatch is
**per-source-per-block** (not per-sample) → fine on Cortex-A9; the golden masters double as perf canaries.

## Slice sequencing (each bit-exact, golden-gated)

Strangler-fig — introduce the base + ports without flipping dispatch until each impl is ready:

1. **`SourceComponent` base + `SourceRenderContext`** — define the interface; make the two **pure** components
   (VA, wavetable) implement it; route their branch of `renderSourceComponent` through the base. Lowest risk
   (no ports). Establishes the contract + dispatch seam.
2. **DX7** — adopt the base (no port; dsp-internal).
3. **`Source::renderInStereo`** — inject its 3 `AudioEngine` globals (so the subtractive driver's stereo decision
   is port/config-fed, not a `processing` reach).
4. **live-input port** — define it; `InputEngine` becomes a port-backed `SourceComponent`.
5. **`SampleSource` port** — the big one; `SampleEngine` becomes a port-backed `SourceComponent` (pool/read/cache/
   sync/priority all via the port + injected priority).
6. **Flip dispatch** — `renderSourceComponent` → a single `component->render(ctx)`; retire the oscType switch.
   (This dissolves the Subtractive/FM `friend`-couplings the per-source render currently relies on.)

Then L-viii (relocation) can move the now-port-backed kernel to `synth::Voice`.

## Open questions (resolve as slices land)

- `SourceRenderContext` shape (struct-ref vs wide args) — start struct-ref.
- Component holding — (1) `SourceComponent* active` member vs (2) variant; lean (1).
- Port ownership/injection point — ports injected at note-on (adapter domain) vs construction; the impls are
  app-side singletons/wrappers (AudioEngine voice-sample pool, live-input).
- Where the `SampleSource`/live-input port interfaces live (`synth/` headers; impls in `model`/`processing`).
- Whether sync applies to non-oscillator components (likely no — sample/input aren't sync slaves).

## Pointers

- L-vi framing + the lift surface: `docs/dev/synth_voice_lift.md`.
- The L1 engine/component architecture (the parent): `docs/dev/synth_l1_engine_architecture.md`.
- Phase-4 roadmap + the arc ledger: `docs/dev/synth_phase4_voice_kernel.md`.
