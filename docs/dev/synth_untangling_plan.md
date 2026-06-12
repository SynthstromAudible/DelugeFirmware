# Synth Engine Untangling — Audit, Target Architecture, and Plan

Status: **architecture decided, plan drafted** (2026-06-12). No code changes yet.

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

Landed as `tests/golden/` (see its README): 22 corpus presets rendered
through the full app in the host-sim — file-backed card image, real
`SYNTHS/` preset load at boot, scripted frame-keyed DIN MIDI, deterministic
offline render — with PCM SHA-256 fixtures cross-validated bit-exact between
the x86-SIMDe build and the arm-linux/NEON oracle (qemu cortex-a9). **Every
subsequent phase must stay bit-exact**; any deliberate waiver is documented
per-commit and re-validated against the ARM oracle.

The harness paid for itself immediately, finding four pre-existing bugs:
an uninitialized-index read in the patched `f_mount`; a null `WaveTable*`
deref in the ringmod path; an out-of-bounds `Sound::paramFinalValues` read
in `Voice::noteOff` (the offsetof bug class, live); and an out-of-bounds
sine-table read breaking all vectorised FM since the argon bump (caught
precisely by the x86-vs-ARM fixture divergence). Sample/wavetable corpus
cases are deferred until audio files join the card images.

### Phase 1 — Make L0 true

Scrub `dsp/`'s upward reaches: invert the `oscillator.h → storage/wave_table`
dependency (`WaveTableView` POD in dsp; storage owns loading); absorb
`processing/render_wave.h` and `vector_rendering_function.h`; replace
`spareRenderingBuffer` and FlashStorage reads with injected scratch/config.
Grow per-primitive unit tests out of dsp_diff.

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

### Phase 4 — Assemble the L2 voice kernel

New `synth::Voice` + voice pool composed entirely of Phase 1–3 parts,
constructible in a unit test with zero Song/ModelStack/AudioEngine. Old
`Voice`/`Sound::render` shrink to an adapter translating ParamManager-world
into ControlBlocks. Scheduling policies (`justCreated` spread, `cpuDireness`
culling, render-skipping) stay app-side behind explicit hooks.

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
