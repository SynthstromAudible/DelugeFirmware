# Project-Model Extraction (firmware)

Status: **planning** — 2026-06-19

## Goal

Extract the *project model* — the persistent musical document (clips, notes,
instruments, params, arrangement, scale, tempo) — out of the `Song` god-object
and into an independent, **host-unit-testable** library, separated from view
state and execution. The extracted model is the **live runtime source of
truth** the firmware operates on, not merely a load/save snapshot.

This mirrors, for the legacy C++ firmware, what `spark-project` already provides
for the Rust successor (`spark`). The two stay *conceptually* related but the
firmware model is designed C++-idiomatically and does **not** track
`spark-project`'s structure field-for-field.

## Why this is hard today

Three concerns are tangled into one ~6,100-line object (`model/song/song.cpp`):

1. **Project data** — the document we want to extract.
2. **View / UI state** — `xScroll`, `xZoom`, `xScrollForReturnToSongView`,
   `xZoomForReturnToSongView`, `arrangementYScroll`, `affectEntire`,
   `currentClip`, … (23+ members) live directly on `Song`.
3. **Execution** — `song.cpp` reaches into `playbackHandler`, `view`,
   `sessionView`, `arrangerView` ~94 times; the data objects *are* the
   execution objects.

Serialization compounds it: there is **no DOM**. ~50 model classes parse
*themselves* one character at a time from a streaming pull-parser
(`Deserializer`) and write themselves to a streaming `Serializer` (218
`readFromFile`/`writeToFile` sites). Two formats already exist (legacy XML,
newer JSON) behind an abstract `Serializer`/`Deserializer`.

## Key constraints & decisions

- **Native C++, not Rust-owns-model.** A live model the firmware reads on the
  audio/sequencer path cannot sit behind FFI (per-access cost), and
  `spark-project` is idiomatic Rust (not `#[repr(C)]`), so it can't be shared as
  memory. The live `ProjectModel` is native C++.
- **RAM is no longer the blocker.** ~1 MB SRAM free + **64 MB SDRAM**: a full
  in-memory model lives comfortably in SDRAM; SRAM stays reserved for the audio
  / DSP hot path. The historical streaming-parse design was an artifact of tiny
  RAM, not a permanent law.
- **Serialization language is deferred.** Load/save runs at *load time*, not on
  the hot path, so an FFI boundary there is acceptable. We build the native C++
  model + decoupling first, keep the serializer behind a narrow interface, then
  spike linking the existing Rust `deluge-xml` (serde) crate behind a thin C ABI
  and measure build/marshalling cost before committing. Until then,
  serialization stays C++ (reusing the existing multi-version parsers).
- **`deluge-xml` / `spark-project` as a differential oracle.** Independent of
  the design choice, the Rust serde parser is a ready-made cross-check for
  round-trip fidelity tests.

## Target architecture

```
  ┌─────────────────────────────────────────────────┐
  │ Engine / playback / scheduling                  │  reads via
  │   (transport, voices, automation)               │  ProjectReader
  └─────────────────────────────────────────────────┘
                         │ (read-only interface, no concrete Song)
                         ▼
  ┌─────────────────────────────────────────────────┐
  │ ProjectModel  (native C++, SDRAM, host-testable)│
  │   domain/   value types (ids, time, scale, …)   │
  │   project/  data + mutation ops                 │  ◀── serialize/deserialize
  │   no view state, no playbackHandler, no voices  │      (narrow interface;
  └─────────────────────────────────────────────────┘       C++ now, Rust-FFI later?)
                         ▲
  ┌─────────────────────────────────────────────────┐
  │ SessionView / UIState  (scroll, zoom, current*) │
  └─────────────────────────────────────────────────┘
```

A `ProjectReader`-style read-only interface (concept borrowed from
`spark-engine-core`) sits between the model and the playback/scheduling code, so
the engine never depends on the concrete model type.

## Phases (bisectable, golden-gated — matches existing refactor workflow)

- **Phase 0 — Safety net (GATE).** Extend the golden-master harness
  (`tests/golden`) to *project files*: load → save → compare across a corpus of
  real songs spanning firmware versions; assert x86 ≡ ARM. Use Rust `deluge-xml`
  as a differential oracle. Nothing below is safe without this.
- **Phase 1 — Carve out view/UI state.** Move `xScroll`/`xZoom`/`*ForReturnToSong`/
  `arrangementYScroll`/`affectEntire`/`currentClip` etc. off `Song` into a
  `SessionView`/`UIState`. Mechanical, in-tree, stays green.
- **Phase 2 — Sever execution coupling.** Eliminate the ~94 direct
  `playbackHandler`/`view`/`sessionView` reaches in `song.cpp`; define the
  data/engine seam.
- **Phase 3 — `ProjectReader` interface.** Introduce a read-only interface the
  playback/scheduling code uses instead of touching `Song`/data directly.
- **Phase 4 — Native C++ `ProjectModel` library.** Extract `domain/` value types
  + `project/` data and mutation ops into a standalone target with **zero**
  firmware/UI/DSP dependencies, fully host-unit-testable. `Song` becomes a thin
  owner/wrapper of one `ProjectModel`.
- **Phase 5 — Serialization into the library.** Move load/save behind a narrow
  interface, still C++, reusing the existing multi-version parsers; no firmware
  deps; host unit tests. Keep the interface narrow enough that a Rust
  implementation could replace it.
- **Phase 6 — (deferred) Rust serde spike.** Link `deluge-xml` behind a thin C
  ABI for load/save; measure build cost (Rust toolchain + cbindgen on
  `armv7a-none-eabihf` — `deluge-embassy` proves the cross-compile) and
  DTO→model marshalling; decide whether to adopt.

## Phase 0 — detailed plan

Phase 0 is the regression net that makes Phases 1–5 safe: it pins the
**serialization behaviour** of the model so that any accidental change to *what
gets written to disk* is caught. It is the serialization analogue of the synth
audio golden-master harness (`tests/golden`), and reuses its conventions
(committed fixtures, SHA anchors, x86≡ARM oracle, `--update` re-baselining).

### The invariant (this is the crux)

The firmware's `Song::writeToFile` does **not** reproduce the input file
byte-for-byte. On load it upgrades legacy tag-format → modern attribute-format,
fills defaults, normalizes ordering, and drops tags it doesn't understand. So
"output == original" is the wrong contract. The stable contract is a
**canonical fixed point**:

```
  gen1 = save(load(original))
  gen2 = save(load(gen1))
  assert gen1 == gen2          # idempotency: the corpus entry is "settled"
  fixture = sha256(gen1)       # committed regression anchor
```

- **Idempotency (`gen1 == gen2`)** is a one-time validation that each fixture is
  a true fixed point, so we're not anchoring on a half-normalized artifact.
- **Regression contract during the refactor:** for every corpus song,
  `save(load(original))` must keep producing the committed `gen1` bytes. A
  refactor that changes serialization output breaks the hash → must be justified
  and re-baselined (`--update`), exactly like the audio harness.

### Driver: direct in-memory round-trip

Because Phase 0 tests *serialization*, not rendering, the driver isolates that
path rather than booting the full app + MIDI like the audio harness:

1. Load the song XML into a **writable** buffer; construct an `XMLDeserializer`
   over it and call `Song::readFromFile`.
2. Re-serialize via a memory-based `XMLSerializer`
   (`FileWriter::setMemoryBased()`), capture bytes via `getBufferPtr()` /
   `bytesWritten()`.
3. Hash and compare; repeat once more on the output to check the fixed point.

No FAT image, no audio clock.

#### Spike results (2026-06-19) — `sim/roundtrip_spike.cpp`

A throwaway driver (CMake target `roundtrip_spike`) established:

- **Links against `deluge_app` alone** — the load path pulls in nothing outside
  the app library; no separate boot subsystem at link time. ✅
- **Allocator + `Song` ctor + `setupUnpatched` + `openXMLFile` run headless** —
  no audio engine, display, USB, or flash init needed to reach `readFromFile`. ✅
- Added a **memory ctor to `XMLDeserializer`** (mirroring `JsonDeserializer`) +
  made `XMLDeserializer::reset()` honour `memoryBased`. Small, reusable, needed
  by the driver. ⚠️ uncommitted firmware change.
- The memory-based XML reader **mutates its buffer in place** (null-terminates
  tokens) — the input buffer must be writable. ⚠️ driver constraint.
- **`Song::readFromFile` is NOT isolated from execution:** it calls
  `AudioEngine::routineWithClusterLoading()` **6×** mid-parse (song.cpp 2115,
  2143, 2282, 2291, 2299, 2376) — cooperative yields that pump audio / load
  sample clusters during a long load. On the host these hit
  `GeneralMemoryAllocator::checkStack` (bogus stack-distance vs the *emulated*
  `program_stack_start` → false `E338`) → null `display` deref → SIGSEGV. ⚠️

#### Resolution (done 2026-06-19)

Gated the yields at the single chokepoint: `AudioEngine::routineWithClusterLoading()`
now early-returns when `activeDeserializer->shouldCallRoutines()` is false (i.e.
in-memory reads, which already set `FileReader::callRoutines = false`). One guard
covers all 10 load-path call sites — including the per-Output/Clip/Instrument
readers a real song reaches — not just the 5 in `Song::readFromFile`.
**Firmware-neutral:** file-backed loads keep `callRoutines = true` (default
`activeDeserializer` is the file-based `smDeserializer`), so the gate is off on
hardware. Changes:

- `storage_manager.h`: `FileReader::shouldCallRoutines()` getter + the
  `XMLDeserializer` memory ctor; `XMLDeserializer::reset()` honours `memoryBased`.
- `audio_engine.cpp`: the early-return guard.

With this, `roundtrip_spike` loads the minimal song fully headless: `readFromFile
-> 0`, clean `exit 0`. (The driver calls `_exit()` to skip firmware static
destructors, whose host-side teardown trips the allocator integrity check →
null `display`; unrelated to the load. `checkStack`'s false host `E338` is no
longer reached because the engine is never pumped during a gated load.)

#### Real-song validation (2026-06-19)

Ran the spike over 42 real songs from `~/Deluge Backup/SONGS` (after wiring a
`SevenSegment` display so the defensive `display->...` popup paths don't deref
null headlessly):

- **33/33 ordinary songs load fully headless, clean exit.** The gate covers the
  per-`Output`/`Clip`/`NoteRow` yields a real song hits — validated. ✅
- **All 9 failures are in `problem_songs/`** (dirs literally named "load_crash",
  "loadcrash", "drum_stealing"). The harness reproduces exactly the pre-flagged
  bad set and adds no spurious failures — strong fidelity signal.
  - 2× `Error::FILE_CORRUPTED` (CLickybasswobble) — parse issue, TODO.
  - ≥1 crash (`2.XML`) is **internal region FULL** during `NoteRow` ParamManager
    alloc. Host provisions `HOST_INTERNAL_BYTES = 2.0 MB`; device internal RAM is
    `0x20020000–0x20300000 = 2.8 MB`. The host **under-provisions internal by
    ~0.8 MB**, so big songs exhaust it on host before they would on device →
    likely a harness artifact, not (necessarily) a device bug. Secondary real
    bug: the OOM path is unhandled and the error popup itself faults.

**Harness fix needed:** raise the host internal region to match the device
(`HOST_INTERNAL_BYTES` 2.0 → ≥2.8 MB) in `src/bsp/host/host_bsp.c` before
trusting failures on large songs. (Touches the shared host BSP — coordinate with
the audio harness.)

#### Round-trip write half + fixed point (2026-06-19)

Extended the spike to the full `load → save → reload → save` fixed-point check
(`gen1 = save(load(orig))`, `gen2 = save(load(gen1))`, require `gen1 == gen2`,
anchor = hash(`gen1`)). Added `FileWriter::setMemoryBuffer(buf, len)` so the
global `smSerializer` writes into a large caller-owned buffer (its built-in
32 KB buffer truncates — memory mode never flushes); songs serialize fine into
a 16 MB host buffer.

**Result: 15 of 16 non-`problem_songs` SETTLE (`gen1 == gen2`), 0 mismatches,
x86 ≡ ARM bit-identical.** Both initial outliers resolved:

- `CLickybasswobble.XML` — **not a song**: its root tag is `<sound>` (a synth
  preset misfiled into `SONGS/`). `openXMLFile` correctly returns
  `FILE_CORRUPTED` (no `song` tag). Correctly excluded — not a bug.
- `Glitchy_2.XML` — was a **harness artifact, now fixed.** The driver set
  `currentSong` to the song *being loaded*; `Song::setBPM` during `readFromFile`
  triggers the undo/action logger (`getNewAction` → `grabFromClip`), which walks
  `AllClips::everywhere(currentSong)` and derefs each clip's `output` — null mid
  load (outputs are wired later). On firmware `currentSong` during a load is the
  *previous* song (the new one is `preLoadedSong`), so the logger never walks the
  half-loaded song. Fix: set `currentSong` only **after** `readFromFile`. Now
  settles.

  Two real findings remain from this: (a) `setBPM`'s `shouldLogAction=false` is
  ignored at `auto_param.cpp:293` (`recordUnautomatedParamChange` runs
  regardless) — the undo logger runs during load at all, which is conceptually
  wrong; (b) deserialization output depends on the load-time `currentSong`
  global (re-baselining showed 5 songs serialize differently by that alone) —
  another load/execution coupling. Both are Phase-1/2 targets, not gate
  blockers; the harness anchors on the neutral `currentSong`-unset-during-load
  form.

**Two more couplings surfaced on the write path** (both fixed in-harness by
clearing transient view state, which is itself the right separation):

- `Clip::writeDataToFile` (clip.cpp:674) serializes `"selected"`/`"beingEdited"`
  for `getCurrentClip()` by dereferencing the (null, headless) root UI. View
  state leaking into serialization. Harness clears `setCurrentClip(nullptr)`
  before saving → view-independent canonical form that still round-trips.

**Net:** the round-trip net works and the fixed point is real. The crashes are
not harness bugs — they are concrete instances of the project-state/execution
conflation this effort targets, surfaced precisely at the load/save boundary.

### Corpus (tiers, sample-free first)

Assemble `tests/golden/songs/` (real `SONGS/*.XML`, committed pristine):

- **Format coverage:** legacy tag-format songs *and* modern attribute-format
  songs (exercises the upgrade transforms — the riskiest serialization surface).
- **Firmware-version spread:** songs saved by several firmware versions.
- **Feature coverage:** synth + kit + MIDI + CV instruments; arrangement +
  session clips; automation; scales/keys; sections; param managers.
- **Source:** Synthstrom factory cards + community packs (the
  `feat/synth-golden-masters` community corpus is presets — Phase 0 needs whole
  *songs*). Start **sample-free**; songs that reference WAVs are a later tier
  (the sample *path* still serializes — that's what we test — but loading may
  need the file staged on a card image).

### Independent oracle — deferred

An independent parser (e.g. the `spark` `deluge-xml` serde crate) would catch a
self-consistent writer bug that the firmware's own round-trip cannot. Deferred
for now — Phase 0 anchors on the firmware's canonical fixed-point hash. Revisit
once the extraction is underway.

### Deliverables / checklist

- [x] Spike: confirmed host-sim reaches `Song::readFromFile` headless; links
      against `deluge_app` alone. Driver = direct in-memory, gated on removing
      the 6 in-load `routineWithClusterLoading()` yields (see above).
- [x] Gate the in-load `AudioEngine::routineWithClusterLoading()` yields behind
      `callRoutines` (single guard at the engine entry; firmware-neutral). Minimal
      song now loads headless, clean exit. Real-song validation pending corpus.
- [x] Round-trip driver (`tests/golden/song_roundtrip.cpp`, target
      `song_roundtrip`): in-memory load→save→reload→save fixed-point check,
      emits `gen1`.
- [x] Runner `tests/golden/songs_run.py` + `songs_fixtures.json` (SHA-256 of
      `gen1`); `--update` to re-baseline; fails only on a committed-hash
      `REGRESS`. Verified: detects a tampered fixture (exit 1), clean (exit 0).
- [x] CTest registration (`ctest -R song_roundtrip`, `SKIP_RETURN_CODE 77`) +
      `tests/golden/README`.
- [x] **Bring-your-own corpus:** only `songs_fixtures.json` (14 SHA-256 hashes,
      keyed by corpus-relative path) is in-repo; song `.XML` stays out-of-repo
      (privacy/licensing). Corpus via `$DELUGE_SONG_CORPUS`/`--corpus` (default
      gitignored `tests/golden/songs/`). No corpus → check SKIPs (exit 77, CTest
      *Skipped*) so CI stays green; with the corpus the gate verifies the
      committed hashes. Verified against `~/Deluge Backup/SONGS`: OK=14.
- [x] ARM/qemu oracle: built `build-sim-arm/song_roundtrip` (Cortex-A9/NEON,
      hard-float) and ran the same fixtures → **OK=15, bit-identical to x86**.
      Serialization is arch-independent; fixtures trustworthy on both backends.
- [x] Outliers triaged: `Glitchy_2` fixed (now a fixture; 15 total),
      `CLickybasswobble` is a misfiled `<sound>` preset (correctly excluded).
      `--update` takes `--exclude` (default `problem_songs`) to keep the baseline
      curated.
- [ ] Broaden corpus across formats / firmware versions / features.

### Phase 0 risks / open questions

- **Host init surface** for song load (the spike above resolves this).
- **Determinism of `writeToFile`** given a fixed model (any timestamps / random
  IDs / pointer-derived ordering must be ruled out, else fixtures flake).
- **Sampled songs** — scope to sample-free first; staged-sample tier later.

## Open risks

- **Real-time concurrency.** The god-object implicitly serializes audio-thread
  reads vs. UI-thread edits. The clean model needs an explicit discipline (the
  audio thread cannot `malloc`; mutating the SDRAM model during playback must be
  defined).
- **Format fidelity across firmware versions** is the hardest correctness
  surface — Phase 0's corpus must cover the legacy XML quirks (hex attributes,
  version-gated tags, custom encodings).
- **Build system** for a standalone library target + host tests, and (Phase 6)
  Rust-in-firmware-build integration.

## Related

- `docs/dev/libdeluge_separation_plan.md`, `docs/dev/libdeluge_bsp_design.md`
- `spark` (Rust successor): `libs/project/spark-project`,
  `libs/formats/deluge-xml`, `ProjectReader` in `libs/engine/core`.
