# Audio file-loading redesign

Status: **in progress** on `feat/audio-file-parser`. The sample load path is redesigned, de-goto'd, and
runtime-validated; the WaveTable byte-source rework and the typed-result / orchestration-collapse phases
remain. Behaviour-preserving throughout (no intended change to what loads or how it sounds).

## Why

The WAV/AIFF file-loading code (`AudioFileManager::getAudioFileFromFilename`, the `AudioFileReader`
hierarchy, the parser, `WaveTable::setup`) was deeply entangled: a sentinel-laden flat descriptor, a byte
reader whose cluster cursor was poked from outside the class, fake `(Sample*)this` polymorphism, and a
~430-line loader with 15 `goto`s fusing file-resolution, object construction, the reason lifecycle, and RIFF
sniffing.

That badness is a **design** problem, not a language one, so we redesign it **in C++** rather than porting
to Rust — porting the bad shape would enshrine it behind an FFI seam and make the real cleanup harder, and
the consumers (`Sample`, `WaveTable`, the holders, ~45 includers) are all C++ and staying C++. This is the
componentization-first principle: get a clean component, and language becomes a swappable implementation
detail later (cf. `docs/dev/resource_manager.md`, which moved the *mechanism* — eviction/residency — to Rust;
the file *parser* is leaf app-logic where modern C++ `std::span`/bounds-checked reads already give most of
the safety win).

## Scope

**In:** the one-time header-load path — the byte-reader abstraction, the parser, and the file→object
construction in `getAudioFileFromFilename`.

**Out (separate subsystems, untouched):** runtime playback streaming (`SampleLowLevelReader` /
`VoiceSample` / `SampleCluster::getCluster`); the cluster-materialise / loader-pump path
(`loadCluster` / `readClusterData` / `loadAnyEnqueuedClusters` → `deluge_resource_*`, already Rust); the
`Sample` / `WaveTable` object-model internals; and `SampleRecorder`'s parallel write path (shares only
`Sample::initialize` + the adopt/reason primitives — do not change their contract).

## Target design — three layers

**Layer 1 — `AudioByteSource`** (`storage/audio/audio_byte_source.h`): a four-call forward byte stream
(`read` / `pos` / `seekForwardTo` / `size`) that hides *how* bytes are fetched and owns its own position +
cleanup. Replaces `AudioFileReader` + its subclasses.

**Layer 2 — typed parse result** (not yet done): split the `type`-flag parser into `parseSampleHeader` /
`parseWaveTableHeader` returning `std::expected<…, Error>` with `std::optional` fields (no `255` / `-1`
sentinels), container detection folded in; lower optionals to the legacy defaults at the apply site so
behaviour is identical.

**Layer 3 — collapse `getAudioFileFromFilename`** (not yet done): separate file-resolution (dedup lookup +
alternate-dir search + `f_open`) from object construction (`buildSample` / `buildWaveTable`) under an RAII
reason guard; delete dead code (`WaveTable::cloneFromSample` decl; the always-true `*error=NONE;
if(*error!=NONE) goto` stubs).

## Progress

### Done + validated
- **`util/audio_format_helpers.{h,cpp}`** — the parser's pure leaf helpers (`ConvertFromIeeeExtended`,
  `memToUIntOrError`, `swapEndianness*`, `charsToIntegerConstant`) extracted out of the heavy `functions.h`
  (which now re-exports them, so no caller changed), making the parser a standalone, host-testable unit.
- **`AudioByteSource`** interface; `parseAudioFileHeader` retargeted onto it. The AIFF MARK cursor poke
  became `seekForwardTo(pos()+n)`.
- **`ClusterByteSource`** (`storage/audio/cluster_byte_source.{h,cpp}`) — streams a Sample's clusters and
  releases the held cluster's reason in its **destructor** (RAII), replacing `SampleReader` + the loader's
  `ensureSafeThenCheckError` goto cleanup. `SampleReader` retired.
- **`AudioFile::loadFile`** now takes `AudioByteSource&` (+ a transitional `WaveTableReader*` `wtReader`
  seam used only by the WaveTable path). The sample branch of `getAudioFileFromFilename` lost the
  `char readerMemory[sizeof(SampleReader)]` placement-new hack and the external reader field-poking.
- **Latent UB fix:** `WaveTable::setup` declared `Error error;` uninitialised (only set in a `catch`); a
  successful `bands.resize` left it indeterminate before `if (error != Error::NONE)`. Now `= Error::NONE`.

### Regression nets (run these before/after each subsequent change)
- **Parser descriptor spec** — `tests/spec/audio_file_format_spec.cpp`: feeds hand-crafted WAV/AIFF byte
  buffers through an in-memory `AudioByteSource` to `parseAudioFileHeader` and asserts each descriptor field
  against a first-principles value (independent of the implementation). Covers 8/16/24/32-bit, mono/stereo,
  float, `smpl` loops, truncation, the `clm ` wavetable path, stereo-wavetable rejection, and AIFF with the
  80-bit IEEE-extended sample rate (the corpus's gap).
  Run: `cmake -B build-tests -S tests && cmake --build build-tests --target all_specs && ctest --test-dir build-tests -R audio_file_format_spec`
- **`deluge_loadcheck`** (`src/bsp/host/host_loadcheck_main.cpp`) — Tier-2 host driver that loads files
  through the *real* path (`getAudioFileFromFilename` → `ClusterByteSource` → parse → buildSample) off a
  packed FAT image and prints each parsed descriptor. Validates the construction path end-to-end (FAT walk +
  cluster streaming + parse). Needs `mtools`.
  Run: `cmake --build build-sim --target deluge_loadcheck && ./build-sim/deluge_loadcheck --project <dir-with-SAMPLES/> --file SAMPLES/X.WAV --wavetable SAMPLES/WT.WAV`
  (16-bit mono/stereo + 8-bit mono samples and a single-cycle wavetable all verified loading correctly.)

The full 296-preset render golden sweep (`feat/synth-golden-masters`) is the belt-and-suspenders, deferred
to merge.

## What's next

1. **WaveTable byte-source rework (the gnarly one).** `WaveTable::setup`'s file branch (`wave_table.cpp`
   ~340–520) keeps its own `clusterIndex` / `byteIndexWithinCluster`, pokes `reader->byteIndexWithinCluster`,
   and does **zero-copy** reads straight out of the global `smDeserializer.fileClusterBuffer` (using the
   reader only to trigger `f_read` via `advanceClustersIfNecessary`). Write a `DeserializerByteSource`, move
   `setup`'s data read onto it, then retire `WaveTableReader`, the `ReaderByteSource` adapter
   (`storage/audio/reader_byte_source.h`), and the `wtReader` seam in `AudioFile::loadFile`. Extend
   `deluge_loadcheck` with wavetable fixtures and confirm green first. WaveTable currently loads correctly on
   the legacy reader, so this is cleanup, not a correctness gap.
2. **Layer 2 — typed parse result** (`SampleHeader` / `WaveTableHeader` + `std::expected`).
3. **Layer 3 — collapse `getAudioFileFromFilename`** (resolution vs construction split, RAII reason guard,
   delete dead code).
4. **(later, optional)** deliberate quirk fixes / dead AIFF-path removal — each its own change.

## Key files

- `src/deluge/storage/audio/audio_byte_source.h` — the interface.
- `src/deluge/storage/audio/cluster_byte_source.{h,cpp}` — sample path (done).
- `src/deluge/storage/audio/reader_byte_source.h` — transitional adapter (delete after the WaveTable rework).
- `src/deluge/storage/audio/audio_file_format.{h,cpp}` — `AudioFileFormat` descriptor + `parseAudioFileHeader`.
- `src/deluge/storage/audio/audio_file.{h,cpp}` — `AudioFile::loadFile` (parse + apply dispatch).
- `src/deluge/storage/audio/audio_file_manager.cpp` — `getAudioFileFromFilename` (Layer-3 target).
- `src/deluge/storage/wave_table/wave_table.cpp` — `WaveTable::setup` (next-step target).
- `src/deluge/util/audio_format_helpers.{h,cpp}` — dep-free byte/format helpers.
- Nets: `tests/spec/audio_file_format_spec.cpp`, `src/bsp/host/host_loadcheck_main.cpp` (+ `sim/CMakeLists.txt`).
