# The libdeluge C ABI overhaul

Status: implemented. Phases 1-4 are merged into `next`; phase 5 has landed on its branch. What remains open is
listed in section 5. Five phase plans implement it:

1. `docs/superpowers/plans/2026-09-29-libdeluge-abi-p1-bugs.md`: live defects the audits found. Branch
   `fix/libdeluge-abi-bugs`, merged into `next` (a20e0ff82).
2. `docs/superpowers/plans/2026-09-29-libdeluge-abi-p2-dead-surface.md`: delete everything no one calls. Branch
   `chore/libdeluge-dead-surface`, merged into `next` (1d4d303ce).
3. `docs/superpowers/plans/2026-09-29-libdeluge-abi-p3-header-contract.md`: bindgen checks the whole surface.
   Branch `chore/libdeluge-header-contract`, merged into `next` (307705272).
4. `docs/superpowers/plans/2026-09-29-libdeluge-abi-p4-channels.md`: one channel shape on both sides. Branch
   `refactor/libdeluge-channels`, merged into `next` (7d07f5f31).
5. `docs/superpowers/plans/2026-09-29-libdeluge-abi-p5-shape.md`: the remaining structural cleanups. Branch
   `refactor/libdeluge-shape`, off `next` at 7d07f5f31.

Each phase ships on its own and leaves `./dbt test` green; later phases assume the earlier ones landed.

## 1. What the ABI is

`include/libdeluge/*.h` (37 headers) is the C boundary between the portable C++ app (`src/deluge`) and the
platform. With the legacy RZA1 BSP and the C host retired, one platform implements it: the Rust BSP
(`src/bsp/rust`, `#[unsafe(no_mangle)]` exports), with a few symbols in in-tree crates (`crates/deluge_*`). It runs
on the device and, under `host_app`, on the Embassy host (the gates and `dbt sim`).

The surface still carries the shape it needed when three platforms implemented it: feature probes, weak fallbacks,
hand-copied constants, and one hand-written channel per file operation.

## 2. Findings

Two audits, run 2026-09-29: an inventory (every declared function: definition, callers, bound or not) and a design
review. Everything below was checked against the code.

### A. Defects

- **A1: the trigger-clock input is dead.** `playback_handler.cpp` polls `triggerClockRisingEdgesReceived`, which
  only `ffi_extra.rs` defines, as a zero that nothing writes. Nothing calls `deluge_trigger_clock_set_handler`, so
  `cv_gate.rs`'s `trigger_clock_task` counts edges and drops them. Analog clock sync does nothing on the Rust BSP.
- **A1b: capture stamps are noise.** `deluge_audio_stamp_to_render_offset` (`audio.rs`) masks `stamp - TX_WRITE`,
  but the producers pass Embassy `Instant` ticks (`midi.rs` `deluge_midi_din_read_timed`, and the clock task's
  `LAST_EDGE_TICKS`). The result is a pseudo-random offset in 0..127 on every MIDI clock tick: up to a block of
  jitter.
- **A3: the write channel's answer slot is keyed too loosely and survives an ejection.**
  - `file_write.rs` `op_key` keys `ReadFile`/`WriteFile`/`Open` by kind alone. The `_path` arguments to their
    collects are ignored.
  - Answers carry no card generation, and `deluge_card_flush` (`card.rs`) does not flush this channel, so an answer
    from before an ejection is collected after it.
  - An `Open` answer that `accept` discards leaks its stream handle: the table has 8 slots, and nothing closes it.
- **A3b: `FileTimestamp` crosses the ABI with Rust layout.** `file_bytes.rs` passes it by value through
  `deluge_file_bytes_request_set_time`, mirroring `DelugeTimestamp` (`types.h`) with no `#[repr(C)]`. The doc
  comment claims `file_bytes_abi.cpp` pins it; nothing does.
- **A2: legacy USB leftovers.**
  - `ffi_extra.rs` exports the `openUSBHost`/`closeUSBHost`/`openUSBPeripheral`/`closeUSBPeripheral` stubs and the
    `anythingInitiallyAttachedAsUSBHost` global, and the NE10 stubs return 0.
  - `deluge.cpp:221-224,426-431` declares them.
  - `usbLock`'s linkage differs between `deluge.cpp` and `resource_checker.h:22`.
- **Width mismatch:** `services.rs:172` returns `i32` for `deluge_platform_init`, which the header types as
  `DelugeStatus` (`int8_t`). The function is dead (P2 deletes it).

### B. The channels

`audio_load`, `file_prefix`, `file_directory`, `file_bytes`, `file_write` and `streaming_fill` implement one idea
(request, then poll a status of PENDING/READY/UNKNOWN/FAILED; answers stamped with a card generation; `card` flush)
six times. Each has its own:

- status macros,
- `MAX_PATH` spelling (`DELUGE_MAX_PATH`, `DELUGE_FILE_DIRECTORY_MAX_PATH`, `DELUGE_FILE_BYTES_MAX_PATH`, and
  `file_write.rs`'s private 255),
- `DELUGE_NOEXCEPT` definition,
- Rust slot, and
- C++ pending machine with its own 600-tick stall constant.

There are 14 stall constants: 12 at 600 ticks, `kMaxStagingWaitTicks` at 2000 and `file_reader.cpp`'s `uint16_t`.

- **B1:** one generic Rust answer slot.
- **B2:** one `libdeluge/channel.h` for the shared vocabulary.
- **B3:** one C++ stall bound.
- **B4:** `file_write` is two channels in one. Split it: the handle-keyed stream operations stay, and the whole-file
  operations join the path-keyed byte channel.
- **B5:** every channel answers `holds(buffer)`.
- **B6:** FAILED always carries a `DelugeStatus`.

### C. Leftovers from having several platforms

- **C1:** five `deluge_*_async_active()` functions answer a constant `true`, and 39 sites ask them, including four
  scenario markers.
- **C2:** `deluge::boot::finished()` is the one real selector between the blocking and channel paths; say so once.
- **C4:** the `efatfs_streaming` and `async_streaming_loader` cargo features are in `default` and nothing builds
  without them. Make them unconditional.
- **C5:** comments still describe a C++-only build linking weak definitions (for example `file_bytes.rs:691`).

### D. The headers

- **D1:** bindgen reads 16 of the 35 headers (`wrapper.h`), and only for types (`allowlist_type("Deluge.*")`). No
  function signature or `#define` constant is checked from the Rust side. The Rust side copies constants by hand,
  and the `*_abi.cpp` `static_assert`s cover a few of them.
- **D2:** the umbrella `libdeluge.h` includes 11 headers.
- **D3:** `include/libdeluge/README.md` still names `~/GitHub/deluge-embassy` as a future BSP, and says nothing
  about the channels, the bindgen contract or handles. The `[task]`/`[audio]`/`[isr]` context tags it defines are
  applied unevenly.
- **D4:** `streaming_fill.h` mixes three concerns, and there are two handle conventions: 0 is invalid in
  `sample_stream.h`, but slot indices from 0 are valid in `file_write`. The `DelugeFile`/`DelugeDir`/`DelugeStream`
  typedefs are unused.
- **D5:** enums are spelled three ways. The target is `typedef enum X : uint8_t`.
- **D6:** `scheduler_api.h` is part of the ABI but lives in `src/OSLikeStuff`.
- **D7:** `deluge_boot_mark_finished` is exported to C and called from C++ only.

### E. Types

- **E1:** errors are spelled four ways: `DelugeStatus`, bare `bool`, a status byte with a `u32` side channel, and
  `int` codes. The target is `DelugeStatus`.
- **E2:** statuses come in `int8_t`, `uint8_t` and `int` widths. The target is `DelugeStatus` for errors and
  `DelugeChannelStatus` (`uint8_t`) for channel polls.
- **E3:** there is no single path bound. `DELUGE_PATH_MAX` = 256 includes the NUL. The C++ side takes
  `std::string_view` and terminates the string at one place.
- **E4:** `noexcept` is applied unevenly. Every ABI function becomes `DELUGE_NOEXCEPT`.
- **E5:** handles are `void*`. They become opaque struct pointers (`typedef struct DelugeStreamHandle_*
  DelugeStreamHandle`) or typed `uint32_t` newtypes.

### F. Other

- **F1:** Rust crates link against C++ symbols that no header declares (`deluge_streaming_define_asset`,
  `deluge_debug_now_frames`, and others).
- **F2:** the `*_abi.cpp` pin files. P3's bindgen pins replace them.
- **F3:** the context tags. Some headers have none, so which tier a function may be called from is not written
  down. (The audit also suggested splitting the `tier` meter. It counts one thing per tier, interaction per
  entry and audio in total, and stays as it is.)
- **F4:** `card.rs` hand-lists the channels it flushes.
- **A4:** half of `audio_load.h` is C++ calling C++ through the ABI (`audio_load_abi.cpp`). That half moves to a
  C++ header.
- **A5:** C++ functions that Rust calls are neither declared in a header nor `noexcept`.

### The dead surface (P2)

The inventory's dead list was re-checked by `scripts/check_abi_surface.py --dead` (P2 Task 1), which counts callers
in the app, the tests and scenarios, and Rust. These have no caller outside mocks of themselves, or are never
defined:

| Header | Functions |
|---|---|
| audio_io | `stop`, `max_block_frames`, `sample_rate`, `output_latency_frames` |
| clock | `ticks_per_second`, `monotonic_hz`, `monotonic` (a unit-test mock only) |
| memory | `external_end`, `internal_begin` (mocks only), `cache_line_size`, `cache_clean`, `cache_invalidate` |
| midi_io | `port_kind`, `read`, `port_connected` |
| scheduler_api | `addOnceTask`, `getAverageRunTimeforCurrentTask`, `setNextRunTimeforCurrentTask`, `removeTask`, `boostTask`, `yieldWithTimeout`, `yieldToIdle` |
| sample_reader | `reserve_asset`, `reserve_covered_count`, `reserve_covered_index`, `reserve_leased_count` |
| streaming_fill | `deluge_streaming_chunk_ready` |
| file_io / stream_io | `efatfs_file_read` (called only by its mock's own spec), `efatfs_file_truncate` (a mock only), `efatfs_is_mounted`, `efatfs_stream_flush` |
| tier | `sync_total` |
| system | `platform_init` |
| display | `busy` |
| control_surface | `set_pad` |
| app | `deluge_app_on_event` (declared, never defined) |
| block_device | `init`, `sector_count`, `sector_size`, `sync` (never defined) |
| boot | `deluge_boot_mark_finished` (D7: its only caller is Rust) |

- `sd.rs` still exports FatFs diskio (`disk_*`, `get_fattime`), which nothing links.
- The orphaned headers are `src/OSLikeStuff/task_scheduler/task.h` and `resource_checker.h`.
- **Kept, because something calls them:**
  - `getAverageRunTimeForTask` (`audio_engine.cpp`);
  - `reserve_open`/`reserve_move` (`sample_holder.cpp`, `time_stretcher.cpp`);
  - the other `tier_sync_*` functions (four scenarios);
  - `efatfs_unmount` (`song_preview_ab` ejects the card with it);
  - `deluge_block_read`/`write`, `deluge_sample_stream_get_asset_id`, `deluge_control_poll_resume` and
    `deluge_system_reset`, which Rust or the app still calls;
  - the `*_set_latency_ms` hooks (the scenarios).
- **Kept, pending the allocator redesign:** the whole of `alloc.h`. The unit tests and `container_guard` call part
  of it, and the TLSF/slab redesign decides the rest.
- **To investigate:** `ffi_extra.rs` exports NE10 FFT/FIR stubs that return 0. P2 establishes which definition the
  device image links.

## 3. Decisions

1. **Capture stamps are TX play frames.** A capture stamp is the TX ring's play position at the moment of capture.
   The BSP makes it from the play position when it reads the event, minus the frames elapsed since the event:
   `capture_stamp(play_now, now_ticks, event_ticks)`. `deluge_audio_stamp_to_render_offset(stamp)` returns
   `(stamp - BLOCK_START_PLAY) & TX_MASK`, which is how far past the current block's start the event fell. That puts
   a constant block of latency on every event and leaves the spacing between events exact. The app's
   `+ 40 & (SSI_TX_BUFFER_NUM_SAMPLES - 1)` is unchanged. On the Embassy host the conversion answers 0 ("now"), as
   it does today: its render is paced by the audio device, and there is no play position to read.
2. **The clock-in edges land in an app-owned ring.** The app installs a `noexcept` handler that writes into a
   4-entry ring of atomics in `playback/trigger_clock_input.cpp`. `routine()` drains the ring. The
   `ffi_extra.rs` globals are deleted.
3. **Write-channel answers are keyed by (kind, handle, path) and stamped with the card generation.**
   - `ReadFile` and `WriteFile` are keyed by their path; their collects already receive it.
   - `Open` stays keyed by kind alone. The channel is single-flight, and the recorder's abort path collects an
     open without knowing which of two paths it was for. P4 keys it by path when `file_stream` replaces this
     channel.
   - A collect with the wrong key answers UNKNOWN and leaves the answer alone.
   - An answer from an earlier card is removed and answers UNKNOWN.
   - The card flush drops queued requests and waiting answers, but never the running operation: its buffer is
     still in use, and `holds()` must keep saying so.
   - Every successful `Open` answer that is dropped without being collected (displaced, discarded, stale or
     flushed) queues its handle, stamped with its card generation; the task closes it only while that card is
     still the current one, because a remount's table reset frees the old card's handles and reuses their
     indices.
4. **Delete before binding.** P2 removes the dead surface before P3 binds everything, so nothing dead gets a pin.
5. **Bindgen is the contract.**
   - `wrapper.h` includes the umbrella `libdeluge.h`, which includes the other 36 headers.
   - bindgen emits functions (`allowlist_function("deluge_.*")`) and constants (`allowlist_var("DELUGE_.*")`) as
     well as types.
   - Every Rust export is pinned against its generated declaration by `abi_pin!`, which fails the build on any
     mismatch in signature.
   - Rust constants are re-exported from `sys` where the module only compiles with bindgen output. A module that
     also compiles without it (the plain host tests, `src/main.rs`'s plain host build) keeps its own copy, and
     each copy is pinned to `sys` by `abi_const!`.
   - The `*_abi.cpp` pin files go.
   - Carriers without bindgen (the plain host tests) use `sys_host.rs`, whose types are pinned to `sys` on
     `host_app` builds.
6. **One channel vocabulary.**
   - `libdeluge/channel.h` holds `DelugeChannelStatus` (a `uint8_t`, with PENDING/READY/UNKNOWN/FAILED as
     integer macros, so bindgen evaluates them and the per-channel aliases),
     `DELUGE_PATH_MAX` (256, including the NUL) and `DELUGE_NOEXCEPT`.
   - Per-channel macros become aliases for one release of the tree, then go (P4).
7. **One Rust slot, one C++ stall bound.**
   - `crates/deluge_channel` provides `AnswerSlot<K, V>`: publish, take and drain, with generation stamps and the
     key-mismatch-leaves-alone rule.
   - `deluge::io::StallBound` counts caller ticks. `kChannelStallTicks = 600`; `kMaxStagingWaitTicks` keeps 2000,
     spelled as a multiple.
8. **`file_write` becomes `file_stream`.** Handle-keyed open, read_at, write_at, truncate, close and stats. Its
   whole-file read and write join `file_bytes`, extended with a caller-buffer mode.
9. **The async_active probes go.** Their scenario markers (`asyncActive=1`) go with them. `deluge::boot::finished()`
   stays the selector between the blocking and channel paths, and its declaration says so.
10. **Out of scope:** the website (it will be rewritten for the new bootloader), `faster-than-real-time export`,
    the model line.

## 4. Evidence

- Pure Rust logic: `cargo test --test <x>_host` in `src/bsp/rust`, or `cargo test -p <crate>` in `crates`.
- C++ pure logic: CppUTest in `tests/unit/cpputest` (`./dbt test unit`).
- Behaviour: the scenario gates (`./dbt test`). The gates that exercise each channel:
  - song-preview-ab: prefix, directory;
  - settings-channel: bytes;
  - document-save-channel and song-save-channel: write;
  - recorder-roundtrip: stream write;
  - sample-* and preset-load: audio_load.
- The device image must link: `./dbt build RelWithDebInfo` with `src/bsp/rust/local-sdk.toml` moved aside, then
  `git checkout -q src/bsp/rust/Cargo.lock`.
- Hardware never gates a phase.

## 5. Open follow-ups

The overhaul leaves these open. None is ABI shape; each is behaviour behind the ABI.

- **P4's parked channel behaviours.** The progress self-feed, the SysEx scaling, the recorder's folder retry, and a
  caller's own `write_from` that can stay PENDING with no bound.
- **Audio-tier hazards.** An `[audio]` function should never block, allocate or lock, and some still do: the mod-FX
  buffer is set up and released inside the render through `deluge_alloc`/`deluge_free`, a region acquire can take and
  release slab slots (`deluge_slab_acquire`/`deluge_slab_release`), the render takes `ENTER_CRITICAL_SECTION`, and a
  freeze in the render blocks in `deluge_fault_pad_flush`. `include/libdeluge/README.md` lists them; the tags record
  them, they do not fix them.
- **The blocking recorder arm's NOT_FOUND to MakeFolder step is untested.** `SampleRecorder`'s synchronous create
  path retries through `CreatePhase::MakeFolder` when `deluge::io::Stream::open` answers `NOT_FOUND`; no gate drives
  that branch.
