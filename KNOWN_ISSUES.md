# Known Issues

## Voice output: 2-tap (`[0.5, 0.5]`) half-sample-delay low-pass in the shared render path

**Status:** open · **Found:** 2026-06-16 (`feat/synth-golden-masters`) · **Severity:** low (inaudible for real
audio) but a genuine output-fidelity defect.

### Symptom
The rendered voice output is a 2-tap moving average of the per-sample render — `out[A] ≈ ½(render[A] +
render[A-1])` — i.e. a half-sample group delay plus a mild low-pass. The effect is frequency-dependent:

| Probe fed to a live-INPUT source | output↔input correlation |
|---|---|
| white noise (full spectrum) | ~0.60 |
| 43 Hz band-limited triangle | ~0.9997 |

So it is **inaudible for real (band-limited) audio**, but a full-spectrum probe exposes it clearly.

### What we know
- It is in the **shared render path** (`renderingMemory`), i.e. the same code that runs on hardware — **not a
  host-sim artifact.** A single input impulse produces `renderingMemory[N] ≈ renderingMemory[N+1] ≈ ½·peak`.
- The per-source **input read is a clean exact 1:1 pass-through** — `oscBuffer` is single-tap (an impulse train
  fed every 1024 frames yields `oscBuffer` spikes exactly 1024 apart; a frame-index-encoded probe reads back
  exact integers at a constant offset). So the `inputReadPos` loop is **not** the cause.
- The 2-tap is therefore introduced **downstream of the read, in the voice → Sound output write**
  (`processing/engines/audio_engine.cpp` `renderAudio` → `currentSong->renderAudio` → `Voice` → `soundBuffer`,
  which *is* `renderingMemory`). `outputStage` is per-sample (reads only `renderingMemory[A]`), so it is not the
  source.
- It is **not** addressed by `main`'s `95fad73b` "Fix stereo unison input" (#4538): that is an input-*read* fix
  and leaves the mono read byte-identical; it is upstream of the 2-tap.

### Where to start a fix
1. The voice's post-filter output write to `soundBuffer` in `model/voice/voice.cpp` (~L1660–1700: the
   `filterSet.renderLong(...)` call followed by the `outputSample` / `overallOscAmplitude` write, and the
   stereo/pan path). Suspect a step that blends adjacent frames or maps one `oscBuffer` sample to two
   `soundBuffer` frames.
2. Establish **general vs source-specific**: is every voice/oscillator half-sample-averaged, or only certain
   paths? (Impulses can only be injected via INPUT; confirm other sources by reading the output-write code.)
3. **Decide desirability before changing anything** — it may be intentional interpolation / anti-imaging. If it
   is undesirable, removing it is a deliberate **device sound change** and requires a full re-baseline of the
   golden fixtures (`tests/golden/`); treat it as its own firmware-audio task.

### Reproduction
The host-sim live-input injection harness (`DELUGE_HOST_INPUT=1`, `src/bsp/host/host_audio.c`) feeds a
deterministic probe into the input ring; reconstruct the expected signal in Python and cross-correlate against the
rendered WAV. An impulse train reveals the 2-tap impulse response directly; a band-limited probe (corr ~0.9997)
vs a white probe (corr ~0.60) is the signature.
