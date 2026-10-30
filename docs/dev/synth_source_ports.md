# Source ports — separating the sources from the App (dependency inversion)

> Status: **design / north star.** The Source-layer half of "completely separate sources / engines / instrument
> from the App" (a portable `libdeluge-synth`). Prompted by the realisation that L-vi gave us the *dispatch* seam
> (the voice kernel talks to sources through one `synth::Source`) but **not** separation — 4 of 5 source impls
> lived in App layers and reached up into `AudioEngine`/storage. This doc defines how **ports** invert that
> dependency so each source becomes App-free and movable into the library, at which point the kernel owns it.
> Companion to `synth_voice_lift.md` (the L2 kernel lift) and `synth_l1_engine_architecture.md` (the L1 tiers).

## Where we are

L-vi unified all five sources behind `synth::Source::renderSource(ctx)` — the `OscType` megaswitch is gone and the
kernel no longer names concrete source types. **All five sources** now physically live in `synth/source/`
(library-resident): `VirtualAnalogSource` (pure DSP), `WavetableSource` (port #1 — band-data view), `DX7Source`
(port #2 — the DX voice-pool port, its pool now of `dx7::Voice`), `InputSource` (port #3 — live input) and
`SampleSource` (port #4 — sample playback). Only the port adapters stay App-side:

| Source | Home today | App-coupling |
|---|---|---|
| `VirtualAnalogSource` | `synth/source/` | none — pure DSP |
| `WavetableSource` | `synth/source/` | none — depends on the `fixed::WaveTableData` band-data view (port #1 ✅) |
| `DX7Source` | `synth/source/` | none — depends on `synth::DxVoicePort`; the `getDxEngine()` pool (of `dsp-next`'s `dx7::Voice`) + `DxPatch` live behind `dsp::DxVoiceAdapter` in `processing/dx7/` (port #2 ✅) |
| `InputSource` | `synth/source/` | none — depends on `synth::AudioInputPort`; `AudioInputAdapter` (`processing/live/`) exposes `AudioEngine`'s input ring, frame clock, plug state and the `LiveInputBuffer` pool. The live-input DSP (`LiveInputBuffer`, `LivePitchShifter`) is library-resident under `synth/source/live/` (port #3 ✅) |
| `SampleSource` | `synth/source/` | none — depends on `synth::SamplePlaybackPort`; `SamplePlaybackAdapter` (`model/voice/`) holds the pooled `VoiceSample` reader and keeps storage, the cluster cache, time-stretch, the pool and `playbackHandler` (port #4 ✅) |

"Swappable behind an interface" (done) is **not** "separated from the App" (the source impl is still glued to the
firmware). The second, harder half is the dependency inversion below.

## The principle: thin ports, app-side adapters

A **port** is an interface the **library** declares and a source depends on; the **firmware** supplies the concrete
**adapter** (and a host/test can supply a fake). The dependency arrow flips:

```
today:   SampleSource ──reaches up──▶ AudioEngine / VoiceSample / storage      (source glued to the App)
ported:  SampleSource ──depends on──▶ SamplePlaybackPort ◀──implements── firmware adapter (wraps VoiceSample)
            (in synth/)                  (in synth/)                            (App-side, swappable; tests fake it)
```

**Scoping rule that keeps this tractable — ports are THIN; the heavy machinery stays App-side as the adapter.** We
do **not** relocate `VoiceSample` / `LivePitchShifter` / the DX `dx7::Voice` pool / the SD cluster-cache into the
library. We wrap each behind a narrow interface. What moves into the library is the **per-voice source object**
(its DSP, its dispatch, its lifecycle shape); what stays App-side is the **adapter** doing the actual
hardware/storage/streaming. This is ports-and-adapters exactly: the source is the hexagon's inside, the firmware is
the outside.

## The five ports (and the honest cost gradient)

| Source | Port (thin interface in `synth/`) | Adapter (stays App-side) | Cost |
|---|---|---|---|
| `VirtualAnalogSource` | — (already library-resident) | — | **done** |
| `WavetableSource` | `fixed::WaveTableData` band-data view — a read-only span over the wavetable's bands | the `WaveTable` storage object (`view()`) | **done** (port #1) |
| `DX7Source` | `synth::DxVoicePort` — solicit/release + the `DxVoice` ops, injected instead of the `getDxEngine()` global (the patch is an opaque `void*`) | `dsp::DxVoiceAdapter` wrapping the MSFA `DxVoice` pool | **done** (port #2) |
| `InputSource` | `synth::AudioInputPort` — sample clock, stereo availability, ring read window, per-channel `LiveInputBuffer`, hop reporting | `AudioInputAdapter` over `AudioEngine`'s SSI ring + `LiveInputBuffer` pool | **done** (port #3) |
| `SampleSource` | `synth::SamplePlaybackPort` — render / cache-decision / play-position queries and ops over one pooled reader | `SamplePlaybackAdapter` wrapping `VoiceSample` + clusters + cache + timestretch + the pool + `playbackHandler`; it also owns the note lifecycle (solicit/release, zone change, loop-end note-off) off the port | **done** (port #4) |

VA proves the end state; **wavetable is the cheap first win** that validates the whole port pattern end-to-end
(define interface → make the source depend on it → relocate the source into `synth/` → golden-bit-exact).

## Ownership falls out of the ports

Once a source depends only on its port, it's App-free, so **the kernel owns it** (the "ownership rework" is a
*consequence*, not a separate mechanism). Per unison-part-source the kernel holds the source object (a library
type) + its render state (`oscPos`, `phaseIncrementStoredValue`, `carrierFeedback`, `active`); the firmware injects
the port adapter at note-on (the adapter domain). `VirtualAnalogSource` already works this way; each ported source
joins it, and the kernel becomes relocatable to `synth::Voice` (the prior `synth_voice_lift.md` L-viii) because it
no longer holds any App-layer type.

**Osc-sync** stays abstract via a tiny `synth::Source` facet — `hasOscPhase()` / `oscPhase()` / `seekOscPhase()`
(VA + wavetable implement; the rest false) with the `oscPos` fallback — so the kernel reads the master's phase
without naming a concrete source.

**Stays App-side (the adapter / firmware):** the port adapters themselves, note-on engine *selection*, the
unison-count clone (`sound.cpp`), the GUI play-cursor reads, voice scheduling/culling, transport, storage.

## Interaction with the Engine tier

When the coordinator Engines (Subtractive / RingMod / FM) are themselves moved out of the App, they interact with
this design as follows — and the headline is: **Engines need no ports of their own.**

- **Engines couple to the *kernel*, not the *App*.** `RingModEngine`/`SubtractiveEngine`/`FMEngine` reach only
  voice-kernel state (the param plane, scratch buffers, unison, osc-sync, `carrierFeedback`) — never
  `AudioEngine`/storage/the allocator. Their job is pure cross-source *coordination DSP* (A→B hard-sync, the
  ring-mod multiply, the FM operator graph + feedback). So moving an Engine out is the **same kind of move as the
  L2 kernel relocation** — pure DSP over per-block inputs — and needs **zero ports**. Ports are strictly a
  Source-layer concern (sources are the only things doing hardware/storage IO).
- **The nesting:** `Instrument (L3) → owns Voices (L2) → own Engines → own Sources → depend on Ports ◀ firmware
  adapters`. The port adapters are injected from the firmware **down through** Voice→Engine→Source.
- **Ownership of the sources migrates Voice → Engine.** Today the Voice owns the sources and plays the
  coordinator through `Voice&` (`SubtractiveEngine` drives the public `renderBasicSource` for the unison loop + A→B
  sync; `FMEngine` calls back into `Voice` for `adjustPitch`). `Voice` has no `friend`s.
  The end state (per `synth_l1_engine_architecture.md`) is that **each Engine owns its sources as private
  composition** (Subtractive its 2 slots, RingMod its pair, FM its carriers/modulators). So "the kernel owns the
  ported sources" above is the **interim** truth; the eventual owner is the Engine. **Ports are orthogonal to this**
  — a port abstracts a source's IO regardless of whether the Voice (now) or an Engine (later) owns the source.
- **The source seams here *are* the Engine↔Source seams.** The `hasOscPhase`/`oscPhase`/`seekOscPhase` facet is
  exactly what a `SubtractiveEngine`/`RingModEngine` uses for its *internal* A→B sync; and `SourceRenderContext` is
  the Engine↔Source contract — extracting an Engine means lifting the ctx-building + sync coordination (today smeared
  in `renderBasicSource`) *into* the Engine object. So this work is forward-compatible with the engine extraction.
- **Ordering is flexible** (`synth::Source` decouples them): an Engine can be extracted to coordinate
  `synth::Source*` whether or not the concrete impls are ported, and a source can be ported under either owner.
  Natural order: **source ports first** (the genuinely hard IO separation), then the **engine extraction** (pure
  DSP lift out of `Voice`, ending the engines' `Voice&` coupling) — converging when library Engines own library Sources
  that depend on ports backed by firmware adapters. The engine extraction is its own arc
  (`synth_l1_engine_architecture.md`), not part of these source ports.

## Sequence (each port is its own multi-slice, golden-gated arc)

1. **Wavetable band-data view — ✅ DONE (2026-06-18, commits `bc13012e`/`6a810c78`/`eb78dec5`).** `WavetableSource`
   now depends on a `fixed::WaveTableData` view (not `WaveTable*`), renders through the dsp-next view-based
   oscillators, and lives in `synth/source/` (boundary-clean). The dead app-side `WaveTableOscillator` +
   `WaveTable::render` were retired; `WaveTable` keeps loading + `view()` only. **2 of 5 sources now library-
   resident (VA + wavetable).** (Validates the port pattern end-to-end.)
2. **DX7 pool port — ✅ DONE (2026-06-18, commits `1f3aa383`/`55beb042`).** `DX7Source` now depends on
   `synth::DxVoicePort` (the patch passed as an opaque `void*`, never dereferenced in the library); the
   `getDxEngine()` pool + the `DxVoice`/`DxPatch` ops + the per-block log-pitch/ctrl/`compute` live behind
   `dsp::DxVoiceAdapter` (App-side, per-source). `DX7Source` kept only its bespoke per-unison amplitude/stereo sum
   and relocated into `synth/source/` (boundary-clean — the amplitude sum's include was narrowed from the fat
   `util/functions.h` to `util/fixedpoint.h`). **3 of 5 sources now library-resident (VA + wavetable + DX7).**
3. **Live-input port — ✅ DONE (commits `a287fd00f`/`6fb5d3f1a`/`340133b1e`; namespace moves
   `e3548ddc6`/`eaf4df0ae`).** `InputSource` depends on `synth::AudioInputPort` and lives in `synth/source/`; `LiveInputBuffer` and the live pitch shifter moved to
   `synth/source/live/`. `AudioInputAdapter` (`processing/live/`) is the App-side adapter.
4. **Sample-playback port — ✅ DONE (commits `637c1d121`/`e948fe3c5`; namespace move `e3548ddc6`).**
   `SampleSource` depends on `synth::SamplePlaybackPort` and lives in `synth/source/`. `SamplePlaybackAdapter` (`model/voice/`) wraps
   `VoiceSample`. **5 of 5 sources now library-resident.**
5. **Kernel relocation** (`Voice` → `synth::Voice`) — not started. The sources no longer block it; the remaining
   blockers are the voice's own App reaches, listed in `synth_voice_lift.md`. One of them is here: the voice's
   unison-part source (`voice_unison_part_source.h`) still holds the three port adapters by value, so the kernel
   still names App types. The fix is to inject the ports.

Per port: interface in `synth/` → source depends on it (bit-exact, impl unchanged behind the adapter) → relocate
the App-free source into `synth/` (each step golden-bit-exact + `check_synth_boundary`-gated).

## Out of scope (separate later arcs)

The **Engine tier** (Subtractive / RingMod / FM coordinators — still `Voice&`-coupled)
and the **Instrument boundary (L3, CLAP-shaped)** are *not* this doc — this is the Source layer's separation only.
**Param ownership** (typed per-source param structs vs the flat plane) remains the separate OO phase
(`synth_source_ownership.md`). Numerics stay q31; port types stay numeric-agnostic.

## Pointers

- The kernel lift + L-viii: `synth_voice_lift.md`. The L1 tiers + naming: `synth_l1_engine_architecture.md`.
- Param ownership (later): `synth_source_ownership.md`. Overall plan: `synth_untangling_plan.md`.
- The first port in flight: `[[wavetable-storage-port]]`.
