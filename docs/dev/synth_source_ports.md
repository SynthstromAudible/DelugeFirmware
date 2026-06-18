# Source ports — separating the sources from the App (dependency inversion)

> Status: **design / north star.** The Source-layer half of "completely separate sources / engines / instrument
> from the App" (a portable `libdeluge-synth`). Prompted by the realisation that L-vi gave us the *dispatch* seam
> (the voice kernel talks to sources through one `synth::Source`) but **not** separation — 4 of 5 source impls
> still live in App layers and reach up into `AudioEngine`/storage. This doc defines how **ports** invert that
> dependency so each source becomes App-free and movable into the library, at which point the kernel owns it.
> Companion to `synth_voice_lift.md` (the L2 kernel lift) and `synth_l1_engine_architecture.md` (the L1 tiers).

## Where we are

L-vi unified all five sources behind `synth::Source::renderSource(ctx)` — the `OscType` megaswitch is gone and the
kernel no longer names concrete source types. But only **`VirtualAnalogSource`** physically lives in `synth/`; it's
pure DSP and is the proof a source *can* be library-resident + kernel-owned. The other four still live App-side and
reach upward:

| Source | Home today | App-coupling |
|---|---|---|
| `VirtualAnalogSource` | `synth/source/` | none — pure DSP |
| `WavetableSource` | `dsp/oscillators/` | reads `WaveTable*` band data (storage) |
| `DX7Source` | `dsp/dx/` | `getDxEngine()` pool + `DxVoice`/`DxPatch` (all within `dsp/dx`) |
| `InputSource` | `processing/live/` | `AudioEngine` mic/line + SSI input ring + `getOrCreateLiveInputBuffer` + `LivePitchShifter` alloc |
| `SampleSource` | `model/voice/` | `AudioEngine` voice-sample pool + the whole `VoiceSample` reader + storage/cache/clusters + `playbackHandler` |

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
do **not** relocate `VoiceSample` / `LivePitchShifter` / the DX `DxVoice` pool / the SD cluster-cache into the
library. We wrap each behind a narrow interface. What moves into the library is the **per-voice source object**
(its DSP, its dispatch, its lifecycle shape); what stays App-side is the **adapter** doing the actual
hardware/storage/streaming. This is ports-and-adapters exactly: the source is the hexagon's inside, the firmware is
the outside.

## The five ports (and the honest cost gradient)

| Source | Port (thin interface in `synth/`) | Adapter (stays App-side) | Cost |
|---|---|---|---|
| `VirtualAnalogSource` | — (already library-resident) | — | **done** |
| `WavetableSource` | **band-data view** — a read-only span over the wavetable's bands | the `WaveTable` storage object | **low** — render is already pure; just the data view |
| `DX7Source` | **DxVoice-pool** — solicit/release + the `DxVoice` ops, injected instead of the `getDxEngine()` global | the MSFA `DxVoice` pool | **moderate** — the FM math is self-contained |
| `InputSource` | **live-input** — channel state (mic/line) + ring read + a "buffered/pitch-shifted block" op | `LivePitchShifter` + `LiveInputBuffer` + the SSI ring | **heavy** |
| `SampleSource` | **sample-playback** — solicit/release a voice-sample + setup / late-start / cache-decision / render / zone / loop-end ops | `VoiceSample` + clusters + cache + timestretch + the pool + `playbackHandler` | **very heavy** — the widest coupling in the synth; do last |

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

## Sequence (each port is its own multi-slice, golden-gated arc)

1. **Wavetable band-data view** — resumes `[[wavetable-storage-port]]`; `WavetableSource` → `synth/`.
2. **DX7 pool port** — inject the `DxVoice` pool; relocate `DX7Source`; decide `DxPatch`'s home.
3. **Live-input port** — `InputSource` → `synth/`; `LivePitchShifter`/`LiveInputBuffer` become the adapter.
4. **Sample-playback port** — wrap `VoiceSample`; `SampleSource` → `synth/`. The largest arc.
5. **Kernel relocation** (`Voice` → `synth::Voice`) — now unblocked; owns all five library-resident sources + L-v
   scratch + L-vii noteOff.

Per port: interface in `synth/` → source depends on it (bit-exact, impl unchanged behind the adapter) → relocate
the App-free source into `synth/` (each step golden-bit-exact + `check_synth_boundary`-gated).

## Out of scope (separate later arcs)

The **Engine tier** (Subtractive / RingMod / FM coordinators — still `Voice&`-coupled, FM a `friend` of `Voice`)
and the **Instrument boundary (L3, CLAP-shaped)** are *not* this doc — this is the Source layer's separation only.
**Param ownership** (typed per-source param structs vs the flat plane) remains the separate OO phase
(`synth_source_ownership.md`). Numerics stay q31; port types stay numeric-agnostic.

## Pointers

- The kernel lift + L-viii: `synth_voice_lift.md`. The L1 tiers + naming: `synth_l1_engine_architecture.md`.
- Param ownership (later): `synth_source_ownership.md`. Overall plan: `synth_untangling_plan.md`.
- The first port in flight: `[[wavetable-storage-port]]`.
