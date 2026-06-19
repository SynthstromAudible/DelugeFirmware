# Synth Untangling — State-Decoupling Audit & the Concrete End-Target Object Model

> Status: **audit + target architecture** (2026-06-19). A fresh ground-truth pass over the *actual code* (not just
> the phase docs), prompted by the question "architect a **concrete** end-target object model, CLAP-shaped for both
> the engine slot and the whole instrument." The headline finding: the remaining work is overwhelmingly
> **state-relocation, not behaviour-extraction** — the behaviour has been shaped to the target objects, but state
> ownership has not been re-homed to match. Companions: `synth_untangling_plan.md` (the L0–L3 plan + the two
> extensibility boundaries), `synth_l1_engine_architecture.md` (the engine/source contracts),
> `synth_phase4_voice_kernel.md` + `synth_voice_lift.md` (the voice decoupling), `synth_source_ownership.md` (typed
> params). Where this doc and those disagree on sequencing, this is the newer thinking.

## 1. Where the code actually is (tier by tier)

| Tier | Target home | Reality today | Verdict |
|---|---|---|---|
| **L0 primitives** | `dsp-next/` | osc / wavetable / interpolation / sync / vca / timestretch migrated; `core/fixed/float` split real. **`dsp/filter`, `dsp/dx`, `dsp/util` not yet migrated.** | Clean; one migration arc outstanding |
| **L1 Source** | `synth/source/` | **Done.** Five sources are polymorphic `synth::Source` (VA, wavetable, sample, input, DX7); ports exist (`sample_playback_port.h`, `audio_input_port.h`, `dx_voice_port.h`). | The strong part of the tree |
| **L1 Engine** | `synth/` | Subtractive/RingMod/FM exist as objects but live in `model/voice/`, **own behaviour but no state**, and reach state through `Voice&` / per-block args. Cross-source orchestration (RINGMOD/FM per-unison loop) is still in `voice.cpp:1199–1282`. DX7 still a Source, not an Engine. No registry; dispatch is `SynthMode`+`OscType` switches. | **Stateless coordinators** |
| **L2 Voice** | `synth/` | Decoupled where it counts (no `Sound&` in render sig/body, no `friend`s, scratch injected) but still holds `Sound& sound`, lives in `model/voice/`. Lift (L-viii) **paused** on the dsp-next filter migration. | Logically separable, physically stuck |
| **L3 Instrument** | `synth/` (CLAP-shaped) | **Does not exist.** `Sound` (~5,000 LOC) is `ModControllableAudio` + `Voiced` at once: preset, DSP processor, UI controller, file format, voice pool, arp, FX. | God-object intact |

The bottom two layers are genuinely good; the Engine tier is a façade; the Instrument tier is unstarted.

## 2. Headline finding — the blocker is **state**, not DSP

### 2.1 The Deluge already has CLAP's split, but `Sound` fuses it back

CLAP separates three channels: **audio** (`process`), **params** (event stream), **state** (save/load blob). The
Deluge maps onto these almost perfectly:

- **params-as-events** → `ParamManager` already lives **outside** `Sound` (it arrives via `ModelStack`; it is *not* a
  `Sound` member). The preset and its live modulated values are already separate objects — the hardest part of a
  CLAP port is effectively done.
- **audio** → the render path, now largely `Sound&`-free (Phase 4).
- **state** → serialization is **welded into `Sound`** (`readFromFile`/`writeToFile`/`writeParamsToFile`), with **no
  object boundary**. The preset-config fields are bare members of `Sound` (`synthMode`, `lfoConfig`, unison,
  modulator routing, `oscRetriggerPhase`, transpose…) and of `processing/source.h`'s `Source` (oscType,
  sampleControls, ranges, dxPatch).

### 2.2 `VoiceConfig` is the smoking gun

`model/voice/voice_config.h` is a ~40-field snapshot copied per-block. It exists **only because there is no
library-resident preset object** the kernel can hold a reference to — so every config field gets hand-marshalled.
It also conflates three lifetimes in one bag: true preset config (changes on edit), sound-level transport state
(per block, e.g. `lfo*SyncedPhaseIncrement`, `engineRenderingInStereo`), and derived predicates
(`sourceActiveCurrently`, `noiseVolumeContainsSomething`). The grab-bag is the cost of the missing object. In the
end-target it dissolves into a held `Patch&` (config) + a per-block `ControlBlock` (transport/modulation) — exactly
CLAP's static-params-vs-per-block-events split.

### 2.3 The flat `LOCAL_*` param plane blocks the L1 engine API

The patcher writes a flat `paramFinalValues[]`; sources read their slice by global offset
(`paramFinalValues[LOCAL_OSC_A_PHASE_WIDTH + s]`). POD-backed now (the offsetof bug class is dead, issue #2660),
but still **flat + globally indexed**. A foreign L1 engine cannot be handed the Deluge's global param enum — so this
directly blocks "bring a source, inherit the Deluge." Fix is scoped in `synth_source_ownership.md` (typed
source-owned params, bridge → native); the audit confirms it is load-bearing for extensibility, not cosmetic.

### 2.4 Why the Engine tier is hollow — the *same* root cause

The state an Engine should own already exists; it lives one level **up** and one level **down** from the engine:

```
Voice
 ├── unisonParts[u]            VoiceUnisonPart      ← per-unison state
 │     ├── modulatorPhase[]/modulatorFeedback[]      (FM modulator grid)
 │     └── sources[s]          VoiceUnisonPartSource ← per-source × per-unison state
 │           ├── vaSource / wavetableSource / sampleSource / dx7Source / inputSource  (osc phase lives here)
 │           └── oscPos, carrierFeedback, ...
 ├── subtractiveEngine   ← stateless; borrows the above via Voice&
 ├── ringModEngine       ← stateless; takes Source structs by value each block
 └── fmEngine            ← stateless; reads modulatorPhase[] through Voice&
```

State is indexed **unison-major** (`VoiceUnisonPart` owns `sources[]`), but an Engine coordinates **across** sources
(source 0 ↔ 1: sync master/slave, ring-mod multiply, FM matrix) **within** one unison part. For an engine to own its
state it must own the `[source]` axis of that grid. So the engines are stateless for two reasons:

1. **Strangler discipline** — every slice is bit-exact; relocating *behaviour* (coordination logic) while leaving
   state in place is a smaller, easier-to-verify step than relocating member arrays. Behaviour first, state last.
2. **The unison layout is orthogonal to the engine's span** — moving state into the engine means restructuring
   `VoiceUnisonPart`/`VoiceUnisonPartSource` so each unison part holds **one Engine** that holds **its Sources + FM
   grid**. That is the deepest state-relocation in the whole effort.

`VoiceUnisonPart` is **already the proto-engine** (it holds the FM grid + `sources[]`). The end-target engine body
essentially exists; it is just not named/scoped as an engine, and the cross-source logic lives in the stateless
shells / `voice.cpp` rather than on it.

**Conclusion: whether it's `Patch` (config stuck in `Sound`) or engine state (osc/FM stuck in the unison grid), the
remaining work is state-relocation. "Decouple state first" is the correct instinct.**

## 3. The concrete end-target object model

```
deluge::synth::

  ── STATE (the thing to extract; CLAP "state" + "params") ──
    Patch              preset definition, serializable. Absorbs Sound's config members + Source[]'s config.
                       The CLAP state blob. Held by-reference by the Instrument; numeric-clean.
    PatchMatrix        modulation routing (today's PatchCableSet); a Patch sub-object.
    ParamValues        live modulated plane (today's ParamPlane/paramFinalValues), but TYPED + per-engine-
                       namespaced, not flat LOCAL_*. Filled by the Patcher from ParamManager-world (CLAP param events).

  ── DSP (CLAP "audio") ──
    Phasor, Oscillator         L0 dsp-next (exist)
    Source : interface         L1 per-source renderer (exist, in synth/source/)
      VirtualAnalogSource, WavetableSource, SampleSource, InputSource, DX7Source
    Engine : interface         L1 coordinator / SWAP UNIT (lean native C++, NOT CLAP); one unison-part's worth
      SubtractiveEngine    = N Sources + A→B hard-sync + mix
      RingModEngine        = 2 Sources + multiply
      FMEngine             = 2 carriers + 2 modulators + feedback graph
      DX7Engine            = self-contained 6-op (promotes from a Source)
      PlaitsEngine, …      ← third-party engines drop in here (typed params)
    EngineRegistry             dispatches Engines (replaces SynthMode/OscType switch)
    Voice : composes Engine(s) + VCA/filter/envelopes/LFOs/unison/MPE        L2
    VoicePool                  L2; owns allocation; app supplies cull POLICY via hooks
    Instrument                 L3 CLAP-shaped C-ABI: process(event_list, audio_block); owns the VoicePool.
                               Deluge-synth-as-plugin is the first implementor.
```

**Two plug boundaries (unchanged from `synth_untangling_plan.md`; they nest, not compete):**

- **L1 `Engine`** — lean native C++, per-voice/per-unison, typed params, q31-capable, allocator-injected. *"Bring a
  source, inherit the Deluge"* (envelopes/LFOs/patcher/MPE/filters wrap it).
- **L3 `Instrument`** — real CLAP C-ABI, owns its own polyphony. *"Bring a whole synth, inherit the sequencer."* The
  Deluge synth, expressed as the first such plugin, is the forcing function that proves the L2 kernel is decoupled.

## 4. The dependency-ordered path (what unblocks what)

1. **Fill the Engine tier (front-half of Phase 4) — NEXT.** Pull the RINGMOD/FM per-unison orchestration out of
   `voice.cpp:1199–1282` into the `*Engine` objects so they *own their composition*; give engines ownership of their
   Sources + FM grid (the `VoiceUnisonPart` restructure); move them to `synth/`; add the `EngineRegistry`; promote
   DX7 Source→Engine. Gated on nothing major. The L1 doc notes this is "nearly the same cut" as the voice kernel — do
   it before more voice-lift slices. **Detailed plan: §5.**
2. **Extract `Patch` from `Sound`/`Source` — the "decouple state first" move.** Collapses `VoiceConfig` into a held
   `Patch&` + per-block `ControlBlock`; it is the object the L3 state-blob serializes. Everything at L3 is blocked on
   it; nothing below is.
3. **Typed source-owned params** (`synth_source_ownership.md` Stage A bridge). Required for a *real* foreign L1 engine
   API (foreign code can't see `LOCAL_*`). Naturally co-lands with engines owning their Sources (step 1).
4. **Voice lift (L-viii)** — mechanical, once `dsp/filter`+`dsp/dx`+`dsp/util` reach dsp-next.
5. **VoicePool + L3 `process()`** — voice allocation moves inside the Instrument; app keeps culling *policy* via
   hooks; CLAP C-ABI lands; Deluge-synth-as-plugin proves it.

Non-obvious takeaway: **steps 1 and 2 are higher-leverage than continuing the voice lift.** The lift is paused on a
DSP-migration chore; the Engine-tier fill and the `Patch` extraction are what actually stand between us and a
CLAP-shaped L3, and `Patch` is the keystone (all three CLAP channels pivot on it; `VoiceConfig`'s existence proves it
is missing).

## 5. Step 1 detail — filling the Engine tier

Goal: the `*Engine` objects own their cross-source orchestration **and** their per-unison state, dispatched through a
registry, living in `synth/`. End-state: `voice.cpp`'s render no longer contains a `SynthMode` switch or the
RINGMOD/FM per-unison loop — it calls `engine.render(block)` per unison part. Each slice bit-exact, golden-gated
(x86 byte-identical + targeted ARM/qemu), in the proven cadence.

### The asymmetry to exploit

The three engines are *not* in the same shape today:

- **`SubtractiveEngine` already owns its per-unison loop** — `voice.cpp:1184` is a single `subtractiveEngine.render(*this, …)`
  call; the per-source + osc-sync loop lives *inside* the engine (it delegates each source back to
  `Voice::renderBasicSource`).
- **`RingModEngine` and `FMEngine` do not** — `voice.cpp:1199–1282` is a loop *in Voice* that does shared
  pitch-adjust setup (`1208–1237`) then dispatches **per unison part** to `ringModEngine.render(...)` (`1270`) or
  `fmEngine.render(*this, u, …)` (`1277`).
- The engines reach state through `Voice&` (FM: `modulatorPhase[]`; Subtractive: `renderBasicSource`) or take it by
  arg (RingMod: `Source` structs built at `1244–1268`).

So step 1 begins by **removing that asymmetry**: give RingMod and FM their own internal unison loops, exactly like
Subtractive. The dispatch (`voice.cpp:1181–1285`) then collapses to a clean 3-way switch of whole-voice
`engine.render(...)` calls.

### The three boundaries (where each piece lands)

- **Stays in Voice — per-voice pre-roll, passed in (`voice.cpp:1010–1179`):** source amplitudes/increments,
  `modulatorsActive`, osc-sync master capture, noise fill. SynthMode-conditional setup the engines *consume*;
  leaving it in Voice and passing it in keeps each slice bit-exact (exactly how Subtractive works today). Migrates
  *into* the engines later, in the state-ownership slice (2).
- **Moves into the engine — the per-unison source loop (`voice.cpp:1199–1285`):** the shared pitch-adjust setup +
  the ring-mod combine / FM topology.
- **Stays in Voice — the L2 wrapper, post-engine (`voice.cpp:1287+`):** fold / filter / VCA / saturation / output.
  The voice chain *wrapping* the engine — correctly a Voice concern, not engine.

### Slice sequence (each bit-exact)

1. ✅ **DONE — Make RingMod & FM own their unison loop (mirroring Subtractive).** Two sub-slices, smallest-first per
   the L1 doc's migration sketch:
   - **1a — `RingModEngine` owns its unison loop** (commit `f1ee3341`). Pulled the RINGMOD path out of `voice.cpp`'s
     loop into a whole-voice `RingModEngine::render(...)` that iterates unison internally (pitch setup + combine);
     the old per-unison render became private `renderUnisonPart`. `goto skipUnisonPart` → `return` (faithful).
   - **1b — `FMEngine` owns its unison loop** (commit `5fc6d214`). Same move for FM; `sourceAmplitudes` stays mutable
     (a too-high carrier zeroes the source, persisting across ascending-frequency unison parts). `Voice::render`'s
     dispatch is now the clean 3-way `SUBTRACTIVE → engine, RINGMOD → engine, FM → engine` — **no cross-source
     orchestration remains in the Voice-side render.**
   Behaviour-only — no state moved; engines still borrow per-source state via `Voice&`. Each bit-exact (x86 336/336
   byte-identical + ARM/qemu oracle on ringmod/FM families; boundary lints green).
2. ✅ **DONE — `FMEngine` owns the FM modulator grid (first stateful engine)** (commit pending). **Scope narrowed
   from the original "all three engines own their Sources" framing** by the state-lifecycle audit: exploration found
   **only FM has mode-exclusive per-voice state.** The modulator grid (`modulatorPhase`/`modulatorPhaseIncrement`/
   `modulatorFeedback` per unison + `modulatorAmplitudeLastTime`) is written only under `synthMode==FM` gates and read
   only in `fm_engine.cpp`; it moved into `FMEngine` (`std::array[unison][modulator]`), and `VoiceUnisonPart` became
   a pure source container. **RingMod and Subtractive have no mode-exclusive state** — they compose the *shared*
   per-source components on `VoiceUnisonPartSource` (used by all modes), so their "Source ownership" is the
   typed-param / Source-tier concern (step 3, `synth_source_ownership.md`), **not** a state move. The carrier
   `oscPos`/`carrierFeedback` stay on `VoiceUnisonPartSource` (Source-tier, mode-shared); the grid *setup* logic
   stays in `Voice` (writes `fmEngine`'s public arrays) until the kernel lift. (`setSynthMode` kills voices first, so
   the relocation is reinit-neutral.) Bit-exact: x86 336/336 + ARM/qemu FM families; lints green.
3. **Promote DX7 Source→Engine.** `OscType::DX7` becomes an Engine selection rather than a Source oscType (it is
   already a complete 6-op voice; `dx7_source.{h,cpp}` → `DX7Engine`).
4. **Introduce the `EngineRegistry`.** Replace the `SynthMode`/`OscType` dispatch with engine selection — the L1 plug
   point. A closed registry over the built-in set first; runtime registration when foreign engines become a goal.
5. **Move the Engine tier to `synth/`** behind `check_synth_boundary.py`. Co-lands with step 1's typed-param bridge
   where the engines feed their owned Sources (`synth_source_ownership.md` Stage A) so no `LOCAL_*`/`model/` leak
   crosses the boundary.

### Constraints / guardrails

- **Bit-exact every slice** (the prime directive). The osc-sync "ascending-frequency / affects-all-unison-parts"
  quirk (`voice.cpp:1204–1206`) and the `modulatorsActive` cross-unison persistence (`fm_engine.h`) are load-bearing
  — preserve verbatim when relocating.
- **Block-granularity dispatch only** (Cortex-A9 headroom); per-unison Engine instances are cheap value types.
- **Feature preservation:** Subtractive/RingMod must keep hosting **heterogeneous** Sources (osc A a sample/wavetable,
  osc B a saw, mixed or ring-modulated) — the engine composes Sources, it does not collapse the per-source choice.
- This arc **is** the front half of Phase 4; plan the engine cut and the voice-kernel cut as one effort
  (`synth_l1_engine_architecture.md` §"Voice / unison / Phase-4 relationship").

## 6. Pointers

- Overall plan + extensibility boundaries: `synth_untangling_plan.md`.
- Engine/Source contracts + migration sketch: `synth_l1_engine_architecture.md`.
- Voice decoupling state: `synth_phase4_voice_kernel.md`, `synth_voice_lift.md`.
- Typed source-owned params (step 3): `synth_source_ownership.md`.
