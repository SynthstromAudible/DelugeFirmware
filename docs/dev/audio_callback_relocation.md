# Audio-engine SSI/DMA relocation — the `deluge_app_render` audio boundary

**Status:** planned, not started. The deepest remaining HAL seam in the
application, and the one that most shapes the eventual host-sim and Embassy BSPs.
Suggested branch `feat/libdeluge-audio-io`. This is the change that finally takes
`audio_engine.cpp` off `scripts/platform_boundary_allowlist.txt` (after which only
`deluge.cpp` and the deferred `hid/encoder*.cpp` remain). It touches the realtime
audio path end-to-end, so **the result must be validated on hardware — listening +
scope, under CPU load, with input monitoring and recording — before merge.** A
build alone cannot make this safe; the host-sim WAV-diff harness (Phase 0) is the
mechanical regression guard that makes it *tractable*.

## Goal

Adopt a single audio boundary — the push callback `deluge_app_render(in, out,
frames)` — that is natural for all three BSP targets (RZ/A1L, host-sim, Embassy),
and move the SSI/DMA ring, the buffer geometry, and the **DMA-pacing policy** out
of `audio_engine.cpp` and into the RZ/A1L BSP. The application keeps DSP and the
*musical* timing (sequencer-event alignment, envelope/LFO granularity); the board
keeps everything about *how the samples reach the DAC*.

## The core insight: the "variable window" is two separable concerns

`audio_engine.cpp` today conflates two things that this plan deliberately splits:

1. **Sub-block accuracy** — envelope/LFO stages recalc per render window, and
   windows are shortened to end exactly on the next sequencer event. This is the
   *audible* semantics (min attack/decay/release ≈ window length; jitter-free
   sequencing). It is **internal to the renderer** and survives *any* boundary:
   given a block of N frames, the app sub-divides — render up to the next event,
   recalc, continue. The machinery already exists (`tickSongFinalizeWindows`).

2. **Load-adaptive sizing** — polling the DMA play head and rendering "as far as it
   has advanced," doubled under light load, capped at 128, rounded to 4. This is a
   **CPU-regulation heuristic that is a property of the RZ/A1L's free-running
   polled-DMA architecture**, not of the synth engine. It belongs in the RZ/A1L
   BSP, as the policy that *chooses `frames` and when to call the app*.

Splitting them dissolves the apparent conflict between "keep the variable window"
and "adopt a clean push callback." The variable window stays **fully alive on
RZ/A1L** — the BSP simply calls `deluge_app_render` with a *variable* `frames`.

## Target architecture

```
                 ┌──────────────────────────────────────────────┐
   app  ◄──────  │  deluge_app_render(in, out, frames)           │
 (synth)         │   renders EXACTLY `frames`, sub-dividing       │
                 │   internally at sequencer events / for         │
                 │   envelope granularity. Synchronous,           │
                 │   non-blocking (sample data is pre-loaded).    │
                 └──────────────────────────────────────────────┘
                        ▲                ▲                 ▲
            RZ/A1L BSP  │      host BSP  │      Embassy BSP │
   polls DMA head,      │   OS audio cb  │   await DMA ping-│
   applies direness/    │   hands fixed  │   pong half-done,│
   cap/round-to-4,      │   N (64/128…)  │   render half-buf│
   calls render(VAR N)  │                │                  │
```

One boundary, three drivers:

- **RZ/A1L BSP** keeps the exact DMA-polling + direness-doubling + round-to-4 + cap
  logic (relocated *down* from `audio_engine.cpp`) and calls `deluge_app_render`
  with that variable count → **bit-identical on hardware**.
- **Host BSP** wires the OS audio callback (PortAudio / miniaudio) straight to
  `deluge_app_render`. Enables CI: scripted input/sequence → captured `out` →
  golden-WAV diff.
- **Embassy BSP** awaits a ping-pong DMA half-done interrupt and renders the freed
  half. `deluge_app_render` stays synchronous; this is *why* the storage-yield work
  mattered — sample clusters are pre-loaded by the (now yield-based) SD routine, so
  render never blocks.

### Boundary headers

Refine the existing drafts:

- `<libdeluge/app.h>`: `deluge_app_render(const DelugeStereoSample* in,
  DelugeStereoSample* out, uint32_t frames)` — **the** audio entry point. Renders
  exactly `frames`; `in`/`out` are board-owned slices (may be NULL if the board has
  no input). Synchronous, realtime, non-blocking. `[audio]`
- `<libdeluge/audio_io.h>`: `deluge_audio_start/stop`, `deluge_audio_sample_rate`,
  `deluge_audio_max_block_frames` (the app sizes scratch from this — on RZ/A1L =
  128), `deluge_audio_output_latency_frames` (for aligning gate/MIDI output).
- **Play-position for gate timing:** the app no longer reads the raw DMA pointer.
  Within a render it knows each event's frame offset in the block; it arms the
  existing `deluge_midi_gate_timer_arm(samples_from_now)` (already landed) with that
  offset. The BSP, which drives the block, knows the block's absolute start time —
  so sample-accurate gate/MIDI output is *cleaner* than today, not harder.

## What moves where

| Concern | Today (`audio_engine.cpp`) | Target |
|---|---|---|
| DMA-position polling, direness doubling, round-to-4, window cap | `routine_()` (548–588) | **RZ/A1L BSP** render-driver |
| Sub-dividing at sequencer events; envelope/LFO per sub-window | `tickSongFinalizeWindows` + render loop | **app**, inside `deluge_app_render` |
| Volume + dither + monitoring + recording | `doSomeOutputting()` (1057+) | **app**, writing the `out` block |
| TX/RX rings, wraparound, uncached mirror, RX latency window | direct `getTx/RxBuffer*` + `SSI_*` math | **BSP**, hands the app `in`/`out` slices |
| Gate/MIDI sample-accurate timing | raw DMA pos read (548, 954) | **app** event-offset → `deluge_midi_gate_timer_arm` |
| `audioSampleTimer`, `numSamplesLastTime` bookkeeping | `routine_()` | **app** (driven by `frames`) |

## Coupling inventory (`audio_engine.cpp`) — what the includes are for

| Symbol / header | Sites | Disposition |
|---|---|---|
| `getTxBufferCurrentPlace()` | 548, 954, 1076 | → BSP pacing-driver (548), play-position query (954), gone (1076, internal to BSP copy) |
| `getTxBufferStart/End()` | 223, 1135–1136 | → BSP (ring owned there) |
| `getRxBufferCurrentPlace/Start/End()` | 1117, 1149, 1172, 1191, 1468, 1476 | → BSP (RX ring + latency window) |
| `SSI_TX_BUFFER_NUM_SAMPLES` (19×), `SSI_RX_BUFFER_NUM_SAMPLES` (7×) | many | ring math → BSP; scratch sizing → `deluge_audio_max_block_frames` |
| `clearTxBuffer()` | init | → `deluge_audio_start` |
| `i2sTXBufferPos` / `i2sRXBufferPos` | many | BSP-internal ring cursors |
| `RZA1/cpu_specific.h` | — | `NUM_MONO_*_CHANNELS_MAGNITUDE`, `UNCACHED_MIRROR_OFFSET`, SSI sizes, DMA channels → BSP / `board_layout.hpp` |
| `RZA1/intc/devdrv_intc.h` | 85–93 | `disableInterrupts[]` — **separable**; port independently (a `deluge_system_*` critical helper), don't let it expand audio scope |
| `drivers/ssi/ssi.h` | 78 | gone once the above move |

Preserve the **uncached mirror** (`getTxBufferStart()` = `&ssiTxBuffer[0] +
UNCACHED_MIRROR_OFFSET`, =0x40000000) exactly — it gives DMA coherency with no cache
maintenance. Keep that access *inside* the BSP, or hand the app an uncached
`out`/`in` pointer. A normal cached pointer here causes intermittent DMA corruption.

## Design decisions

- **D1 — `deluge_app_render` fills exactly `frames` (recommended).** The app loops
  internally for sequencer events / envelope granularity. The variable window lives
  on as the *value of `frames`* (variable on RZ/A1L, fixed on host/Embassy), not as
  a boundary protocol. Alternative considered — `render(out, max) -> rendered` so
  the app can stop at events at the boundary — rejected: it leaks RZ/A1L's polling
  quirk into the contract and forces host/Embassy to loop to fill their buffer.

- **D2 — Pacing policy lives in the RZ/A1L BSP.** Direness doubling, the `>10*
  numRoutines` floor, round-to-4, and the 128 cap move into the BSP render-driver.
  This is board policy. Reproduce it verbatim for the bit-identical landing.

- **D3 — Render is synchronous and non-blocking.** No `await`, no SD I/O inside
  `deluge_app_render`. Sample data is pre-loaded by the existing cluster routine.
  This is the contract that makes the Embassy BSP possible. Any current in-render SD
  touch must be hoisted out (verify `doSomeOutputting`/recording paths).

- **D4 — Volume/dither/monitoring/recording stay app-side**, writing the `out`
  block the BSP provides. Dither is mandatory (the codec mutes on sustained
  identical zeros). Zero-copy: the app renders into the BSP's `out` slice directly.

- **D5 — RX latency window → BSP.** The TX/RX alignment (`1463–1483`) is ring
  geometry; the BSP owns it and hands the app an `in` slice already aligned to the
  `out` block. Recording/monitoring consume `in`.

- **D6 — Untangle `routine()`'s interleaving.** Today `routine()` interleaves
  encoder reads + `doSomeOutputting` *between* `routine_()` calls. Under the always-on
  task manager those are separate tasks already; confirm the interleave is legacy and
  remove it so `deluge_app_render` is pure rendering. This is the subtle behavioral
  area — review carefully.

## Phases (each build-green; whole thing audio-tested before merge)

0. **Host-sim audio harness + golden WAV.** Stand up a minimal host build that
   links the app against a stub BSP whose `deluge_app_render` driver renders a
   scripted song/input to a WAV. Capture a golden reference from *current* firmware
   behavior (or the bit-identical Phase 1 result). This is the mechanical regression
   guard for every later phase — build it first.
1. **Introduce `deluge_app_render` + the RZ/A1L pacing-driver, bit-identical.**
   New `src/bsp/rza1/audio_io.c` holds the relocated pacing loop (today's
   `routine_` DMA-polling/direness/round-to-4) and calls a new `deluge_app_render`
   that runs today's exact render path (`renderAudio` body). `AudioEngine::routine`
   becomes a thin call into the BSP driver. Target: byte-identical firmware behavior,
   verified by the Phase-0 harness + on-HW A/B.
2. **TX output under the boundary.** `out` slice handed to the app; move ring
   wraparound + uncached-mirror writes into the BSP. Drop `getTxBuffer*` + TX `SSI_*`
   math from the app.
3. **RX input + latency window under the boundary** (D5). Drop `getRxBuffer*` + RX
   `SSI_*` math.
4. **Gate/MIDI timing** (D-play-position). Replace the raw DMA-pos reads (548, 954)
   with event-offset arming via `deluge_midi_gate_timer_arm`; reconcile
   `scheduleMidiGateOutISR`.
5. **Relocate board constants** (`NUM_MONO_*_CHANNELS_MAGNITUDE`, SSI sizes) to
   `board_layout.hpp`/boundary; port `disableInterrupts[]`/`devdrv_intc` independently
   (D-separable).
6. **Drop the includes** (`ssi.h`, `cpu_specific.h`, `devdrv_intc.h`); remove
   `audio_engine.cpp` from the allowlist → **lint 3**.

## Hardware / audio test matrix (before merge)

- **Output quality under load:** dense songs — no dropouts/glitches/zipper vs current
  firmware (A/B with the Phase-1 bit-identical build).
- **Dither:** sustained silence doesn't mute/glitch the codec.
- **Envelopes/LFO across CPU load:** light load (tiny windows) vs heavy (128) —
  attack/decay unchanged (this is the `frames`-variable property; exercise it).
- **Sequencer timing:** tight quantized sequences — no added jitter.
- **Input monitoring + resampling/recording:** line-in monitor + sample recording —
  correct latency, recorded files bit-compare where feasible.
- **Gate/MIDI output sample-accuracy:** CV gate + MIDI clock-out timing vs current
  (exercises Phase 4 + the existing `deluge_midi_gate_timer_arm` path).
- **Host harness:** golden-WAV diff is bit-exact through Phase 1; stays within an
  agreed tolerance once the model is reshaped (it should not reshape — D1/D2 keep
  RZ/A1L identical).

## Risk notes

- Deepest seam in the codebase; realtime, audible, DMA-coherency-sensitive. The
  bit-identical Phase 1 + the Phase-0 harness are the two things that de-risk it —
  do both before any cleanup.
- Uncached-mirror access must be preserved exactly (D4/inventory) — cached-pointer
  bugs are intermittent and brutal to diagnose.
- The variable window is preserved *by construction* (variable `frames` on RZ/A1L);
  do not "simplify" the RZ/A1L driver to fixed blocks.
- `disableInterrupts[]`/`devdrv_intc` is a separable concern — port it on its own,
  don't let it grow the audio change.
- Confirm nothing in the render path blocks (D3) before the Embassy BSP depends on it.
