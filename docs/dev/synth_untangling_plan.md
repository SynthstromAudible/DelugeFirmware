# Synth Engine Untangling — Audit, Target Architecture, and Plan

Status: **architecture decided; Phase 0 (golden masters) + Phase 1 (`dsp-next` L0
library — full oscillator migration, wavetable storage port, sample/wavetable
interpolator unification) complete; Phase 2 in planning** (updated 2026-06-14).
Phase 1 detail lives in `dsp_next_component_architecture.md`; the original
Phase-2 framing ("kill the offsetof data plane") was substantially resolved by
PR #3366 and is re-scoped to a decoupling/extraction phase in
`synth_phase2_data_plane.md`. Phases 3–5 not yet started.

Goal: extract the synthesizer/oscillator/DSP machinery into a clean, modern,
portable, and *testable* library (`libdeluge-synth`), structured so that:

- foreign synthesis engines (Mutable Instruments Plaits, Rings, …) can be
  hosted as first-class peers of the native engines, and
- the instrument boundary is CLAP-shaped, enabling eventual loading of
  external `.clap` plugins on hardware (spark's bare-metal loader proves
  feasibility) and export of the Deluge synth itself as a desktop plugin.

This is a sibling effort to the libdeluge/BSP separation
([libdeluge_separation_plan.md](libdeluge_separation_plan.md)): that effort
cut the *platform* boundary below the app; this one cuts the *synthesis*
boundary inside it.

---

## Part 1 — Audit of the current architecture

### 1.1 The synth is not where it says it is

`src/deluge/dsp/` (~6k LOC) is **not** the synth engine — it is a collection
of mostly-clean leaf kernels: filters (SVF, ladders), reverb (header-only,
Mutable-derived `fx_engine.hpp`), delay, granular, compressor, the DX7 port,
and the oscillator wave renderers. The actual synthesizer lives outside it,
smeared across three god files:

| Component | Location | LOC | Responsibilities (all of them, simultaneously) |
|---|---|---|---|
| `Sound` | `processing/sound/sound.cpp` | ~5,000 | Preset state, XML serialization, UI mod-button/knob handling, arpeggiator hooks, global LFOs, voice lifecycle, FX chain, render orchestration |
| `Voice` | `model/voice/voice.cpp` | ~2,500 | Envelopes, local LFOs, MPE expression smoothing, patcher invocation, porta/pitch math, per-voice filters, unison, source dispatch, FM feedback paths, sample auto-release |
| `VoiceSample` + unison parts | `model/voice/` | ~2,100 | Sample streaming, time-stretch, cluster/cache interaction, interpolation |

`Sound` inherits from `ModControllableAudio` *and* `Voiced`. It is
simultaneously a preset, a DSP processor, a UI controller, and a file format.

### 1.2 Entanglement evidence

The include list of `voice.cpp` is the indictment — a per-voice DSP file
reaches into every layer of the application:

- `gui/waveform/waveform_renderer.h` (UI)
- `model/song/song.h`, `model/clip/instrument_clip.h` (song model)
- `playback/playback_handler.h` (sequencer)
- `storage/storage_manager.h`, `storage/cluster/cluster.h` (SD streaming)
- `storage/flash_storage.h` — **bend ranges are read from flash settings
  inside the render loop** (`voice.cpp:822`)
- `memory/general_memory_allocator.h` (global allocator, `checkStack` calls)
- `processing/engines/audio_engine.h` — a namespace of 30+ `extern` globals

Additional structural problems:

- File-scope `spareRenderingBuffer[4]` scratch shared across the render path.
- Every render call threads a `ModelStack`; you cannot instantiate one Voice
  in a test without a Song, ParamManager, and the AudioEngine globals.
- Scheduling policy (the `justCreated` note-on spreading hack, `cpuDireness`
  culling, render-skipping) is interleaved with synthesis code.

### 1.3 The patcher: crown jewel and biggest hazard

The per-voice modulation matrix (`modulation/patch/patcher.cpp`) is genuinely
good DSP architecture: q31 fixed-point, change-driven via `sourcesChanged`
bitmasks, separate linear/exp cable-combination rules, per-voice +
sound-global source split.

But it addresses parameters by raw index into `paramFinalValues` arrays whose
layout is shared between `Sound`, `Voice`, and `Patcher` by `offsetof`
convention. The canary: `sound.h:98-109` documents a 2024 release bug
(issue #2660) that changed the synth's sound and was "fixed" by aligning
`modKnobs` to 8 bytes — *without anyone understanding why*. Any architecture
in which a member-alignment change silently re-voices the instrument is
unmaintainable by definition. Killing this failure class is a primary goal.

### 1.4 Engine dispatch by megaswitch

Sources are dispatched on `enum class OscType` (`definitions_cxx.hpp:366`),
which conflates true oscillators (`SINE` … `WAVETABLE`) with `SAMPLE`,
`DX7`, and `INPUT_L/R/STEREO`. `Oscillator::renderOsc` is a 13-argument
static function switching on type. The DX7 integration shows what adding an
engine costs today: special cases scattered through Voice, Sound, the menu
system, and serialization. Adding Plaits this way would scatter another set.

### 1.5 What's already in our favour

- **Host-sim** compiles the entire application off-target (SIMDe), so
  "render audio on a CI box" already works.
- **`tests/dsp_diff`** proves ARM/NEON vs x86 bit-equivalence of the DSP
  primitives at -O0 and -O2.
- `dsp/` code is namespaced (`deluge::dsp`) and partially modernized; the
  reverb shows the target style (header-only, value types, no globals).
- Spark already contains a Rust port of the oscillators
  (`libs/dsp/deluge`, shipping as the `deluge_waves` CLAP instrument) and a
  working bare-metal CLAP loader **targeting this exact SoC**
  (`libs/sdk/clap/deluge-clap-loader`: relocates ARM32 `ET_DYN` ELF into
  SDRAM, fixes GOT/PLT, flushes caches, drives the CLAP lifecycle, `no_std`).

### 1.6 Reference architecture takeaways

**Plaits** (`~/GitHub/mutable-eurorack/plaits/dsp/engine/engine.h`):

```cpp
struct EngineParameters { int trigger; float note, timbre, morph, harmonics, accent; };

class Engine {
  virtual void Init(stmlib::BufferAllocator* allocator) = 0;
  virtual void Reset() = 0;
  virtual void Render(const EngineParameters&, float* out, float* aux,
                      size_t size, bool* already_enveloped) = 0;
  PostProcessingSettings post_processing_settings;  // out_gain, already_enveloped
};
```

Lessons: allocator injection instead of globals; a small normalized
macro-parameter surface; out+aux dual bus; engines declare envelope/gain
behaviour and the *voice* owns the envelope/LPG; an `EngineRegistry` makes
engines pluggable; every engine is unit-testable in isolation. Caveat: the
tiny parameter surface is a Plaits product decision — it maps to the Deluge's
*source/oscillator slot*, not to the whole voice. The Deluge's per-voice
patch-cable system is much richer and must live a layer above.

**Spark**: CLAP as the universal plugin boundary, with `ClapLibrary` /
`ClapLoader` traits unifying desktop dlopen and the embedded ELF loader.
CLAP gives us a stable C ABI (language-neutral — relevant to the Rust
strategic direction), float32 block processing, typed event queues, and
per-note-id polyphonic parameter modulation.

---

## Part 2 — Decisions (made 2026-06-12)

1. **Numerics: q31 core, float at the edges.** The L2 voice kernel and
   native engines stay q31 — that *is* the Deluge sound, and it keeps
   golden-master tests bit-exact against current firmware. Float engines
   (Plaits etc.) and CLAP adapt at the boundary (q31↔f32 is one NEON
   instruction per vector). Interfaces are designed numeric-agnostic so the
   engine infrastructure (envelopes, LFOs, patch cables) can *eventually*
   migrate to float32 as an implementation swap, not an interface change.
   Physically this lands as the `dsp-next` library's `core/` (numeric-agnostic
   interfaces + q31↔f32 conversion) over parallel `fixed/`
   (`libdeluge-dsp-fixed`, the bit-exact q31 core) and `float/`
   (`libdeluge-dsp-float`) backends — see Phase 1.

2. **CLAP at L3 only.** Internal engines use a lean C++ interface (per-voice
   granularity, zero ABI overhead, q31-capable). CLAP appears at instrument
   granularity: a host adapter loads external plugins as instruments, and a
   plugin adapter exports the Deluge synth as a `.clap`.

3. **C++23 in-tree, C ABI at L3.** Extract as portable static libraries
   (`deluge::dsp`, `deluge::synth`) inside this repo, refactoring in place
   under golden-master coverage. The L3 boundary is C-ABI (CLAP provides one
   for free) so a future Rust host (deluge-embassy / spark) drives it
   unchanged.

4. **Sample playback deferred behind a port.** First pass extracts
   oscillator/wavetable/FM/DX7 synthesis. `VoiceSample`/time-stretch/cluster
   streaming stays app-side, reached through a `SampleSource` port at L1, so
   it can migrate later without re-cutting the architecture.

---

## Part 3 — Target architecture

```
┌─ app (firmware) ──────────────────────────────────────────────────┐
│  Song / Clip / ParamManager / automation / serialization / menus  │
│  Arpeggiator, mod-FX chain, voice-culling & scheduling policy     │
└──────────────┬────────────────────────────────────────────────────┘
               │  L3: instrument boundary — C ABI, CLAP-shaped
               │  process(note/param/expression events, audio block)
┌─ libdeluge-synth (portable C++23, in-tree) ───────────────────────┐
│  L2 voice kernel: voice pool, patcher, envelopes, LFOs, filters,  │
│      unison, MPE smoothing — q31, numeric-agnostic interfaces     │
│  L1 source engines: VA, wavetable, FM, noise, ringmod, DX7 — q31  │
│      native; Plaits/Rings-style float engines via adapter;        │
│      SampleSource port (implementation stays app-side for now)    │
│  L0 deluge::dsp primitives: freestanding, no globals              │
└───────────────────────────────────────────────────────────────────┘
```

### Layer contracts

**L0 — DSP primitives.** Pure functions and POD-state value types:
oscillator kernels, filters, envelope/LFO cores, FX kernels, interpolators.
No globals, no allocator (caller provides buffers), no OS, no storage. This
is mostly what `dsp/` already is, after scrubbing.

**L1 — Source engines.** The Plaits lesson applied at the right altitude:
a `SourceEngine` renders one voice-block given pitch/phase plus a small block
of *already-modulated* parameter values. Per-voice state is engine-owned and
allocator-injected at init. Engines are q31-native; a float adapter wraps
float-world engines with per-block conversion. Native implementations: basic
waves (VA), wavetable, FM (modulators + feedback), noise, ring-mod pairing,
DX7 (re-homed properly). `SampleSource` is the port behind which the existing
sample path hides. An engine registry replaces the `OscType` megaswitch.

> **Refinement (see `synth_l1_engine_architecture.md`).** The swappable unit is
> drawn *above* the single-oscillator level: an **engine** is a complete
> per-voice timbre generator (Subtractive / RingMod / FM / DX7 / sample / input
> / future Plaits), and hard-sync / ring-mod / FM-topology are its *private
> composition*, not contract surface. The Phase-3 `*Engine` objects (VA,
> wavetable) are really *source components* one layer below. The registry
> dispatches engines, not oscillators.

**L2 — Voice kernel.** The heart of the untangling. A `synth::Voice` (voice
pool, patcher, envelopes, LFOs, filters, unison, expression smoothing) that
owns **no preset storage** and holds **no references upward**. Inputs per
block: a `PatchLayout` (which cables exist — changes rarely) and a
`ControlBlock` (source values, bend ranges, LFO configs — built by the app
side from ParamManager-world). This separates *patch + automation state*
(app) from *voice rendering state* (library), communicating via parameter
events at block boundaries — precisely CLAP's process model, which is why
the shape survives to L3.

**L3 — Instrument boundary.** Block-based C-ABI `process()`: event list in
(note on/off, param value/mod, expression, transport), audio out. Voice
allocation lives inside the instrument; the app keeps culling/steal *policy*
via explicit hooks. External `.clap` loading and Deluge-synth-as-plugin both
attach here.

### The vertical stack (composition tiers)

The L0–L3 *layers* above decompose vertically into six **composition tiers** —
each tier is built from the one below and is independently testable:

**Phasor → Osc → Source → Engine → Voice → Instrument**

| Tier | Owns / does | Layer | Today |
|---|---|---|---|
| **Phasor** | phase accumulation (UQ0.32) | L0 | `deluge::dsp::Phasor` |
| **Osc** | one waveform = phasor + shape; pure DSP, owns phase | L0 | the `va_shapes` variant *inside* `VirtualAnalogEngine` |
| **Source** | one voice source (osc / wavetable / sample / live-input / FM-op) + its per-source amplitude · sync-as-phase · pulse-width; reports a lifecycle outcome (`ACTIVE`/`UNASSIGN`/`SKIP`) | L1 | `synth::SourceEngine::renderBlock` **+** the `SourceRenderResult` dispatch (`renderSourceComponent`) — **to be folded into one `synth::Source`** |
| **Engine** | *coordinates* multiple Sources: hard-sync master/slave, ring-mod pairing, the 4-op FM matrix | L1 | the cross-source orchestration still inside `Voice::render` + `RingModEngine` (no first-class engine objects yet) |
| **Voice** | the per-voice chain *wrapping* one Engine: VCA + filter + envelopes + LFOs + unison + MPE smoothing | L2 | the rest of `Voice::render` → the `synth::Voice` lift |
| **Instrument** | the whole synth: + mod-FX chain, arpeggiator, preset/serialization (the Vital/Serum/Massive/Pigments altitude) | L3 | `Sound` + `ModControllableAudio` |

Key boundary: **an Engine _coordinates_ Sources; a Voice _wraps_ an Engine with
the VCA/filter/envelope chain.** That Source↔Engine↔Voice split is precisely the
L1/L2 cut the Phase-4 lift is making. "Engine" is reserved for the *coordinator*
tier — so the Phase-3 `synth::SourceEngine` (a single-source renderer) is
mis-named and becomes **`synth::Source`** (folding in the `SourceRenderResult`
outcome). See `synth_l1_engine_architecture.md` for the Source/Engine contracts.

### Extensibility model — two plug-in boundaries

Foreign / third-party code attaches at two different altitudes. They are
**complementary, not alternatives**: L3 is just the L2 voice kernel + L1
engines wrapped in a C-ABI, so building the stack once yields both, and they
*nest* rather than compete. The boundary you draw decides what the newcomer
inherits from the Deluge and what it must bring itself.

- **Engine API (L1) — "bring a source, inherit the Deluge."** A foreign source
  engine (a Plaits macro-osc, a physical model, a new wavetable engine) slots in
  where VA/FM/sample sources live and *still flows through* the Deluge's
  envelopes, LFOs, patch matrix, MPE/aftertouch-as-patch-sources, unison,
  filters and FX. This is the Deluge-native path: it makes a new timbre *feel
  like a Deluge instrument* and inherits the whole sound-design language. It is
  what the DX7 integration already is. The contract is narrow and opinionated
  (per-voice, q31-block, allocator-injected, hard-sync master/slave, the
  amplitude/VCA convention, pitch-as-phase-increment, `cpuDireness` fallback) —
  that opinionation is the cost of the inheritance. **The engine _registry_
  (replacing the `OscType` switch) is the substrate for this boundary:** a
  closed `switch` is fine for the built-in set, but a registry earns its keep
  the moment engines register at runtime.

- **Instrument API (L3) — "bring a whole synth, inherit the sequencer."** A
  whole CLAP instrument (Vital/Serum-class) drops in at the instrument boundary
  and the Deluge becomes its *host*: it gets the song / sequencer / automation
  and whatever params and note-expressions are bridged, but brings its **own**
  voices, modulation and filters — the Deluge mod matrix does not reach inside
  it. This maximises ecosystem compatibility (any `.clap`) at the price of the
  Deluge's voice/mod character. The built-in Deluge synth, expressed as the
  first such plugin, is the forcing function that proves the L2 kernel is
  decoupled.

**Two consequences that constrain the design:**

- **CLAP belongs at L3 only; L1 must stay a lean native C++ interface** (a
  C-ABI later, for Rust/foreign engines). The Deluge is per-voice
  (Voice / unison / pooling): an L1 engine is instantiated and pooled *per
  voice* and must be cheap and allocator-injected, whereas a CLAP instrument
  owns its own polyphony and is not designed to be spun up per voice.
- **The L1 engine API is gated on the L2 extraction (Phase 4).** Thin foreign
  integrations are possible now (DX7-style), but a real third-party engine API
  needs the surrounding voice processing (env / LFO / filter / patcher) to be a
  clean *host*, not tangled into `Voice` / `Sound`.

A third, orthogonal boundary — **CLAP _effects_ on the Sound-level FX chain** —
is deliberately out of scope here (see below); keep it distinct from both the
source-engine and whole-instrument plug points so the layering stays clean.

### Cross-cutting rules

- No globals below L3. Scratch buffers and allocators injected
  (Plaits `BufferAllocator` pattern). `spareRenderingBuffer` dies.
- q31 is an implementation detail, never a public type: boundaries use typed
  aliases (`Sample`, `ModValue`, `ControlValue`) so the float migration is
  contained.
- Patch-cable combination semantics (linear/exp curves, saturation) get
  documented as **behaviour contracts**, not left implicit in q31 bit tricks
  — the float reimplementation targets the contract, then re-baselines
  golden masters once.
- Virtual dispatch at block granularity only; never per-sample.
- Everything below L3 compiles and unit-tests on host with zero firmware
  scaffolding.

---

## Part 4 — The plan

Execution style follows the libdeluge pattern: one `feat/synth-*` branch per
phase, small bisectable commits, green golden-masters as the merge gate.

### Phase 0 — Lock the sound (golden masters) — **DONE**

Landed as `tests/golden/` (see its README): a corpus of **319** presets — 23
hand-written feature cases plus the sample-free synth presets imported from
the Deluge OLED V4.1.0 factory card (168, legacy tag-based XML) and the
Community Preset Pack 1.2 (128, modern attribute XML incl. DX7) — each
rendered through the full app in the host-sim (file-backed card image, real
`SYNTHS/` preset load at boot, scripted frame-keyed DIN MIDI, deterministic
offline render), with PCM SHA-256 fixtures cross-validated bit-exact between
the x86-SIMDe build and the arm-linux/NEON oracle (qemu cortex-a9). The two
preset formats also exercise the legacy-format loader and version-compat
transforms. **Every subsequent phase must stay bit-exact**; any deliberate
waiver is documented per-commit and re-validated against the ARM oracle.

The harness paid for itself immediately, surfacing several pre-existing bugs
— most of them live on hardware:

- an out-of-bounds sine-table read breaking *all* vectorised FM since the
  argon bump (caught precisely by the x86-vs-ARM fixture divergence), and the
  same argon-gather mistake in the pulse-wave renderer;
- an out-of-bounds `Sound::paramFinalValues` read in `Voice::noteOff` (the
  offsetof bug class, live);
- a one-past-the-end span index in `Sound::render` and an unresolved
  `PLACEHOLDER_RANGE` cable reaching the patcher (a `paramFinalValues` OOB
  write) — together crashing ~78 real factory/community presets in host-sim;
- an uninitialized-index read in the patched `f_mount`; a null `WaveTable*`
  deref in the ringmod path.

The six firmware-affecting fixes were cherry-picked onto `minor-fixes`
(builds green) for independent release ahead of the untangling itself. One
sim-only quirk also fell out: the DX7 NEON asm kernel (polynomial sine) and
the scalar LUT path are not bit-identical, so the sim builds force the scalar
path (`DELUGE_DX_SCALAR`) for x86≡ARM agreement. Hardware ear-checks on the
FM, env2-release, and DX7 changes remain pending. Sample/wavetable corpus
cases are deferred until audio files join the card images.

### Phase 1 — `dsp-next` (the clean L0 library)

Rather than scrub the accreted `dsp/` in place, stand up a **new, clean L0
library `dsp-next` and strangle the old one**: migrate primitives into it one
at a time as idiomatic C++23 (`std::span` buffers, injected scratch/quality/
allocator, strong numeric aliases, no globals), switch each caller under
golden-master cover, delete the old implementation, and remove `dsp/` once
empty. The library splits into `core/` (numeric-agnostic interfaces +
q31↔f32 conversion), `fixed/` (the bit-exact q31 core → `libdeluge-dsp-fixed`)
and `float/` (the float32 edge track → `libdeluge-dsp-float`, structurally
stood up now, populated later). Clean by construction: the include-lint gate
has no allowlist. Sample/granular/timestretch stay in old `dsp/` and relocate
to the Phase-3 `SampleSource` home, not into `dsp-next`.

**Detailed step-by-step plan: [synth_phase1_l0_plan.md](synth_phase1_l0_plan.md).**

### Phase 2 — Kill the offsetof data plane

Highest risk, highest value. Replace the raw-index/`offsetof` parameter
plumbing shared between Sound/Voice/Patcher with an explicit `VoiceParams`
POD plus compile-time parameter metadata (eliminates the issue-#2660
alignment-bug class). Extract `Patcher`, `Envelope`, `LFO` into
`deluge::synth`, severing `Sound&` back-references; render inputs become the
per-block `ControlBlock`.

### Phase 3 — L1 engine interface

Define `SourceEngine` (init-with-allocator / render-block). Re-home the
`OscType` megaswitch bodies as engines; finish the DX7 integration properly;
`SAMPLE`/`INPUT_*` leave the enum, with sample playback behind the
`SampleSource` port wrapping the existing `voice_sample` path in place.

*Status:* the **extraction** is done — every `OscType` render body now lives in
an engine (VA, wavetable, DX7, input, sample), and `renderBasicSource` is pure
dispatch. The remaining **unification** — a uniform engine interface across the
foreign engines, `SAMPLE`/`INPUT_*` leaving the enum, and the registry
replacing the `switch` — is the substrate for the **Engine API (L1)** plug-in
boundary (see *Extensibility model* above). A closed `switch` is adequate until
that boundary is committed to, so this unification is worth doing precisely
when runtime-pluggable engines become a goal, not before.

### Phase 4 — Assemble the L2 voice kernel

New `synth::Voice` + voice pool composed entirely of Phase 1–3 parts,
constructible in a unit test with zero Song/ModelStack/AudioEngine. Old
`Voice`/`Sound::render` shrink to an adapter translating ParamManager-world
into ControlBlocks. Scheduling policies (`justCreated` spread, `cpuDireness`
culling, render-skipping) stay app-side behind explicit hooks.

> **Roadmap: `synth_phase4_voice_kernel.md`.** Approach: **in-place decouple,
> lift to `synth::Voice` last** — sever `Voice::render`'s remaining `Sound&` /
> `ModelStack` / `ParamManager` reads one bit-exact slice at a time (route
> config through an expanded `ControlBlock`/`VoiceConfig` + precomputed flags),
> then lift the upward-ref-free render into the kernel as a mechanical final
> move. That doc has the coupling inventory + sub-slice sequence.

### Phase 5 — L3 boundary + first foreign engine

Define the CLAP-shaped C-ABI `process()`; move voice allocation inside the
instrument. Port one Plaits engine through the float adapter as proof the
architecture earns its keep. External `.clap` hosting via the spark
`deluge-clap-loader` approach becomes its own follow-on project — the
boundary will be ready for it.

### Explicitly out of scope (for this effort)

- The Sound-level FX chain (`ModControllableAudio`) — a second, separate
  untangling.
- The arpeggiator — note-event domain; could later become an L3 event
  processor.
- The float32 core migration — *enabled* by the interface rules above,
  executed later.

### Risks

- **CPU headroom** on the Cortex-A9: the current code is hand-tuned
  integer NEON; indirection must stay at block granularity, and the golden
  masters double as performance canaries (run under host-sim timing and
  spot-check on hardware per phase).
- **Arithmetic-reorder bit-drift** during extraction: moving q31 operations
  changes rounding. Golden masters catch it immediately; waivers documented.
- **Voice-stealing semantics** moving inside the instrument (Phase 5) — the
  app's culling policy hooks must be designed before voice allocation moves.
