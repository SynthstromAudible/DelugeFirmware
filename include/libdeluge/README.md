# `libdeluge`: the platform boundary

These headers are the C ABI between the portable Deluge application (`src/deluge`) and the platform that runs
it. One platform implements them: the Rust BSP (`src/bsp/rust`, on deluge-sdk's `deluge-bsp`), on the device and,
under the `host_app` feature, on the Embassy host that `dbt sim` and the gates run.

## Rules

1. **C, and only C, crosses.** Every declaration is inside `extern "C"`. Only fixed-width integers, `bool`, and
   POD structs and enums with an explicit underlying type cross. No STL, no exceptions, no allocation across the
   boundary. No function behind the ABI may throw or unwind: the Rust side aborts on panic, and a C++ definition
   catches at the boundary and returns its failure value. Every function declaration ends `DELUGE_NOEXCEPT`
   (`abi.h`), so the compiler holds every C++ definition to it. A failure that carries a reason crosses as
   `DelugeStatus`, a channel's collect answers a `DelugeChannelStatus` (`DELUGE_CHANNEL_*`), and a path is bounded
   by `DELUGE_PATH_MAX`.
2. **The header is the contract.** bindgen reads every header (`src/bsp/rust/wrapper.h` and
   `src/bsp/rust/harness/golden_vt_render/wrapper.h`, both just `#include <libdeluge/libdeluge.h>`), and every
   Rust export is pinned to its generated declaration by `abi_pin!` — beside the definition it pins (including the
   one rename form, `abi_pin!(yield_ => r#yield)` in `scheduler.rs`), and for another crate's own exports and
   imports, via its `in path:` form, in `src/bsp/rust/src/abi/pins.rs`. That file also holds the `abi_layout!`
   and `abi_const!` checks, except for `sys_host.rs`'s, which sit at the bottom of that file. A crate-defined
   type that crosses the boundary is re-exported into `sys` under the header's name and checked exhaustively,
   field-for-field or variant-for-variant, by `abi_layout!`. A copied constant is pinned by `abi_const!`. The
   pre-commit hook `python3 scripts/check_abi_surface.py --check --pins --tags --noexcept` fails when a declared
   function has no definition, when a Rust definition or `extern "C"` import of one has no pin that reaches it, or
   when a declaration lacks `DELUGE_NOEXCEPT`. Change a signature in the header, and the build tells you every place
   that disagrees.
3. **Direction is one-way.** The app includes `<libdeluge/…>`. The platform implements it. `app.h` is the other
   direction: callbacks the app implements and the platform calls. `scripts/check_platform_boundary.py` keeps the
   app from including platform headers.
4. **Call context is documented per function.** Each declaration's doc carries the tags of every context it may
   be called from; a function with several tags may be called from each of them. `scripts/check_abi_surface.py
   --tags` (in the pre-commit hook) fails any declaration without one.
   - `[audio]`: may be called from the audio tier, `AudioEngine::routine_task`'s call graph on the preemptive
     audio executor. That graph is more than the voices: it runs the sequencer's ticks, so sequenced note-ons,
     MIDI, CV and gate output happen there too. The goal for an `[audio]` function is that it never blocks,
     allocates or locks. Today some do: the mod-FX buffer is set up and released inside the render through
     `deluge_alloc`/`deluge_free`, a region acquire can take and release slab slots
     (`deluge_slab_acquire`/`deluge_slab_release`), the render takes `ENTER_CRITICAL_SECTION`, and a freeze in the
     render blocks in `deluge_fault_pad_flush`. The tags record these hazards; they do not fix them.
   - `[task]`: may be called from the non-audio tiers: the interaction tier (the cooperative executor's tasks, boot
     and the UI) and the storage tier. On the device the storage tier runs on `STREAM_EXEC`, an interrupt executor
     (IRQ mode), so `[task]` does not mean thread mode.
   - `[isr]`: may be called from an interrupt handler or a fault path.

   Before `registerTasks()`, the boot's song-less render runs `routine()` once on the task executor, so the
   functions `routine()` reaches directly (`deluge_audio_drive`, `deluge_app_render`, `deluge_app_render_skipped`,
   `deluge_heap_check`) also carry `[task]`. The render's deeper callees do not: the three `[audio]`-only ones
   (`deluge_audio_frames_until_block_offset` and `deluge_midi_gate_timer_arm`, reached only when MIDI or gate output
   is pending, and `deluge_sample_region_state`, reached only by a playing sample voice) need a song that boot has
   not loaded.

## Handles

Every table handle that crosses the ABI (an open file, directory or stream) is a `DelugeHandle` (`types.h`, a
`uint32_t`), and `DELUGE_HANDLE_NONE` (0) is none: no call hands it out, and a caller tests a stored handle against
it. The tables behind the handles index their slots from 0, so a slot crosses as `slot + 1`; the Rust exports
convert with `src/bsp/rust/src/abi/handle.rs`'s `to_abi`/`from_abi`, at the exports only; the tables themselves
work in slots. `sample_stream.h`'s registry numbers its handles the same way.

A call passed `DELUGE_HANDLE_NONE` does nothing and gives its bad-argument answer: a status-returning call (a stream
close included) answers `DELUGE_ERR_PARAM`, a close that returns nothing does nothing, a channel request answers
false and a channel collect answers `DELUGE_CHANNEL_UNKNOWN`. A non-zero handle that names no open row gets each call's own answer, which is not always
the same one (the streaming `deluge_efatfs_read_at` and `deluge_efatfs_stream_*` answer `DELUGE_ERR_IO`).

## Channels

Storage is reached through request/collect channels: a request returns at once, and a collect is polled once per
tick for the answer (`channel.h`'s `DelugeChannelStatus`). Every channel follows the same rules:
- answers are keyed, and a collect with another key answers UNKNOWN and leaves the answer for its owner;
- answers are stamped with the card generation (`card.h`'s `deluge_card_flush` bumps it for every channel at
  once), so after an ejection an old answer is never collected.

Two channels hold a caller-owned buffer across ticks, and each says whether the platform may still touch it:
`file_bytes.h`'s read-into and write-from answer `deluge_file_bytes_holds()`, and `file_stream.h`'s offset read and
write answer `deluge_file_stream_holds()`. Every other operation copies its bytes at the request or hands them back at
the collect that reports READY, so it needs no such call.

`file_prefix.h`, `file_directory.h`, `file_bytes.h`, `file_stream.h` and `audio_load.h` are the channels. Before
boot finishes (`deluge::boot::finished()`), the app uses blocking calls instead.

`streaming_fill.h` is not one of these: it is the sample-streaming fill queue (enqueue/drain of the loader
queue and fill-context registration), not a keyed request/collect channel.

## Host-only declarations

Declarations inside `#ifdef DELUGE_HOST` (the `*_set_latency_ms` knobs, `deluge_trigger_clock_host_edge`) exist
only in the Embassy host build, for the gates. A device image never defines them.
