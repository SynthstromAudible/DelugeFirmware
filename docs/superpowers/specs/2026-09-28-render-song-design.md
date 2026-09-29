# One way to render a song on the host

Status: all five steps built 2026-09-28 (§11).
- Branch `feat/rung3-browser-listing`.
- Follows the lens1 investigation of the same day (conversation; findings recorded in memory, `streaming-underrun-harness`).
- Relates to the test layout spec (`2026-09-28-test-layout-and-runner-design.md`), whose §9 brings in the synth golden masters.

## 1. Why

Three paths render audio on the host today, each built for one gate, and none of them renders a song the way the
device plays one:

| Path | Renders | How | State |
|---|---|---|---|
| golden_vt_render, `golden-vt-render` and `stem-export` | a song, as stems | offline `StemExport`; voices render on the INTERACTION tier, in `renderAudioForStemExport` | works; hashes recorded |
| lens1_vt_sim | a song, played in real time | its own binary, clock, executor, card image and task list | measures nothing (below) |
| feat/synth-golden-masters `tests/golden/run.py` | presets, not songs | the C-host sim's real-time path, 32-frame blocks, a scripted MIDI note | works; 336 fixtures, x86 ≡ ARM |

Nothing renders a song in real time on the audio tier, which is where the device renders it, so nothing tests
streaming playback: reading sample clusters from the card while voices play. lens1 was meant to, and has measured
nothing since July, for three reasons found on 2026-09-28:

1. **It never renders after boot.** `audio_host::should_skip_render()` (710c402f8, 2026-07-29) skips every post-boot
   render not on host_app's `"deluge-audio"` thread. A single-executor harness has no such thread.
2. **It never loads its song.** It does not spawn the channel drivers golden_vt_render spawns
   (`file_directory_task`, `file_prefix_task`, `file_bytes_task`, `file_write_task`, `audio_load_task`). The C++ weak
   fallbacks stand in: the directory request does nothing and the collect answers UNKNOWN, so the `SONGS` listing
   times out after 600 ticks with `SD_CARD`, the load gives up, the one-clip default song stays, and the empty
   arrangement stops playback on its first tick. Broken since 7ae3290ed (2026-08-29).
3. **It varies the wrong thing.** It sweeps card throughput. On the device, three quarters of a streaming chunk's
   latency is the fill task waiting to be scheduled (16 ms steady mean, 260 ms bursts during song load); the card
   takes 5.8 ms per 32 KB fill.

Its scenario reported success throughout, because it checked that each step was attempted, not that it worked:
"load completed" meant the load stopped running, and "playback confirmed" meant the clock ran for one block.

Two of the three failures come from lens1 keeping its own copy of what golden_vt_render already sets up. A second copy
falls behind every time the first one changes.

## 2. Goals and non-goals

Goals:
- **One `render_song` scenario** in golden_vt_render that loads a song through the real browser, renders it in real
  time on the audio tier for a set length, and reports what happened with evidence: the song that loaded, blocks
  rendered, sample-voice renders by tier, underruns, audio-tier synchronous card calls, and the PCM hash.
- **Gates built on it**, each a registry entry plus a check: a committed song for the PR tier, the personal corpus
  (Cordae) for manual runs, and streaming margin.
- **A latency model of what the device measured:** fill-task scheduling delay first, card latency second.
- **Retire lens1_vt_sim.**

Non-goals:
- Moving the synth golden masters off the C-host sim (§9).
- Changing the offline stem export or its recorded hashes. It stays exactly as it renders today.
- Hardware measurement. The calibration numbers come from the device measurement already made.

## 3. What 2026-09-28 established

- **With every post-boot render on (`DELUGE_RENDER_AFTER_BOOT=1`, 45ccb9044), no audio-tier synchronous card call
  and no wedge** across all 25 golden_vt_render scenarios and lens1 with its latency model on. The meter goes red on a
  planted call.
- **But almost nothing it rendered played samples:** about 120 sample-voice renders on the audio tier estate-wide.
  The evidence that the render never blocks on the card is real but thin, and `render_song` is what thickens it.
- **The offline export is deterministic because nothing renders before it starts.** With post-boot renders on, the
  export starts at `audioSampleTimer` 4,992 instead of 128 and the stem hash changes. Rendering only during the export
  reproduces the recorded hash exactly: the export loop's own `deluge_audio_drive()` calls change nothing.

## 4. The `render_song` scenario

A stepped scenario (a row in `tests/integration/scenarios/scenario_table.cpp`), run by golden_vt_render's one driver
on the memory card, configured by environment variables so every gate is a registry entry:

| Variable | Meaning | Default |
|---|---|---|
| `RENDER_SONG` | the song's card path | required |
| `RENDER_TREE` | the card tree to fill the memory card from | the golden fixture tree |
| `RENDER_BLOCKS` | real-time blocks to render after playback starts | 3,000 (≈8.7 s) |
| `RENDER_MIDI` | a note script, in the synth goldens' `.notes` format | none |
| `RENDER_PCM_OUT` | write the captured output as a WAV here | none |

Steps, each with an outcome check that prints its marker and counts a failure when the check fails:

1. **Load.** Set the song path, open the load browser, tick it until its listing lands, `beginLoad`, tick until the
   load is no longer in flight. `RS-LOADED name=<n> outputs=<k>`: the loaded song's name must be the one asked for,
   and it must not be the default song. This is the check lens1 lacked.
2. **Play.** Arranger or session as the song was saved, then play. `RS-PLAYING`: the clock must still be running
   after the first sequencer tick.
3. **Render.** Wait until `RENDER_BLOCKS` real-time blocks have rendered after playback started. Each block is
   rendered by the audio task at the fixed virtual cadence Lens 1 already sets
   (`scheduler::set_audio_period_override_us`, 2,902 µs per 128 frames). `RS-RENDER blocks=<n> stops=<k>`: the full
   count, with playback never having stopped (`stops=0`) unless the song ends inside the window.
4. **Report.**
   - `RS-VOICES audio=<n> interaction=<m>`: sample-voice renders by tier. A song with sample tracks must render
     sample voices on the audio tier.
   - `RS-TIER-SYNC-AUDIO total=<n>`, required 0: the render must never make a synchronous card call.
   - `RS-UNDERRUN wait=<n> unassign=<n>`.
   - `RS-PCM sha256=<h> frames=<n> peak=<p> rms_db=<r>`: the same record the synth goldens keep, over the output
     block the render wrote.
   - `RS-RESULT failures=<n>`.

### What golden_vt_render needs for it

- **Real-time rendering per scenario, not per process.** `should_skip_render()` keeps skipping unless the running
  scenario asked for real-time rendering. `render_song` asks; every other scenario, and the stem export above all,
  renders exactly as today. `DELUGE_RENDER_AFTER_BOOT` stays as the measurement override.
- **A capture sink.** `audio_host`'s null sink hashes each rendered block (and writes the WAV when asked) while
  capture is on. Capture starts with playback.
- **A per-tier sample-voice count** (`VoiceSample::render`), a host-only counter behind a C ABI getter, like the
  existing underrun counters.

## 5. Fixtures

- **A committed song for CI.** A small sample-backed song generated at build time by a script (the way
  `make_sample.py` generates the synth goldens' samples), with samples several clusters long so playback streams
  cluster by cluster. Nothing binary is committed. Its record lives in `tests/integration/golden/songs.json`, next to
  the synth goldens' `fixtures.json` and in the same shape.
- **The personal corpus (Cordae and others) for manual runs.** Rebuilt from `DELUGE_BACKUP` as today. These entries
  get a `requires` key for the corpus, so a machine without it skips them with the reason. `golden-vt-render` and
  `stem-export` get the same key: today they declare no requirement but only run where the backup exists.

## 6. Gates

| Entry | Tier | What it requires |
|---|---|---|
| `render-song` | pr | the committed song loads, plays for the full length, renders sample voices on the audio tier, makes no audio-tier card call, and matches its recorded hash |
| `render-song-corpus` | manual | the same, for each corpus song (Cordae first) |
| `streaming-margin` | nightly | §7 |

## 7. Streaming margin

**The model.** Delay the streaming fill task between a chunk's request and the fill starting, in virtual time:
- a steady delay, calibrated to the device's per-interval measurement (16 ms mean, 25 ms max);
- a burst, 260 ms, applied while a scripted competing storage job runs (a second song load, a large listing);
- card latency (today's `sim_latency`, 5.8 ms per 32 KB) as the second knob.

The numbers come from the device measurement (storage-execution-model design, Appendix C; per-interval figures, not
the cumulative 35 ms mean in `audio_engine.cpp`'s comment).

**The check:**
- At the steady calibration, `RS-UNDERRUN` must be 0.
- A starved configuration (a constant delay well past one cluster's playback time) must produce underruns. This is
  the detection control lens1's `CONTROL_A` was meant to be.
- The burst case records its count as a characterization line. It becomes a gate when the storage tier gets its
  scheduling guarantee.

**The sweep**, lens1's margin curve, stays available as a manual run of the same scenario over a range of delays.

**What it needs:** the latency model must become switchable at run time. It is a compile-time feature today, and
golden_vt_render leaves it off because it slowed the stem export to about 0.01× real time. It needs lens1's progress
hook (`sim_block` + `preempt`), which keeps the latency model's pump running while a synchronous wait spins. That
moves into golden_vt_render with its selftests.

## 8. Retiring lens1

When `render-song` and `streaming-margin` pass, `src/bsp/rust/harness/lens1_vt_sim/` and its registry entry are
deleted. What moves first: the progress hook and its selftests, the scheduling-delay model, and the sweep as a manual
entry. Nothing else in lens1 is unique.

## 9. The synth golden masters

`render_song` takes the synth goldens' inputs (a card tree, a song or preset, a `.notes` script, a length) and writes
their record (`sha256`, `frames`, `peak`, `rms_db`). It does not replace them. They stay on the C-host sim as the test
layout spec's §9 plans, because the C-host builds for ARM Linux and every fixture is checked bit-exact under qemu,
while golden_vt_render is x86_64 only. Whether the corpus later moves onto `render_song`, leaving one host platform
for rendering, is a separate decision. It needs either an ARM build of golden_vt_render or an agreement that the
C-host oracle stays.

## 10. Also

The weak no-BSP fallbacks in `storage/*_abi.cpp` turn a missing driver into a silent 600-tick timeout: a harness that
does not start a driver gets no link error. In host builds, a fallback called after boot should say so once on
stderr, and the golden runner should fail a run that printed it.

## 11. Steps

1. **Done** (32cfa891b, 48a2a0b4f). `render_song` with the committed generated song: per-scenario real-time
   rendering, the capture sink, the voice counter, the outcome checks; `render-song` at the PR tier, about 8 s.
   - The generated song renders 12,246 sample-voice renders on the audio tier, with 62 card reads while playing,
     no underruns and no audio-tier synchronous card call. Two runs from freshly generated trees render
     byte-identical output.
   - Controls, each seen to fail: a song that does not exist (the browser loads the nearest file instead, and
     `RS-LOADED` fails), a card with no songs (the load gives up; `RS-LOADED` fails), a song whose clips hold no
     notes (`RS-VOICES` fails), and a tampered record (`RS-PCM` fails).
   - `RS-STREAM reads` is a card-read count, added so a song whose samples were already resident cannot pass as
     streaming.
   - What the song format turned out to need, found by the checks:
     - A synth's parameters go in each clip as `soundParams`, not in the sound: without them the load freezes
       with E412.
     - Notes use `noteDataWithSplitProb` for a `c1.3.0` song: `noteDataWithLift` is read only from older
       versions, and the clip loads empty.
     - `inArrangementView` is read by its presence, whatever its value, so `inArrangementView="0"` puts the song
       in the arranger, and a song with no arrangement stops on its first tick.
2. **Done** (70cc680cd). `render-song-corpus` plays Cordae from the golden fixture card (manual tier): loaded in the
   arranger with 8 outputs, 10,012 sample-voice renders on the audio tier, 627 card reads while playing, no
   underruns. `golden-vt-render`, `stem-export` and `render-song-corpus` require `song-corpus`, whose probe also
   accepts fixtures already rebuilt in the golden cache.
3. **Done** (ad6532497, a3742b4c8), smaller than planned: **no run-time latency switch and no progress hook.** The
   fill task already had a host knob for its wait before each pass (`deluge_streaming_set_fill_latency_ms`), and a
   per-chunk read delay went in beside it (`deluge_streaming_set_fill_read_latency_us`). Both wait in the async fill
   task, where waiting costs nothing: `sim_latency` and lens1's progress hook exist to delay SYNCHRONOUS transfers,
   boot's included, which the margin does not need. `render_song` applies the model while playing (`RS-MODEL`).
   - Building the gate found that the committed `render-song` song had never streamed: without the tick attributes
     the firmware writes for a new song, its clock ran so fast that every note retriggered within a fraction of a
     second and only the start of each sample played. Its "streaming" reads were the load's background reads. The
     song now carries a tempo (226 reads while playing), and `RS-SWELL` checks that the output's level moves
     (1.62 when playback advances, 1.02 when it is stuck).
   - `streaming-margin` (nightly): the device's steady figures, 16 ms wait and 5.8 ms per 32 KB read, stream with no
     underrun; a 500 ms wait drops both voices (the detection control); a 260 ms burst for half a second drops both
     voices, as the device does, and is reported. `streaming-margin-sweep` (manual) prints the curve: no underrun up
     to a 125 ms wait, the first dropped voice at 150 ms. A cluster holds 186 ms of this song's audio.
4. **Done** (6e2d0e547). lens1 deleted; nothing of it was carried over (step 3). Comments that pointed at it now say
   what they meant.
5. **Done** (44926e8bf). Every weak fallback in the six channel files reports its first call after boot on stderr,
   counted; golden_vt_render exits 4 when any was called. Proven by hiding the directory driver's collect: a listing
   gate reports 1,603 calls.

Each step: `./dbt test` green, and a capture identical to the one before it on every existing marker.

## 12. Open questions

- **Culling in virtual time: answered.** `direness_max` reads 0 for both songs: under the virtual clock the audio
  task's measured run time is zero, so voices are never culled. The render does not capture the degraded path the
  synth goldens once did, but it also never exercises culling, which a real-time-paced render would.
- **Kits on x86_64: answered for Cordae.** Its eight kits render on the 64-bit host.
- **Session or arrangement: both covered.** The PR song plays in session view, Cordae in the arranger.
- **The device image grew 400 bytes** with the tier meter's audio-tier counting (45ccb9044): the counting runs on
  the device too, where an audio-tier synchronous call is equally a violation.
