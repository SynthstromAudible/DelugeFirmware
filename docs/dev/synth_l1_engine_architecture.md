# L1 Engine Architecture — the swappable-engine boundary

> Status: **design / north star** (no code yet). Companion to `synth_untangling_plan.md` (the overall L0–L3
> plan and the "Extensibility model — two plug-in boundaries" section). This doc specifies the **L1 engine
> boundary** in depth: what a swappable engine *is*, the two layers beneath/at it, and the migration that gets
> us there. It refines Phase 3 / Phase 4 of that plan; where they disagree, this doc is the newer thinking.

## Why this boundary exists

Phase 3 *extracted* every `OscType` render body into a `*Engine` object, but it drew the boundary at the
**single-oscillator** level: `synth::SourceEngine` (`src/deluge/synth/source/source_engine.h`) is
oscillator-shaped — it exposes `phase()`, takes a `BlockControls::sync` (an `optional<Phasor>` master), and a
`pulse_width`. The consequence is that **`voice.cpp` is still the real engine**: it conducts every cross-source
relationship.

- **Osc-sync** is orchestrated by Voice: `oscSyncPos[u] = unisonParts[u].sources[0].currentOscPhase()`
  (`voice.cpp:1163`) makes **source 0 the master**, and the slave is rendered with `(s == 1) && doingOscSync`
  (`voice.cpp:1261`). It applies in **both** SUBTRACTIVE and RINGMOD.
- **Ring-mod** combines the two sources: render each raw, then `out = (src0 * src1) * amplitudeForRingMod`
  (`voice.cpp:1421–1444`).
- **FM** is a topology over the two sources (forced to sine carriers, `voice.cpp:336–338`) plus
  `kNumModulators = 2` modulators with feedback and an optional `modulator1ToModulator0` route
  (`voice.cpp:1448–1517`).
- **Mix / stereo / unison** are all Voice's job.

The key realisation (the Pigments/Plaits lesson): **hard sync is a relationship _between_ oscillators, not a
property of one.** It — like ring-mod pairing and the FM operator graph — can only live inside something that
owns the oscillators it relates. So the swappable unit should be a **complete timbre generator for one voice**,
and its internal structure (one osc, two oscs with sync, a ring-mod pair, four FM operators, a sample reader, a
live tap) is **private composition, not contract surface**. Plaits and Pigments both swap at exactly this
altitude.

This re-casts what Phase 3 built, honestly: `VirtualAnalogEngine` and `WavetableEngine` are **source
components**, not swap-engines (despite the `*Engine` name). The real swap-engines — Subtractive, RingMod, FM —
do **not exist as objects yet**; Voice is impersonating them.

## The two layers

```
┌─ ENGINE (the L1 swap boundary) ────────────────────────────────────────────────┐
│  flat contract:  noteOn(NoteCtx) · render(Block) → status · release · unassign │
│  NO phase()/sync() — sync/ring-mod/FM-topology are PRIVATE composition         │
│                                                                                │
│   SubtractiveEngine   = N source-components + (A→B) hard-sync + mix            │
│   RingModEngine       = 2 source-components + (A→B) hard-sync + ring-mod       │
│   FMEngine            = 2 sine carriers + 2 modulators + feedback graph        │
│   PlaitsEngine (whole-voice), …  ← third-party coordinator/whole-voice engines │
│   (a whole-voice DX7 engine could live here too — deferred; DX7 is a Source)   │
└───────────────────────────┬────────────────────────────────────────────────────┘
                            │ owns / composes
┌─ SOURCE COMPONENTS (below the boundary) ──────────────────────────────────────┐
│  VA osc · wavetable osc · sample reader · live-input tap · DX7 (self-contained)│
│  THIS layer owns Phasor / pulse-width / sync-as-phase-source                  │
│  (DX7Source / SampleSource / InputSource are here — self-contained generators │
│   used as one of the two composable per-source slots, not coordinator engines)│
└───────────────────────────────────────────────────────────────────────────────┘
```

**Engines** are the swap unit and what a future registry dispatches. **Source components** are the reusable
building blocks engines compose. Sync/ring-mod/FM never appear in the engine contract — they are how specific
engines wire their components together.

## The two contracts

### Engine contract (the swap boundary) — flat

Illustrative (names/shapes not final):

```cpp
class Engine {                       // one voice's worth of a complete timbre generator
public:
    virtual ~Engine() = default;
    virtual void       init(BufferAllocator&) = 0;   // per-voice state; allocator-injected, no globals
    virtual NoteResult noteOn(const NoteContext&)  = 0;
    virtual RenderResult render(const RenderBlock&) = 0;  // adds into the block; returns activity/voice-death
    virtual void       release() = 0;                 // note-off (DX7 keyup, sample loop-end, …)
    virtual void       unassign() = 0;                // free pooled resources (explicit, pooled-slot reuse)
};
```

- `RenderResult` unifies today's divergent signals: VA/wavetable never die (`void`), DX7 returns `active`,
  Sample returns `{ACTIVE, UNASSIGN, SKIP}` (`model/voice/sample_engine.h`). One status type covers all.
- **No** `phase()` / `setIncrement()` / `sync` here — those leak oscillator structure. (osc-sync master/slave
  is internal to Subtractive/RingMod, between *their* components.)
- The `RenderBlock` carries the already-modulated per-block numbers (the spirit of today's
  `synth::BlockControls`) plus output/scratch buffers — but trimmed of the oscillator-only fields
  (`pulse_width`, `sync`), which move down to the component layer.

### Source-component contract (below) — the harder one

This is where the real difficulty is, because it must cover **heterogeneous** components uniformly:

- VA oscillators and the wavetable osc are clean L0/dsp-next citizens (own a `Phasor`, no upward refs) — they
  keep `phase()` / `setIncrement()` / `seek()` and the pulse/sync machinery (that's legitimate at this layer).
- The **sample reader** and **live-input tap** are app-coupled (reach `AudioEngine`, storage, the allocator,
  `ParamManager`). To stay buildable/testable with zero firmware, they sit behind **ports** —
  `SampleSource` (wrapping the existing `voice_sample` path) and a live-input port — injected into the
  component, never reached directly. (`synth_untangling_plan.md` already names the `SampleSource` port.)

The component contract must let an engine: set per-block pitch, drive (or not) a hard-sync phase source, read a
component's phase (for the engine's *internal* sync), and render raw or VCA'd. Most of this already exists on
the Phase-3 objects — the work is regularising it and adding the port seam.

## Mapping the current Deluge onto the target

| Today | Becomes | Notes |
|---|---|---|
| `SynthMode::SUBTRACTIVE` (`definitions_cxx.hpp:421`) | `SubtractiveEngine` | owns the 2 source slots + A→B sync (`voice.cpp:1229–1315`) + mix |
| `SynthMode::RINGMOD` | `RingModEngine` | 2 raw components + multiply (`voice.cpp:1366–1444`); sync still applies |
| `SynthMode::FM` | `FMEngine` | 2 sine carriers (sources) + 2 modulators + feedback (`voice.cpp:1448–1517`) |
| `OscType::DX7` | **`DX7Source`** (stays a Source) | a self-contained 6-op FM voice **used as a per-source oscType** — see the resolution below |
| `OscType::{SAW,SQUARE,…,WAVETABLE}` | **source components** | VA / wavetable building blocks |
| `OscType::{SAMPLE, INPUT_*}` | **source components** (port-backed) | used *inside* Subtractive/RingMod |

**Feature-preservation requirement (hard constraint):** Subtractive and RingMod must keep hosting
**heterogeneous** source components — osc A a sample or wavetable, osc B a saw, mixed or ring-modulated, exactly
as today. The mechanism is precisely that sample/wavetable/input are *components* that plug into those engines;
nothing about promoting the synth modes to engines removes the per-source choice.

**Resolved (2026-06-19): DX7 is a composable Source, not a (replacement) Engine.** Verified against `voice.cpp`:
`oscType`/`dxPatch` are **per-source** (`processing/source.h:38,47`) and DX7 dispatches per-source
(`voice.cpp:1523` → `unisonParts[u].sources[s].dx7Source`), so a preset can run **osc A = DX7, osc B = saw, mixed**
(non-FM modes only — `voice_unison_part_source.cpp:49`). Promoting DX7 *from* a source *to* an engine would break
that — so **DX7 stays a `DX7Source`.** Its 6-op FM graph is *private composition* of the Source, exactly as a
wavetable source privately owns a phasor + table. This generalizes: a self-contained foreign generator (a Plaits
macro-oscillator) is also **Source-shaped** in the Deluge's two-source mixer — "complete timbre generator" ≠
"Engine" here; **"Engine" = coordinator of the 2-source mixer** (Subtractive/RingMod/FM). A whole-voice **DX7
engine** (DX7 *is* the voice, no source-mixing) is a legitimate *additional* end-state — DX7 as **both** a Source
and an Engine — but it is **deferred** (decision: Kate, 2026-06-19): it is partly redundant (osc-A-only DX7 already
yields a whole-voice DX7) and is a UX/product call, so it is not a near-term engine-tier slice. The `EngineRegistry`
contract should stay open to admitting such a self-contained whole-voice engine later, alongside the coordinators.

## Voice / unison / Phase-4 relationship

- **An engine = one voice's worth.** Voice instantiates an engine **per unison part** (unison stays a
  Voice/voice-pool concern; sync is per-unison today — `oscSyncPos[u]`). Detune/pan across unison parts remain
  above the engine.
- **Forming Subtractive/RingMod/FM = pulling the cross-source orchestration out of `voice.cpp`.** That *is* the
  front half of **Phase 4** (the L2 voice kernel: "old `Voice`/`Sound::render` shrink to an adapter"). So the
  engine cut and the voice-kernel cut are nearly the same cut — plan them as one effort, not two.

## Extensibility tie-in

This is the **Engine API (L1)** boundary from `synth_untangling_plan.md`'s "Extensibility model — two plug-in
boundaries." A third-party engine (Plaits, Rings) implements the flat `Engine` contract and inherits the
Deluge's envelopes/LFOs/patch-matrix/MPE/unison/filters/FX. The **registry** dispatches *engines* (not
oscillators) and is the substrate for runtime-pluggable engines — worth building when that's a goal. **CLAP
stays at L3** (whole-instrument), per that doc; L1 stays a lean native C++ interface (per-voice instantiation
rules CLAP out at L1).

## Migration sketch (each slice golden-gated, x86 ≡ ARM)

High level only; each gets its own per-slice plan:

1. **Reframe the component layer** — name + document VA/wavetable/sample/input as *source components* (the
   `synth::SourceEngine` name likely becomes the component base or is renamed); add the `SampleSource` /
   live-input **port** seams.
2. **`RingModEngine`** — relocate the ring-mod combine + sync (`voice.cpp:1366–1444`) verbatim into an engine
   owning its 2 components. Smallest cross-source engine; good first proof.
3. **`SubtractiveEngine`** — relocate the render-each-source + mix + A→B sync (`voice.cpp:1229–1315`).
4. **`FMEngine`** — relocate carriers + modulators + feedback (`voice.cpp:1448–1517`).
5. ~~**Promote `DX7`**~~ **Dropped (2026-06-19): DX7 stays a `DX7Source`** — it is a composable per-source oscType
   (osc A = DX7, osc B = saw, mixed), so an engine-only promotion would break compat. A whole-voice DX7 *engine* is
   a deferred *additional* end-state (DX7 as both), not a near-term slice. See "Mapping the current Deluge" above.
6. **Registry** — replace the `SynthMode`/`oscType` dispatch with engine selection; this is the L1 plug point. Keep
   the contract open to a future self-contained whole-voice engine (a DX7 engine, Plaits-as-voice) beside the
   coordinators.
7. Voice shrinks to the L2 adapter (Phase 4) as the orchestration leaves it.

## Open questions this doc takes a position on (to finalise during slice 1)

- **Naming.** The Phase-3 `*Engine` classes are mis-named for the new model: `VirtualAnalogEngine`,
  `WavetableEngine`, `SampleEngine`/`InputEngine`, **and `DX7Engine`** are all *source components* (Sample/Input/DX7
  are used inside Subtractive/RingMod), plus the `synth::SourceEngine` interface. **None of the oscTypes is an
  engine** — `*Engine` is reserved for the coordinator tier (Subtractive/RingMod/FM); see the DX7 resolution above.
  Three-tier scheme:

  | Layer | Suffix | Examples |
  |---|---|---|
  | dsp-next kernels (L0) | `*Oscillator` (exists) | `TableOscillator`, `PulseOscillator` — keep |
  | source (per-source renderer) | `*Source` | `VirtualAnalogSource`, `WavetableSource`, `SampleSource`†, `InputSource`, **`DX7Source`**; **base `synth::Source`** (was `SourceEngine`, **folding in** the `SourceRenderResult` outcome) |
  | engines (coordinator / swap) | `*Engine` | `SubtractiveEngine`, `RingModEngine`, `FMEngine`; base `Engine`. (A future whole-voice DX7/Plaits engine could join — deferred.) |

  It reuses the Deluge's own word (osc A/B are already "sources"). **Collisions:** (a) `WavetableOscillator`
  already exists *twice* (`dsp/oscillators/wavetable_oscillator.h` + `dsp-next/fixed/wavetable/wavetable_oscillator.h`)
  and dsp-next owns the whole `*Oscillator` building-block vocabulary; (b) †`SampleSource` is also the reserved
  **port** name in `synth_untangling_plan.md`; (c) the base `Source` brushes against the existing global
  `class Source` (preset-side config, `processing/source.h`) — resolved by the namespace: the new base is
  `deluge::synth::Source`, distinct from the global `::Source`.

  **Decision (2026-06-17): rename now, base-first, not deferred.** The base interface `synth::SourceEngine`
  becomes **`synth::Source`** and *folds in* the `SourceRenderResult` outcome (the per-source renderer reports
  its own lifecycle, replacing the separate `renderSourceComponent` dispatch) — this is the Source tier of the
  `Phasor → Osc → Source → Engine → Voice → Instrument` stack (`synth_untangling_plan.md`), with "Engine"
  reserved for the *coordinator* tier above. **Slice 1 does the base rename + the fold for the VA source only**
  (VA already implements the interface; smallest bit-exact proof), routing just the VA branch of
  `renderSourceComponent` through `synth::Source`. **DONE — the scheme is fully realised:** the base became
  `synth::Source`, all five sources implement it (L-vi), and the concrete-class renames landed (2026-06-17, commit
  7f0c8fd6) — `VirtualAnalogEngine→VirtualAnalogSource`, `WavetableEngine→WavetableSource`,
  `SampleEngine→SampleSource`, `DX7Engine→DX7Source`, `InputEngine→InputSource`. `*Engine` is now reserved for the
  coordinator tier (`SubtractiveEngine`/`RingModEngine`/`FMEngine`).
- **Does sync apply to non-oscillator components?** Today sync is phase-reset; it's only meaningful for
  oscillator components. Define whether a sample/input component can be a sync slave (likely no).
- **Where `BlockControls` lives.** Split: the amplitude/`apply_amplitude`/`cpu_direness` part stays a
  per-block engine/component control; the oscillator-only `pulse_width`/`sync` move to the component layer.
- **Param/mod routing.** How per-source `LOCAL_OSC_A/B_*` (and `LOCAL_MODULATOR_*`) map onto engine-internal
  components while staying **bit-exact** — the orchestration in `voice.cpp` is load-bearing.
- **Port shapes.** Concrete `SampleSource` / live-input port signatures (named here, designed with their slice).

## Non-goals

Any code change (this is the north star); the registry implementation; concrete port signatures; the FX-chain /
CLAP-effect boundary (a separate, third plug point, out of scope per `synth_untangling_plan.md`).
