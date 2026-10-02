# Known Concurrency Bugs — preemptive audio, storage and interaction tiers

**Status:** B1–B8 are all closed (fixed, or their race site no longer exists). What remains open is the set of
cross-tier race sites catalogued in `src/bsp/rust/harness/preemptive_race_tsan/races.txt`, summarised under
"Open" below.

## Scope

The firmware runs on three tiers: **audio** (a preemptive interrupt executor that never blocks), **storage** (Embassy
tasks serving the byte, write, directory and prefix channels and the async audio loader) and **interaction** (the
thread-mode C++ app). Before `deluge::boot::finished()` card I/O runs inline on its caller; after it, through the
channels. A race here needs one tier to preempt another mid-operation, so these bugs exist only on the Embassy BSP,
where audio is genuinely preemptive; on the cooperative BSP nothing preempts mid-operation.

The invariant every open entry breaks: state shared across tiers is owned by exactly one tier and handed over
explicitly.

## Reproducing

`./dbt test preemptive-race-tsan` (or `./run.sh` in `src/bsp/rust/harness/preemptive_race_tsan/`; its header
lists the prerequisites and knobs such as `NUM_RUNS`, `BLOCKS`, `DELUGE_SD_IMAGE`). It runs the real `deluge_app`
under ThreadSanitizer with audio, storage and interaction on separate OS threads, playing a song while recording.
Every race site it sees must have an entry in `races.txt`; an uncatalogued site fails the run. `triage.sh LOG...`
turns run logs into candidate `races.txt` rows. Races are timing-dependent, so an entry a run does not hit is
reported "not seen" rather than closed.

## Open

`races.txt` is the authoritative list, one entry per race site (file + function) with the shared state and the
racing tiers; its trailing index groups the sites by shared state. Every open site is audio × interaction, except
`deluge_debug_now_frames` (audio × storage). The groups:

- **`Sound::voices_`** — walked by the render while the interaction tier mutates it (`terminateOneVoice`, the
  `getNumVoices` lambda). The one group that has crashed a run (SEGV in 2 of 10 triage runs).
- **Render-routine globals** — `audioSampleTimer`, `routineBeenCalled`, `renderingBufferOutputPos`, `renderInStereo`,
  `bypassCulling`, `audioRoutineLocked`, read or written from both tiers.
- **Reverb** — `AudioEngine::reverb` and its parameters, updated by the interaction tier while the render runs it.
- **The current song and playback handler** — `currentSong`, `loadSongUI`, `playbackHandler`, `arrangement`, and
  the sequencer tick's play positions (`processCurrentPos` on clips, note rows, params), set by `setupPlayback`
  while the render ticks.
- **The UI navigation stack** — `uiNavigationHierarchy`, `numUIsOpen`, read by the render.
- **The recorder list and monitoring** — `AudioEngine::firstRecorder`, `audioRecorder`.
- **Arpeggiator settings** — written or cloned by the UI while the render reads them.
- **Memory the render read, then freed and reused by the interaction tier** — reported as a constructor writing
  heap-arena memory (`Song::Song`, `ModControllableAudio`, `AutoParam`, filter/delay/compressor constructors). These
  are object-lifetime races (freed with no handover from the render), not field races.
- **The debug log buffer** — `printf`'s `_out_buffer`.

One unguarded path that no run has reported: `Manager::release_asset` (`crates/deluge_resource/src/manager.rs`)
skips the masked access B1's fix applies everywhere else in `Manager`, so it is sound only while no other tier
touches that asset's chunks during teardown.

## Fixed

- **B1 — `deluge_resource::Manager` chunk table: unsynchronized `Cell<ChunkSlot>`.** Audio's lease release raced the
  loader's `loader_next` on the same slot, able to lose a lease count and evict a playing cluster. Every `Manager`
  access now goes through `crates/deluge_resource/src/sync.rs`'s `Masked` guard (`m_get`/`m_set`/`m_rmw`), which
  masks on every context except the audio render. Fixed: `a04e1a2d5`, `521a01d65`, `c1fc2b1cf`, `41ef30dcd`,
  `ac3cfeb78`.
- **B2 — recorder cluster table reallocated under a concurrent reader.** Now `SampleRecorder::bufferTable_`, a
  stable-address `deluge::SegmentedVector` (`src/deluge/util/segmented_vector.h`), with `currentRecordClusterIndex` a
  release/acquire `std::atomic` (`src/deluge/model/sample/sample_recorder.h`). Fixed: `4be0286ae`, `f557d41e4`.
- **B3 — record payload buffer written by audio while the card write read it.** The B2 index hand-off orders each
  completed buffer's payload before the write that drains it; `status` is atomic for the finalize hand-over. Fixed:
  `17f4a692d`, `086584360`.
- **B4 — worker-ring priority queue could starve NORMAL jobs.** Fixed: `7188cef4c` (priority tier deleted); the ring
  itself went with the fiber, `af4c28e3d`.
- **B5 — unsynchronized `Q_COUNT` / ring links in the worker fiber's queue.** The race site, `fiber.rs`'s
  `enqueue`/`queue_nonempty`, no longer exists: the fiber and its queue were deleted in `af4c28e3d`.
- **B6 — `CLUSTER_LOAD_IMMEDIATELY` and the file-load family doing card transfers outside the single storage owner.**
  The hazard was two contexts entering the non-reentrant C FatFS. C FatFS is deleted (`60af3ddf7`); every efatfs
  call serialises on the `FS` mutex (`src/bsp/rust/src/efatfs_fs.rs`, `with_fs`) and every transfer on `SD_BUS`
  (`src/bsp/rust/src/sd.rs`), from any context. A synchronous post-boot call from the interaction tier is legal and
  holds the UI for the transfer; `src/bsp/rust/src/tier_meter.rs` inventories where that still happens.
- **B7 — efatfs block device bypassed `SD_BUS`.** `SdBlockDevice::read`/`write`
  (`src/bsp/rust/src/fat_block_device.rs`) go through `crate::sd::locked_read_sectors`/`locked_write_sectors`.
  Fixed: `3c3fd1562`.
- **B8 — clip delete could free the `AudioClip` an undo/redo revert was mid-load on.** The window was the SD yield
  inside `ConsequenceAudioClipSetSample::revert`. `deluge::gui::Reversion` (`src/deluge/gui/ui/reversion.h`) loads
  the files first and then runs the reversion card-free in one pass, so `revert()` no longer yields. Fixed:
  `7e13a061a`.
- **Heap arena (TLSF) and MEM_GUARD side table** entered by two tiers at once. Fixed: `d4f53b0fe`.
- **Harness underrun instrument** missed `SampleLowLevelReader::moveOnToNextCluster`, the common miss site. Fixed:
  `fb0dc5e3a`.
