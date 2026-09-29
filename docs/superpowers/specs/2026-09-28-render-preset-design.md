# The synth golden masters on the Embassy host

Status: built. Steps 1 to 3 on 2026-09-28, steps 4 and 5 on 2026-09-29 (§9).
- Branch `merge/synth-golden-masters`, after the merge of `feat/synth-golden-masters` (8c0e55180).
- Carries out §9 of the test layout spec (`2026-09-28-test-layout-and-runner-design.md`), with one change: the golden
  masters move to golden_vt_render instead of staying on the C-host sim.
- Builds on the render_song spec (`2026-09-28-render-song-design.md`): the same host, render path, capture and record.

## 1. Why

The synth golden masters render 336 presets through the C-host sim (`deluge_host`, `tests/golden/run.py`) and compare
each render's hash with `tests/golden/fixtures.json`. After the merge none of them runs. All 336 render silence,
because the C host no longer reads the card image run.py builds (`DELUGE_SD_IMAGE`). Staging a card directory for
its `DELUGE_SD_ROOT` passthrough, as the test layout spec planned, still loads no preset (`sine_basic` stays silent).

Mending the C host would keep a second host alive for one gate. That host is not the device's path either: it
renders 32-frame blocks on its own C BSP, with no Embassy scheduler and no tiers. golden_vt_render boots the same app
on the Rust BSP the device ships, renders on the audio tier, and already has what a preset render needs: a card
filled from a directory, deterministic real-time rendering, PCM capture and the `RS-PCM` record. The render_song spec
already describes that record as "the same record the synth goldens keep".

## 2. Goals and non-goals

Goals:
- Every corpus case renders on golden_vt_render, driven by a `render_preset` stepped scenario, and matches a recorded
  hash.
- The corpus, note scripts and per-case settings carry over unchanged.
- The new records are adopted only with per-fixture evidence that each case still plays the same sound (§7).
- The golden masters run from the registry like every other gate, with no mtools.
- An ARM leg, if golden_vt_render can be built for armv7 and run under qemu (§8).

Non-goals:
- Mending or deleting the C-host sim. Its golden-only pieces (the DIN script, the test input signal) become unused;
  removing them is separate work.
- A live card passthrough for the Embassy host, which would serve a host directory instead of copying it in at boot.
  It is wanted as the base for a local song-render tool, but it is a separate piece of work. This spec keeps the
  memory card that `render_song` uses.
- Fixing the synth boundary violations (nine files reaching `memory/` and `model/` through
  `modulation/params/param_set.h`), and folding the three boundary checkers into one. Both are §9 items of the test
  layout spec, done separately.

## 3. What 2026-09-28 established

- **After the merge, render-song's hash is unchanged** (`5f51108b…369d`). Sample playback renders bit-identically through the synth branch's `SamplePlaybackAdapter`.
  That is the only bit-exact evidence for the merge until this spec's §7 comparison.
- **golden_vt_render's DIN input is a stub.** `deluge_midi_din_read_timed` (`src/bsp/rust/src/host_link_stubs.rs`)
  always returns false. The C host's scripted source lives in `src/bsp/host/host_bsp.c` (`host_din_load_script`).
- **golden_vt_render's audio input is silence.** `audio_host.rs`'s `INPUT_BLOCK` is zeroed and never written. The
  C host's deterministic test signal lives in `src/bsp/host/host_audio.c` (`fill_test_input`).
- **The corpus:** 336 cases, of which
  - 27 synthesized cases at the top level,
  - 168 factory and 128 community presets,
  - 5 sample and 8 wavetable cases, which carry `<name>.wav` and `<name>.2.wav`.

  They take `default.notes` unless a case has its own `.notes`. Captures are 2 s, except `env_shape` at 3 s. The
  three `input_*` cases need the test input signal.

## 4. The `render_preset` scenario

A stepped scenario in `tests/integration/scenarios/render_preset_scenario.{h,cpp}`, registered in
`scenario_table.cpp`, with the same shape as `render_song`. It needs no song: the preset loads the way the fixtures
were designed to load it. The blank boot song takes the first preset in `SYNTHS/` (`PRESET.XML`), through the real
deserialization path. The note script plays on MIDI channel 1, which MIDI follow sends to the active instrument. Any
MIDI learn the preset carries is stripped when it is staged, because a learn on the same channel delivers every note a
second time (step 4 found the C-host records doubled that way).

Inputs:

| Variable | Meaning | Default |
|---|---|---|
| `RENDER_TREE` | the card tree: `SYNTHS/PRESET.XML`, plus `SAMPLES/WT.WAV` and `SAMPLES/WT2.WAV` when the case has them | required |
| `RENDER_MIDI` | the note script, in the `.notes` format; frames count from capture start | none |
| `RENDER_SECONDS` | capture length | 2.0 |
| `RENDER_INPUT` | `1` feeds the test input signal (§5) | off |
| `RENDER_PCM_OUT` | write the captured output here (raw interleaved i32, as `render_song`) | none |

Stages, each printing its evidence:
1. **Loaded.** After boot and mount, `RP-LOADED name=<n> type=<synth|…>`. The current song must hold one synth whose
   preset came from `SYNTHS/PRESET.XML`, and not the default synth a failed load leaves behind. The C host could not
   report this: a failed load showed up only as a silent render.
2. **Render.** Turn on real-time rendering, start capture and start the note script at the same frame, then render
   until `RENDER_SECONDS` of frames are captured. `RP-RENDER blocks=<n> direness_max=<d>`, `RP-CAPTURE frames=<n>`.
   `direness_max` must be 0. The fixtures are meant to capture full-quality paths, and the C host's first offline
   driver did not: its 128-frame blocks pinned `cpuDireness` at 14 and culled voices every block, which is why it
   moved to 32-frame blocks. golden_vt_render also renders 128-frame blocks, but at the device's cadence in virtual
   time, and render_song reports `direness_max=0` there. This check keeps it that way.
3. **Report.** `RP-MIDI released=<n> of=<m>`: every scripted event must have been read by the app before capture
   ends. `RP-TIER-SYNC-AUDIO total=<n>` must be 0. `RP-RESULT failures=<n>`.

The check module computes the PCM record and a silence check, as `render_song.py` does (§6).

## 5. What golden_vt_render needs for it

- **A scripted DIN source.** `deluge_midi_din_read_timed` on the host reads events from a script: `<frame> <hex
  bytes>` per line, `#` comments, frames nondecreasing. It releases an event's bytes once the render has passed that
  frame. The C host's parser is ported to Rust, and the stub is replaced by the script source whenever one is armed.
  The scenario arms it (`deluge_host_midi_script_start(path)`), which sets frame 0 to the current render position.
  The C host counted from boot instead, so its note timing depended on boot length. Arrival ticks are the Embassy
  clock, which is virtual here.
- **A test input signal.** When armed (`deluge_host_test_input(bool)`), `deluge_audio_drive` fills `INPUT_BLOCK` with
  `host_audio.c`'s signal before each render, keyed to the frame count since arming: two sawtooths, 128 and 256
  frames long, at full q31 scale.
- **A card fill for the scenario.** `render_preset` installs `RENDER_TREE` with `memory_card::install`, as
  `render_song` does.

## 6. The gate

- **The corpus and records move** to `tests/integration/golden/presets/` (the corpus, unchanged) and
  `tests/integration/golden/presets.json` (the records), next to `songs/` and `songs.json`. `make_corpus.py` and
  `make_sample.py` move with them.
- **A check module** `tests/integration/checks/render_preset.py` replaces run.py. For each case it:
  - stages the card tree in the gate's temporary directory, with run.py's MIDI-channel injection;
  - runs `GOLDEN_SCENARIO=render_preset` with the case's settings;
  - checks the scenario's lines, and that the render is not silent (peak above 1, as run.py required);
  - compares `{sha256, frames, peak, rms_db}` with `presets.json`.

  Cases run in parallel, one per CPU, and each case's failure is reported on its own. A `RENDER_PRESET_FILTER`
  selects cases by substring. `RENDER_PRESET_UPDATE=1` records instead of comparing, as `RENDER_SONG_UPDATE` does. A
  changed hash is a finding to explain in the commit that records it.
- **Registry entries:**

  | Entry | Tier | Cases |
  |---|---|---|
  | `golden-synth` | pr | all 336 |
  | `golden-synth-arm` | manual | §8 |

  Step 1 measured a case at about 0.04 s of wall time (boot and 690 blocks, all in virtual time), so all 336 take
  seconds. `golden-synth` therefore runs every case in the PR tier, and there is no `golden-synth-corpus`.
- **Retired** with the move: `tests/golden/run.py`, `tests/golden/fixtures.json`, and the README's C-host
  instructions. The README moves to `tests/integration/golden/` and is rewritten for the new gate.

## 7. Re-recording, with evidence

Every hash changes. The render is 128-frame blocks instead of 32, and the capture is 32-bit instead of the C host's
16-bit WAV. Note arrival is quantized to a different block, and each note-on draws the synth's random numbers from a
different point in the stream. A hash cannot show the sound survived, so the records are adopted only after each case
has been compared with its old render:

1. **Old renders.** Build the C host from `feat/synth-golden-masters` in a worktree, where its image-based card still
   works, and render all 336 cases with `run.py --keep`. Check each against its `fixtures.json` hash, so the reference
   is known-good.
2. **New renders,** from `render_preset` with `RENDER_PCM_OUT`.
3. **Compare** with `tests/integration/golden/compare_renders.py`, a tool kept for later re-baselines. It scales both
   renders to the same full-scale range and aligns them on the first note's onset. It reports, per case:
   - windowed RMS envelope correlation;
   - peak and RMS level difference in dB;
   - the difference in spectral centroid;
   - sample-level correlation, meaningful only for cases with no randomness.

   Thresholds are set from the synthesized cases, whose sound is fully known, and then applied to the corpus.
4. **Every outlier is explained** before recording: a harness difference such as onset quantization, a real
   behaviour change the merge brought from `next`, or a bug. Two known changes arrive with the merge: #4922 (delayed
   note-ons) and #4930 (master transpose applied to every oscillator). Presets that exercise them are expected to
   differ, and each is named in the recording commit.
5. **Record,** and commit the comparison report under `docs/superpowers/baselines/`, next to the other baselines.

## 8. The ARM leg

The C host sim's x86 ≡ ARM check ran the same cases on an arm-linux cross build under qemu. That check guarded the
hand-written NEON and assembly paths. For golden_vt_render the equivalent is:
- a host-app tree cross-built for arm-linux (`sim/` already maps `DELUGE_SIM_ARM` to `armv7-unknown-linux-gnueabihf`);
- golden_vt_render built for that target;
- run under `qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9`, and required to match the x86 records.

None of this has been built yet. Step 5 starts with a feasibility check: whether the executor, the sim block device
and the Rust staticlib build and run for armv7 Linux. If they can't, the gap is recorded in this section, and the ARM
unit tree (`unit-arm`, real NEON under qemu) stays the only ARM coverage of the dsp kernels.

## 9. Steps

1. **One preset.** Add the `render_preset` scenario with the card fill and the load check, plus the render and
   capture without MIDI. Show that `sine_basic` loads (`RP-LOADED`), and add the DIN source with `default.notes`
   until it sounds. Measure a case's wall time.
   - [x] Built. `sine_basic` loads (`RP-LOADED name=PRESET swapped=1`) and both script events are read. Direness
         stays 0, and there are no audio-tier card calls.
     - The note sounds from frame 4736, one block after its scripted 4608, and falls silent after the note-off at
       52000.
     - Two runs are byte-identical, and a case takes about 0.04 s of wall time.
     - Over the 2 s the C host recorded, the RMS level matches its record exactly (-26.48 dB). The peak does not:
       2644 in 16-bit terms, against the record's 4771. Step 4 finds out why.
2. **Input.** Add the test input signal. Show that the three `input_*` cases render non-silent and differ from
   silence-in.
   - [x] Built (`deluge_host_test_input`, armed on capture's first frame when `RENDER_INPUT=1`).
     - With the signal, the three cases render at -23.14, -24.72 and -23.15 dB, three distinct renders. Without
       it they fall to -141 dB.
     - Each RMS level is within 0.02 dB of its C-host record (-23.12, -24.72, -23.13).
3. **The gate.** Move the corpus, add `render_preset.py` and the registry entries, and retire run.py. Record the
   provisional hashes, which the next step either confirms or replaces.
   - [x] Built. The corpus, `make_corpus.py` and `make_sample.py` are in `tests/integration/golden/presets/`, and
         `make_corpus.py` still reproduces the committed XMLs byte for byte. `golden-synth` (pr) runs all 336 cases in
         parallel: about 16 s including the build, and the records reproduce across runs.
     - Every case loads, reads every scripted event, keeps direness at 0 and renders sound.
     - Measured against the old C-host records' RMS levels:

       | Change in RMS level | Cases |
       |---|---|
       | within 0.1 dB | 136 |
       | within 0.5 dB | 228 |
       | within 3 dB | 315 |
       | more than 3 dB | 21 |

       The largest is `community/Cornwall`, up 18.6 dB. A cluster near -6 dB also halves its peak, which looks like
       a factor of 2 in amplitude. Step 4 explains these before the records stop being provisional.
4. **Evidence and re-record** (§7).
   - [x] Done. The report is `docs/superpowers/baselines/2026-09-29-synth-golden-masters-rebaseline.md`. Every case
         is either the same sound or explained by a demonstrated cause:

         | Class | Cases |
         |---|---|
         | same as the old code | 147 |
         | within its note-timing / random-draw tolerance | 150 |
         | `next`'s reverb fixes #4936 and #4856 | 18 |
         | same, judged at the old code's own 32-frame blocks | 10 |
         | wavetable morph steps once per block | 7 |
         | just outside tolerance, and timing-sensitive | 4 |

     - The old C-host harness delivered every note twice (the MIDI learn run.py injected, plus MIDI follow), which
       doubled 19 of its records. The gate now strips MIDI learn and plays through follow.
     - The synth's random stream now restarts at the first note (`RENDER_RANDOM_SEED` picks the seed), so a record no
       longer depends on how much randomness boot drew.
     - A regression on `next` turned up and is fixed: the blank boot song dropped its first preset's saved MIDI learn.
       `RP-LEARN` checks it.
     - Two things to look at separately:
       - the C host's 32-frame blocks swell each note's first cycle up to 1.8x, and the device's DMA windows are just as
         small;
       - the old code rendered `wavetable/sync` silent at 128-frame blocks.
5. **ARM** (§8).
   - [x] Built. `golden-synth-arm` (nightly) cross-builds golden_vt_render for `armv7-unknown-linux-gnueabihf` and
         runs it under qemu. It links against `build/host-app-arm`, configured through
         `sim/arm-linux-toolchain.cmake`.
     - All 336 cases and `RP-LEARN` match the x86 records byte for byte. The gate takes about a minute from a clean
       tree, and it fails when the renders change.
     - Every host tree is built with clang now, the compiler the firmware ships with: the ARM toolchain file and the
       x86 `build/host-app` both. The records and every other recorded output were unchanged by the switch.
     - Getting there took three fixes:
       - clang rejects three narrowing initializers in the voice code, which GCC only warns about;
       - `FREEZE_WITH_ERROR` reached the device-only pad-grid reporter on any `__arm__` build;
       - the device `build.rs` archived objects whose sources had moved, so the synth branch's relocated modulation
         core linked twice. The shipping RelWithDebInfo image links again.

## 10. Open questions

- **Onset quantization.** Settled for step 1: an event reaches the app during the block after the one its frame falls
  in, and runs are byte-identical. Every onset moves by the same one block, which §7's alignment absorbs.
- **Voice-start budget.** On the Rust host the render runs in real time on the audio tier, so a preset heavy enough
  to hit `allowedToStartVoice`'s budget could drop notes. The C host did the same. §7's comparison would surface it
  as a missing onset.
