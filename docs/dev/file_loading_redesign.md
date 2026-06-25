# Audio file-loading redesign

Status: **structural redesign complete** on `feat/audio-file-parser`, pending the merge-time golden sweep.
All three layers are done and runtime-validated: the byte-source abstraction (Layer 1, retiring the
`AudioFileReader` hierarchy), the typed sentinel-free `std::expected` parse result (Layer 2), and the
collapse of `getAudioFileFromFilename` into `resolveFilePointer` + `convertSampleToWaveTable` +
`buildAudioFileFromCard` (Layer 3). Behaviour-preserving throughout (no intended change to what loads or how
it sounds). Only deliberate quirk-fixes (optional) and the full render-golden sweep at merge remain.

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

**Layer 2 — typed parse result** (done): the `type`-flag `parseAudioFileHeader` is split into
`parseSampleHeader` / `parseWaveTableHeader` returning `std::expected<SampleHeader / WaveTableHeader, Error>`
with `std::optional` fields (the `255` byte-depth and `-1` midi-note sentinels are gone), and RIFF/FORM
container detection folded in (the parser reads the 12-byte top header itself). A shared internal
`walkChunks` over a private `HeaderFields` accumulator keeps the proven chunk logic single-sourced; the two
public entry points lower it into the tight typed results. `AudioFile::loadFile` lowers the optionals to
Sample's legacy defaults at the apply site, so behaviour is identical.

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
- **`DeserializerByteSource`** (`storage/audio/deserializer_byte_source.{h,cpp}`) — the WaveTable path. Pulls
  the file in `Cluster::size` blocks via `f_read` into `smDeserializer.fileClusterBuffer`. It serves *two*
  surfaces: the byte-stream `AudioByteSource` (header parse) and a lower-level cluster view
  (`clusterBuffer()` / `byteIndexWithinCluster()` / `advanceClustersIfNecessary()`) that `WaveTable::setup`'s
  **zero-copy** band-build loop reads misaligned 32-bit words out of directly. `WaveTable::setup`'s file
  branch now drives that source instead of poking a `WaveTableReader`'s cluster cursor. Retired the whole
  `AudioFileReader` hierarchy: `AudioFileReader`, `WaveTableReader`, and the transitional `ReaderByteSource`
  adapter. (The in-memory `sample != nullptr` setup branch — convert an existing Sample → WaveTable — takes
  no source and is unchanged.)
- **`AudioFile::loadFile`** now takes `AudioByteSource&` (+ a `DeserializerByteSource*` `wtSource` seam used
  only by the WaveTable path — the same object as `source`, passed concretely because the band loop needs the
  cluster accessors). The sample branch of `getAudioFileFromFilename` lost the
  `char readerMemory[sizeof(SampleReader)]` placement-new hack; the WaveTable branch lost the manual
  `WaveTableReader` field-poking (`currentClusterIndex`/`byteIndexWithinCluster`/`fileSize` set-up).
- **Latent UB fix:** `WaveTable::setup` declared `Error error;` uninitialised (only set in a `catch`); a
  successful `bands.resize` left it indeterminate before `if (error != Error::NONE)`. Now `= Error::NONE`.
- **Latent OOB fix:** the `clm ` (Serum wavetable) parse formed its end pointer as `&data[7]` on a
  `std::array<char,7>` — one past the end (UB; trips bounds-checked `operator[]`). Now `data.data() +
  data.size()`. Behaviour-identical on hardware; was only reachable via an explicit-wavetable `clm` file
  (the prior single-cycle fixture had no `clm` chunk, so the descriptor net's new multi-cluster fixture
  surfaced it).
- **Layer 2 — typed parse result:** `parseAudioFileHeader(src, type, atAllCosts, isAiff, out&)` → two typed
  entry points `parseSampleHeader(src)` / `parseWaveTableHeader(src, atAllCosts)` returning
  `std::expected<SampleHeader / WaveTableHeader, Error>`. Killed the `AudioFileFormat` POD and its sentinels
  (`byteDepth == 255` → an internal `std::optional<uint8_t>`; `midiNoteFromFile == -1` → `std::optional<float>`
  lowered to `-1` at the apply site). Folded RIFF/FORM container detection into the parser (a `detectContainer`
  reading the 12-byte top header), so the manager's `readTopHeaderAndLoad` helper and the `isAiff` flag through
  `loadFile` are gone. The chunk walk stays single-sourced as a private `walkChunks` over a `HeaderFields`
  accumulator; the two public functions lower it. Net + loadfile descriptors/CRCs bit-identical.
- **Layer 3 — collapse `getAudioFileFromFilename`** (4 bisectable commits): deleted the always-true
  `*error=NONE; if(*error!=NONE) goto/return` stubs + the declaration-only `WaveTable::cloneFromSample`; then
  extracted three private helpers — `buildAudioFileFromCard` (size checks, alloc+adopt, the Sample FAT-walk +
  `ClusterByteSource` parse / the WaveTable `DeserializerByteSource` parse, insert, finalize, reason
  lifecycle), `convertSampleToWaveTable` (the in-memory Sample→WaveTable branch), and `resolveFilePointer`
  (the supplied-pointer / alternate-dir / regular-`f_open` resolution state machine). Each helper's local
  gotos became early returns (`cantLoadFile`/`ramError`/`audioFileError`, and `notLoadableAsWaveTable`/
  `waveTableCloneError`). `getAudioFileFromFilename` is now a linear lookup → resolve → build (~90 lines, down
  from ~390). Behaviour-preserving — including the legacy reason asymmetry in the conversion's setup-error
  path, now flagged with an NB comment. Descriptors/CRCs bit-identical.

### Regression nets (run these before/after each subsequent change)
- **Parser descriptor spec** — `tests/spec/audio_file_format_spec.cpp`: feeds hand-crafted WAV/AIFF byte
  buffers through an in-memory `AudioByteSource` to `parseSampleHeader` / `parseWaveTableHeader` and asserts
  each header field against a first-principles value (independent of the implementation). Covers 8/16/24/32-bit,
  mono/stereo, float, `smpl` loops, truncation, the `clm ` wavetable path, stereo-wavetable rejection, and AIFF
  with the 80-bit IEEE-extended sample rate (the corpus's gap).
  Run: `cmake -B build-tests -S tests && cmake --build build-tests --target all_specs && ctest --test-dir build-tests -R audio_file_format_spec`
- **`deluge_loadcheck`** (`src/bsp/host/host_loadcheck_main.cpp`) — Tier-2 host driver that loads files
  through the *real* path (`getAudioFileFromFilename` → `ClusterByteSource`/`DeserializerByteSource` → parse →
  build) off a packed FAT image and prints each parsed descriptor. For a `--wavetable` it dumps the decoded
  structure (`numCycles`, per-band `cycleSize`/`mag` + a **CRC32 of each band's data**) — a fingerprint of
  `setup`'s read-then-FFT pipeline, so a regression in the cluster-streaming data read shows as a CRC diff.
  Validates the construction path end-to-end (FAT walk + cluster streaming + parse). Needs `mtools`.
  Run: `cmake --build build-sim --target deluge_loadcheck && ./build-sim/deluge_loadcheck --project <dir-with-SAMPLES/> --file SAMPLES/X.WAV --wavetable SAMPLES/WT.WAV`
  Fixtures (not committed — regenerate into `<project>/SAMPLES/`): 16-bit mono/stereo + 8-bit mono samples; a
  single-cycle wavetable; and a **multi-cluster** wavetable (mono 16-bit, `clm `=2048, 64 cycles ⇒ ~256 KB
  ⇒ ~9 × 32 KB clusters) that exercises the cross-cluster `f_read` advance in the reworked `setup` loop.
  Baseline (post-rework, x86): the 64-cycle fixture loads `numCycles=64 numBands=9`, band0 `crc=F88B7C76`
  … band8 `crc=B1F07F28`; descriptors of the three samples are unchanged from the pre-rework run.

The full 296-preset render golden sweep (`feat/synth-golden-masters`) is the belt-and-suspenders, deferred
to merge.

## What's next

1. **Layer 3 — collapse `getAudioFileFromFilename`** (resolution vs construction split, RAII reason guard,
   delete dead code). Also the natural home for fully dissolving the `wtSource` seam: the WaveTable path's
   source is *always* a `DeserializerByteSource`, so a dedicated `buildWaveTable` could split `setup` into
   `setupFromSample` (in-memory) vs `setupFromFile(DeserializerByteSource&, …)` and drop the shared
   `setup(Sample*, …, source)` overload's nullable-source soup.
2. **(later, optional)** deliberate quirk fixes / dead AIFF-path removal — each its own change.

## Key files

- `src/deluge/storage/audio/audio_byte_source.h` — the interface.
- `src/deluge/storage/audio/cluster_byte_source.{h,cpp}` — sample path (done).
- `src/deluge/storage/audio/deserializer_byte_source.{h,cpp}` — wavetable path (done; byte stream + cluster view).
- `src/deluge/storage/audio/audio_file_format.{h,cpp}` — `SampleHeader`/`WaveTableHeader` + `parseSampleHeader`/`parseWaveTableHeader`.
- `src/deluge/storage/audio/audio_file.{h,cpp}` — `AudioFile::loadFile` (parse + apply dispatch; lowers optionals).
- `src/deluge/storage/audio/audio_file_manager.cpp` — `getAudioFileFromFilename` (Layer-3 target).
- `src/deluge/storage/wave_table/wave_table.cpp` — `WaveTable::setup` (now reads via `DeserializerByteSource`).
- `src/deluge/util/audio_format_helpers.{h,cpp}` — dep-free byte/format helpers.
- Nets: `tests/spec/audio_file_format_spec.cpp`, `src/bsp/host/host_loadcheck_main.cpp` (+ `sim/CMakeLists.txt`).
