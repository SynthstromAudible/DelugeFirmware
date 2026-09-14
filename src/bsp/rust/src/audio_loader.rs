//! The Rust async audio-file load task: the AWAITED-I/O half of a card sample's
//! load — open a candidate path, resolve its size, walk its header — publishing
//! a plain result the C++ side collects on its own tier.
//!
//! `AudioFileManager::buildAudioFileFromCard`'s SAMPLE path is a prologue
//! (allocate + adopt a `Sample`), some I/O (pick and open a path, size it, open
//! the Sample's own read stream, walk the header), and an epilogue (apply the
//! parsed geometry, publish). **Only the I/O lives here.** This module makes no
//! call back into C++ at all: the model half runs on the C++ tier, synchronously,
//! once the bytes are already in hand.
//!
//! ## Why the split is shaped exactly this way
//!
//! Two constraints force it, and between them they rule out every simpler
//! arrangement:
//!
//! 1. **This task cannot run on the thread-mode executor.** It holds the single
//!    `efatfs` FS mutex across an SD await ([`crate::efatfs_fs::with_fs`] holds
//!    `FS.lock()` for the whole closure, awaits included), while the C++
//!    `deluge_efatfs_*` entry points bridge to those same futures through
//!    `sim_block::block_on` — a NON-yielding spin on device. As that module's own
//!    FFI-bridge note puts it: "the calling context is HELD for the duration of
//!    the transfer rather than yielded, so thread-mode tasks (PIC pump, OLED,
//!    UI) do not run during one." A thread-mode spin would therefore wait forever
//!    on a mutex only the task it is starving can release.
//! 2. **Running on its own interrupt executor instead, it must never make a
//!    synchronous C++ call.** The mirror-image hazard: those calls spin rather
//!    than await, and a spin at interrupt priority *cannot* be preempted by the
//!    thread-mode context holding the mutex — so it deadlocks the other way
//!    round. Separately, a C++ call here may allocate through
//!    `GeneralMemoryAllocator` or mutate model state while having preempted
//!    thread-mode mid-mutation.
//!
//! The single rule that satisfies both: **everything on this executor awaits;
//! nothing spins, and nothing calls into C++.** `streaming_fill_task` already
//! obeys it — that is why *it* can live here.
//!
//! That is why [`LoadOps`] is only four operations, all `async`, all filesystem.
//! It is also why the candidate list arrives as data in the request rather than
//! being asked for: `deluge_audio_load_resolve_alternate_dir` probes a directory
//! (`Directory::open` -> `deluge_efatfs_dir_open` -> a spin) and
//! `deluge_audio_load_candidates` allocates `std::string`s, so both must run on
//! the requesting tier.
//!
//! ## What the split buys beyond safety
//!
//! The ownership hazard the previous shape had is *gone*, not merely handled.
//! `deluge_audio_load_begin` adopts a `Sample` and reasons it before any I/O, so
//! exactly one of `finish`/`abandon` had to run on every path across every await.
//! Now `begin` and its matching `finish` sit inside one synchronous C++ function
//! with no suspension point between them, and this half owns no C++ object at
//! all. The only resource here is the efatfs handle, released on the single path
//! out of [`load_io_once`].
//!
//! ## Results are keyed by path, and a path outlives the card it was read from
//!
//! Everything here is keyed by the nominal path, and nothing in a [`LoadResult`]
//! records *which card* produced it. That is fine while one card stays in the
//! slot and is a silent wrong-file bug the moment one does not: parse a header
//! from card A, eject, insert card B, and the collector applies A's geometry to
//! B's file at the same path. It loads clean, reports no error, and plays the
//! wrong thing — the failure shape this whole boundary is built to keep out.
//!
//! `deluge_card_flush` (`card.rs`) is what closes it, and it is called at EJECTION
//! (`AudioFileManager::setCardEjected`), not at re-insertion — flushing on the way
//! back in would leave the window open for however long the slot stays empty.
//!
//! Clearing the three tables is not enough on its own, and the clears are not
//! what the correctness rests on. A load already in flight when the card goes
//! finishes afterwards and would publish into [`RESULTS`] *after* the flush,
//! reinstating exactly the stale result the flush existed to remove; and the
//! flush's own clears are `try_lock`ed (it must never wait), so a contended table
//! is simply left populated.
//!
//! What closes both is a STAMP on the result rather than a clear of the table.
//! Each load carries the card generation it began under, [`publish_into`] records
//! that value on the resident entry, and [`take_from`] returns an entry only when
//! its stamp equals the generation the TAKER is asking under — dropping a
//! mismatched entry outright, so the collector reads `UNKNOWN` and asks again
//! against the card that is actually in the slot. Checking at take is what makes
//! the outcome independent of which locks the flush managed to take, and of which
//! context either side happens to run in. ([`publish_result`] checks the same
//! stamp on the way in as well; that is an early drop that saves a slot, not the
//! guarantee — see its doc.)
//!
//! The counter itself is not this module's. It is [`deluge_card_generation`],
//! shared with every other loader that caches something read from the card,
//! because a card swap invalidates all of them at once and a second hand-rolled
//! counter is a second chance to reintroduce the bug this belt closed.
//!
//! The stamp is taken where the load BEGINS ([`take_next_request`], under the
//! queue lock), never where it publishes: sampling it at publish time reads the
//! generation the flush just installed and lets the stale result straight through,
//! which is the same "sampled too late" defect the C++ half's deferred-load cancel
//! already had.
//!
//! A stale entry that a failed clear left behind stays in its slot until it is
//! evicted or asked for. Deliberately not swept: it is uncollectable either way,
//! and with [`RESULT_SLOTS`] slots the cost is at most one redundant header read.
//!
//! ## Always on
//!
//! Everything here is compiled wherever this module is -- the Embassy BSP: device,
//! or host under `host_app` (see the `mod audio_loader;` cfg in `main.rs`): the
//! request/collect C ABI ([`deluge_audio_load_request`],
//! [`deluge_audio_load_collect`], [`deluge_audio_load_async_active`]), the
//! queue/result tables behind it, the parse driver ([`LoadOps`], [`load_io_once`])
//! and, where a backing is linked, `ProdOps` and [`audio_load_task`]. These override
//! the `__attribute__((weak))` C++ fallbacks in `audio_load_abi.cpp`; every other
//! BSP (legacy/cooperative sim, rza1) never links this crate, resolves those
//! fallbacks, and always takes the synchronous path.
//!
//! There is no switch. The tables and the task's pool live in `.bss`, which is
//! on-chip SRAM shared with the C++ allocator's `FAST_INTERNAL` heap: about 17 KB
//! when the loader became mandatory, which stepped that heap down one 64 KiB block.
//! That cost was accepted rather than moved to SDRAM.
//!
//! ## Why the driver is generic over [`LoadOps`]
//!
//! Same constraint as `streaming_loader`: `host_app` (which links the host-built
//! C++ app closure) and this crate's integration tests do not co-compile, so the
//! orchestration takes its filesystem operations through a trait. `ProdOps` wires
//! that to `crate::efatfs_fs` / `crate::efatfs_host_shim`, and
//! `tests/audio_loader_host.rs` wires it to an in-memory double.

// `audio_load_task` is the only drain for the request queue, and it needs a backing to do I/O through: on
// device that is `efatfs_streaming`. A device build without it would compile cleanly, accept every request and
// drain none, so the C++ collector would report LOAD_IN_PROGRESS forever, for every sample -- silence with no
// error. Refused at compile time instead. On host the module is reached through `main.rs` under `host_app`
// (itself a backing) and through `tests/audio_loader_host.rs`, which drives the tables by hand and needs no task.
#[cfg(all(target_os = "none", not(feature = "efatfs_streaming")))]
compile_error!(
    "the async audio loader needs `efatfs_streaming` on device: `audio_load_task` is the only drain for \
     the request queue and is not compiled without it, so every request would stay queued forever and \
     the C++ side would see LOAD_IN_PROGRESS on every tick."
);

use core::ffi::{CStr, c_char};
use core::mem::MaybeUninit;

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;

/// Longest single candidate path, including its NUL — `DELUGE_MAX_PATH`
/// (`audio_load.h`). The C side writes its candidate buffer at this stride, so
/// it is a layout constant, not a preference.
pub const DELUGE_MAX_PATH: usize = 256;

/// `AudioFileManager::kMaxLoadCandidates` — the C side proposes at most this
/// many, so a longer buffer would only ever be partly meaningful.
const MAX_LOAD_CANDIDATES: usize = 3;

/// `deluge_audio_load_candidates`' "`out` is not trustworthy at all" sentinel: a
/// candidate did not fit, or an internal allocation failed. The header is
/// explicit that the caller must not read `out` in that case, so a request
/// carrying it is refused rather than clamped. Never a legitimate count.
const DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED: u32 = 0xFFFF_FFFF;

/// One path, inline. **Nothing in this module may touch the Rust global heap.**
///
/// That heap is `fs_alloc::DelugeGlobalAlloc` over the 96 KiB `EFATFS_ARENA`,
/// and it says of itself: "Single-threaded use is assumed ... no internal
/// locking is added speculatively" (`deluge_alloc/src/lib.rs`). Today the
/// assumption holds because its only other user is `embedded-fatfs`, which runs
/// inside `with_fs` under the single FS mutex. This task runs on a *preempting*
/// interrupt executor, so a `String` allocated or dropped here could call
/// `Tlsf::free` while the thread-mode fiber is mid-`Tlsf::malloc` on the same
/// control block — corrupted free lists, surfacing later at some unrelated mount
/// or read.
///
/// It is the same hazard class as the C++ calls the request/collect split
/// removed, one level down: moving the C++ calls off this tier did not move the
/// Rust heap off it. Fixed-capacity storage is what actually removes it.
pub type LoadPath = heapless::String<MAX_PATH_CHARS>;

/// Longest path in CHARACTERS. [`DELUGE_MAX_PATH`] is a C buffer size and counts
/// the NUL, so the text it can hold is one byte shorter. Keeping the two
/// separate is not pedantry: sizing [`LoadPath`] at `DELUGE_MAX_PATH` let a
/// 256-character path be accepted as a request key and then refused by
/// [`LoadResult::set_alternate_path`] (which needs room for the terminator),
/// turning a loadable file into a spurious `FILE_UNREADABLE`.
const MAX_PATH_CHARS: usize = DELUGE_MAX_PATH - 1;

/// The candidate paths for one load, inline. See [`LoadPath`].
pub type CandidateList = heapless::Vec<LoadPath, MAX_LOAD_CANDIDATES>;

/// How many queued loads are held before further requests are refused. A refusal
/// is safe: the caller has already been told `LOAD_IN_PROGRESS` and its next
/// cursor move re-requests.
const LOAD_QUEUE_DEPTH: usize = 8;

/// How many finished-but-uncollected results are held.
///
/// Deliberately not smaller than [`LOAD_QUEUE_DEPTH`]: a loader that accepts more
/// work than it can report would, on a song load that fans out, discard the
/// earliest results by construction and re-read those headers every round. The
/// assertion below makes that structural rather than a property of two numbers
/// that happen to match today.
pub const RESULT_SLOTS: usize = LOAD_QUEUE_DEPTH;

const _: () = assert!(
    RESULT_SLOTS >= LOAD_QUEUE_DEPTH,
    "the result table must be able to report every request the queue accepts"
);

// Deluge `Error` codes as they cross the ABI, matched by NUMBER (`audio_load.h`:
// "Fixed-width, never a native enum"). `enum class Error` has no fixed underlying
// type, so the C++ side pins these numeric identities with `static_assert`s in
// `audio_load_abi.cpp`; these constants are this side of that same pinning.
/// `Error::NONE`.
pub const DELUGE_ERROR_NONE: u8 = 0;
/// `Error::FILE_CORRUPTED`.
pub const DELUGE_ERROR_FILE_CORRUPTED: u8 = 5;
/// `Error::FILE_UNREADABLE` — "or not found, I think?" per its own definition.
pub const DELUGE_ERROR_FILE_UNREADABLE: u8 = 6;
/// `Error::FILE_UNSUPPORTED`.
pub const DELUGE_ERROR_FILE_UNSUPPORTED: u8 = 7;
/// `Error::TOO_MANY_OPEN_STREAMS` — the efatfs handle table had no free slot.
///
/// Deliberately NOT folded into [`DELUGE_ERROR_FILE_UNREADABLE`]: the collector
/// treats that code as "no candidate opened" and answers it by probing the
/// alternate directory, which is the wrong recovery for a resource exhaustion and
/// can end up loading a different file. This code is what routes it to the
/// reclaim the synchronous path uses instead.
pub const DELUGE_ERROR_TOO_MANY_OPEN_STREAMS: u8 = 34;

/// `deluge_audio_load_collect`: queued or in flight; ask again, do not re-request.
pub const DELUGE_AUDIO_LOAD_PENDING: u8 = 0;
/// `deluge_audio_load_collect`: a result was written out and removed.
pub const DELUGE_AUDIO_LOAD_READY: u8 = 1;
/// `deluge_audio_load_collect`: nothing known about this path.
pub const DELUGE_AUDIO_LOAD_UNKNOWN: u8 = 2;

/// The values this module hand-duplicates from `audio_load.h`, pinned.
///
/// Asserting a constant against its own literal looks tautological, and that is
/// exactly the point: it makes a change here take two deliberate edits instead of
/// one, and the mirror of these numbers lives in `audio_load_abi.cpp`'s
/// `static_assert`s. Without this the C side pinned only C's own values, so a
/// Rust-side edit — `MAX_LOAD_CANDIDATES` most dangerously, since it silently
/// changes how many candidate slots are read — was caught by nothing.
const _: () = {
    assert!(DELUGE_MAX_PATH == 256);
    assert!(MAX_PATH_CHARS == 255);
    assert!(MAX_LOAD_CANDIDATES == 3); // AudioFileManager::kMaxLoadCandidates
    assert!(DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED == 0xFFFF_FFFF);
    assert!(DELUGE_AUDIO_LOAD_PENDING == 0);
    assert!(DELUGE_AUDIO_LOAD_READY == 1);
    assert!(DELUGE_AUDIO_LOAD_UNKNOWN == 2);
    // Deluge `Error`, pinned C-side by audio_load_abi.cpp's own static_asserts.
    assert!(DELUGE_ERROR_NONE == 0);
    assert!(DELUGE_ERROR_FILE_CORRUPTED == 5);
    assert!(DELUGE_ERROR_FILE_UNREADABLE == 6);
    assert!(DELUGE_ERROR_FILE_UNSUPPORTED == 7);
    assert!(DELUGE_ERROR_TOO_MANY_OPEN_STREAMS == 34);
};

/// The parsed header geometry, field-for-field `DelugeAudioLoadGeometry`
/// (`audio_load.h`). A named struct rather than positional scalars precisely so a
/// same-typed transposition cannot compile clean and render wrong.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct LoadGeometry {
    pub sample_rate: u32,
    pub audio_data_start_pos_bytes: u32,
    pub audio_data_length_bytes: u32,
    pub file_loop_start_samples: u32,
    pub file_loop_end_samples: u32,
    pub wavetable_cycle_size: u32,
    /// Only meaningful when `midi_note_present` is nonzero.
    pub midi_note_value: f32,
    pub num_channels: u8,
    pub byte_depth: u8,
    /// `RawDataFormat`'s numeric value. An out-of-range byte is REFUSED by the
    /// C++ side, not degraded to a default.
    pub raw_data_format: u8,
    pub midi_note_present: u8,
    pub wavetable_explicitly_specifies_self: u8,
}

/// A finished parse, field-for-field `DelugeAudioLoadResult` (`audio_load.h`).
///
/// Plain data, no ownership — deliberately. A result sits in a fixed slot until
/// collected, so it has to be safe to discard one nobody collects. Nothing here
/// owns a handle, an allocation, or an adopted object, so dropping one costs the
/// I/O that produced it and leaks nothing.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LoadResult {
    /// Valid only when `error` is [`DELUGE_ERROR_NONE`].
    pub geometry: LoadGeometry,
    pub size_bytes: u64,
    /// The alternate location the bytes were found at, NUL-terminated, or an
    /// empty string when the regular path won.
    pub alternate_path: [u8; DELUGE_MAX_PATH],
    pub error: u8,
}

impl LoadResult {
    /// A result carrying only a failure verdict. Geometry and path are zeroed;
    /// only `error` is meaningful, and the collector will not build anything.
    pub fn failed(error: u8) -> Self {
        Self {
            geometry: LoadGeometry::default(),
            size_bytes: 0,
            alternate_path: [0; DELUGE_MAX_PATH],
            error,
        }
    }

    /// A result carrying a parsed header. `alternate_path` starts empty (the
    /// regular path won) and is filled in by the caller only when it did not.
    pub fn succeeded(geometry: LoadGeometry, size_bytes: u64) -> Self {
        Self {
            geometry,
            size_bytes,
            alternate_path: [0; DELUGE_MAX_PATH],
            error: DELUGE_ERROR_NONE,
        }
    }

    /// Write `path` into `alternate_path`, NUL-terminated. `false` if it does not
    /// fit — which cannot happen for a candidate the C side proposed (it bounds
    /// them by `DELUGE_MAX_PATH` and reports truncation rather than writing one
    /// that would not fit), but is refused rather than silently cut here.
    #[must_use]
    pub fn set_alternate_path(&mut self, path: &str) -> bool {
        let bytes = path.as_bytes();
        if bytes.len() + 1 > DELUGE_MAX_PATH {
            return false;
        }
        self.alternate_path[..bytes.len()].copy_from_slice(bytes);
        self.alternate_path[bytes.len()] = 0;
        true
    }
}

/// `DelugeAudioLoadGeometry`'s and `DelugeAudioLoadResult`'s layouts, pinned
/// field by field.
///
/// This boundary is hand-declared on both sides — `audio_load.h` is not in
/// `wrapper.h`, so bindgen does not generate these structs. Pinning the SIZE
/// alone is not enough and was the gap here before: swapping `sample_rate` and
/// `audio_data_start_pos_bytes` (both `u32`) keeps every size identical and
/// compiles clean on both sides, producing a sample that loads without error and
/// renders at the wrong rate — this file's own "worst available shape".
///
/// `audio_load_abi.cpp` carries the mirror of these numbers as `static_assert`s,
/// so a reorder on EITHER side fails to compile rather than silently
/// reinterpreting every field after it.
const _: () = {
    use core::mem::{align_of, offset_of, size_of};

    assert!(size_of::<LoadGeometry>() == 36);
    assert!(align_of::<LoadGeometry>() == 4);
    assert!(offset_of!(LoadGeometry, sample_rate) == 0);
    assert!(offset_of!(LoadGeometry, audio_data_start_pos_bytes) == 4);
    assert!(offset_of!(LoadGeometry, audio_data_length_bytes) == 8);
    assert!(offset_of!(LoadGeometry, file_loop_start_samples) == 12);
    assert!(offset_of!(LoadGeometry, file_loop_end_samples) == 16);
    assert!(offset_of!(LoadGeometry, wavetable_cycle_size) == 20);
    assert!(offset_of!(LoadGeometry, midi_note_value) == 24);
    assert!(offset_of!(LoadGeometry, num_channels) == 28);
    assert!(offset_of!(LoadGeometry, byte_depth) == 29);
    assert!(offset_of!(LoadGeometry, raw_data_format) == 30);
    assert!(offset_of!(LoadGeometry, midi_note_present) == 31);
    assert!(offset_of!(LoadGeometry, wavetable_explicitly_specifies_self) == 32);

    assert!(size_of::<LoadResult>() == 312);
    assert!(align_of::<LoadResult>() == 8);
    assert!(offset_of!(LoadResult, geometry) == 0);
    assert!(offset_of!(LoadResult, size_bytes) == 40);
    assert!(offset_of!(LoadResult, alternate_path) == 48);
    assert!(offset_of!(LoadResult, error) == 304);
};

/// One queued load: the nominal path (the key a result is collected by) and the
/// candidate paths to try, in order.
pub struct LoadRequest {
    pub path: LoadPath,
    pub candidates: CandidateList,
}

/// A finished parse waiting to be collected, tagged with the nominal path it
/// belongs to and the card generation the load that produced it began under.
pub struct PendingResult {
    key: LoadPath,
    result: LoadResult,
    /// The [`deluge_card_generation`] value the load was started under. Carried
    /// on the RESIDENT entry, not merely checked on the way in, so that whether a
    /// result is honoured is decided by the taker rather than by which locks a
    /// flush managed to take. See [`take_from`].
    generation: u32,
}

/// Raised to wake [`audio_load_task`] when a load is newly requested. Always
/// compiled — with the driver absent, or before the task is spawned, nobody
/// awaits it and signalling is a harmless flag set.
pub static LOAD_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// The bounded request queue [`deluge_audio_load_request`] pushes into and
/// [`audio_load_task`] drains.
/// The request queue's storage type. Named so the table logic below can be
/// compiled and tested without the static itself existing.
pub type RequestQueue = heapless::Deque<LoadRequest, LOAD_QUEUE_DEPTH>;

/// The result table's storage type. See [`RequestQueue`].
pub type ResultTable = heapless::Deque<PendingResult, RESULT_SLOTS>;

static QUEUE: Mutex<CriticalSectionRawMutex, RequestQueue> = Mutex::new(heapless::Deque::new());

/// The path the task is parsing right now, if any. Exists so
/// [`deluge_audio_load_collect`] can answer `PENDING` rather than `UNKNOWN` in
/// the window between a request leaving [`QUEUE`] and its result reaching
/// [`RESULTS`] — an `UNKNOWN` there would tell the C++ side to re-request work
/// that is already running, and it would do so on every tick.
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<LoadPath>> = Mutex::new(None);

/// Finished parses waiting to be collected, oldest at the front.
///
/// **What happens to an uncollected result:** it is evicted, oldest first, once
/// [`RESULT_SLOTS`] later loads have finished. That is safe precisely because
/// [`LoadResult`] owns nothing — eviction costs the I/O that produced it and
/// nothing else, and a collector that comes back to find it gone gets `UNKNOWN`,
/// whose documented meaning is "request it again". Back-pressure was the
/// alternative (refuse new requests until the table drains), and it is worse: a
/// browser cursor that moves on without collecting would wedge the loader for the
/// rest of the run rather than costing one redundant read.
///
/// **Superseded requests** need no special handling for the same reason. Results
/// are keyed by nominal path, so a newer request arriving mid-flight does not
/// disturb the older one; the older result lands in its own slot and is either
/// collected or evicted. A repeat request for a path already in the table
/// replaces that entry rather than adding a second, so a coalescing caller
/// cannot grow the table with duplicates.
static RESULTS: Mutex<CriticalSectionRawMutex, ResultTable> = Mutex::new(heapless::Deque::new());

/// Drop the tables this loader keeps. The card generation's bump belongs to the
/// CALLER (`card.rs`'s `deluge_card_flush`), which performs it before calling
/// here — see that function for why the ordering is not negotiable.
///
/// # Why a failed clear is free
///
/// This runs synchronously from C++ on the interaction tier and must never wait,
/// so the three tables are `try_lock`ed rather than awaited.
///
/// **Every one of the three `try_lock`s may fail, and none of them costs
/// anything.** The clears are housekeeping — they free slots and stop the
/// collector waiting on a mark nothing will resolve — and no correctness argument
/// rests on any of them landing:
///
/// - [`RESULTS`]: an entry that survives an unclearable table carries the stamp of
///   the card it was read from, and [`take_from`] compares that stamp against the
///   collector's current generation and drops it. A surviving entry is
///   uncollectable, not stale-but-live.
/// - [`IN_FLIGHT`]: a surviving mark reports `PENDING` for one more collect, and
///   the load it names clears it when it finishes publishing (into a table that
///   will refuse it). It cannot make a result collectable.
/// - [`QUEUE`]: a request that is still QUEUED has done no I/O yet, so whenever it
///   runs it reads whatever card is in the slot then and is stamped accordingly.
///   Clearing the queue is the contract ("a request issued against a card that is
///   gone has no claim on being honoured"), not the safety property.
///
/// The safety property is the STAMP, checked at take, and it holds no matter which
/// locks were taken or which contexts the two sides ran in. Nothing here may be
/// re-argued from where the loader is scheduled: the loader owns its own executor
/// on device but a plain `std::thread` under `host_app`, so any claim of the form
/// "these two cannot run at once" is false on some configuration that builds this
/// file.
pub(crate) fn flush_tables() {
    if let Ok(mut queue) = QUEUE.try_lock() {
        queue.clear();
    }
    if let Ok(mut in_flight) = IN_FLIGHT.try_lock() {
        *in_flight = None;
    }
    if let Ok(mut results) = RESULTS.try_lock() {
        results.clear();
    }
}

/// Whether the Rust async loader backs sample loads — what the C++ side asks
/// before choosing the async path over the synchronous one.
///
/// Always `true` wherever this module is compiled: the loader has no switch. The
/// symbol is what overrides `audio_load_abi.cpp`'s weak `false`, which every BSP
/// without this crate resolves, and those builds keep the synchronous path.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_load_async_active() -> bool {
    true
}

/// Milliseconds `audio_load_task` waits after marking each load in flight. Host harness only; see
/// [`deluge_audio_load_set_latency_ms`].
#[cfg(feature = "host_app")]
static LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: make every audio load wait `ms` milliseconds after it is marked in flight, so
/// a gate can hold a load while a consumer keeps collecting `PENDING`. 0, the default, means no wait.
///
/// Compiled only under `host_app`: the device image has neither this function nor the static it sets.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_load_set_latency_ms(ms: u32) {
    LATENCY_MS.store(ms, core::sync::atomic::Ordering::Relaxed);
}

/// Queue `file_path` for the async loader and wake it. See `audio_load.h`.
///
/// # Stack cost on the calling tier
///
/// Going allocation-free moved this cost from the heap to the caller's stack, and
/// it is worth stating rather than only counting the `.bss` half. On device
/// (`usize` = 4) a [`LoadPath`] is 260 bytes, a [`CandidateList`] 784, and a
/// [`LoadRequest`] 1044; this function materialises the decoded list and then the
/// request, so its frame peaks under ~2 KB before the request is moved into the
/// queue. Against the 32 KB program stack that is comfortable, but it is charged
/// to whatever context C++ calls this from, not to the loader. (Derived from the
/// type layouts, not measured — Rust has no stack-usage report here.)
///
/// # Safety
///
/// `file_path` must be a valid NUL-terminated C string, and `candidates` must
/// point to at least `num_candidates * DELUGE_MAX_PATH` readable bytes. Both are
/// copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_audio_load_request(
    file_path: *const c_char,
    candidates: *const c_char,
    num_candidates: u32,
) -> bool {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(file_path) }) else {
        return false;
    };
    if candidates.is_null() {
        return false;
    }
    if num_candidates == DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED {
        // `deluge_audio_load_candidates` reported its buffer untrustworthy, and
        // the header is explicit that the caller must not read it. Clamping this
        // to the candidate bound and reading three slots anyway would open
        // whatever a silent truncation left there -- possibly a DIFFERENT real
        // file sitting at the truncated path. Most such buffers would also fail
        // the decoder's NUL/UTF-8 checks, but that is an accident of the decoder,
        // not the contract.
        return false;
    }
    let slots = (num_candidates as usize).min(MAX_LOAD_CANDIDATES);
    // SAFETY: the caller promises `num_candidates * DELUGE_MAX_PATH` readable
    // bytes, and `slots` is clamped at or below `num_candidates`.
    let buffer =
        unsafe { core::slice::from_raw_parts(candidates.cast::<u8>(), slots * DELUGE_MAX_PATH) };
    let Some(candidates) = decode_candidates(buffer) else {
        return false;
    };
    if candidates.is_empty() {
        return false;
    }

    enqueue_request(LoadRequest { path, candidates })
}

/// Push a decoded request onto [`QUEUE`] and wake the loader.
///
/// `try_lock` rather than a block: this is called synchronously from C++ in
/// whatever context the app happens to be in, and must never wait.
fn enqueue_request(request: LoadRequest) -> bool {
    let Ok(mut queue) = QUEUE.try_lock() else {
        return false;
    };
    if queue.push_back(request).is_err() {
        return false; // Queue full; the caller re-requests on its next tick.
    }
    drop(queue);
    LOAD_WAKE.signal(());
    true
}

/// Collect the finished parse for `file_path`, if one is ready. See
/// `audio_load.h`.
///
/// # Why the probe order is QUEUE -> IN_FLIGHT -> RESULTS
///
/// An entry only ever moves forward: [`QUEUE`] -> [`IN_FLIGHT`] -> [`RESULTS`].
/// **Three** rules together make a forward pass unable to miss one, and all three
/// are load-bearing:
///
/// 1. [`audio_load_task`] sets `IN_FLIGHT` while still holding the `QUEUE` lock.
/// 2. It publishes to `RESULTS` *before* clearing `IN_FLIGHT`, so that handoff
///    overlaps rather than gaps.
/// 3. [`is_queued`] and [`is_in_flight`] report `true` when they cannot take
///    their lock.
///
/// Rule 3 is what covers the QUEUE -> IN_FLIGHT handoff, and it is easy to lose:
/// rule 1 does NOT put the entry in two tables at once, so during that handoff
/// the entry really is in neither — a reader that treated contention as "not
/// there" would report `UNKNOWN` for a request the loader had just picked up. It
/// is the *lock*, not the table, that covers that window. (Rule 2, by contrast,
/// does put the entry in both tables briefly, which is why a published-but-still-
/// marked result reports `PENDING` for one extra tick.)
///
/// Given all three: a miss on `QUEUE` means the entry has advanced past it, a
/// subsequent miss on `IN_FLIGHT` means it has advanced past that too, and
/// `RESULTS` is terminal until collected. Reading in the REVERSE order can miss —
/// a miss on `RESULTS`, then the task publishes and clears `IN_FLIGHT`, then a
/// miss on `IN_FLIGHT` — and reports `UNKNOWN` for a load that has already
/// succeeded, so the caller re-requests work that is done and orphans the result
/// slot it left behind.
///
/// That interleaving needs two threads and is not directly testable here; the
/// argument above is what makes it safe, and each branch of the probe has its own
/// test.
///
/// # Safety
///
/// `file_path` must be a valid NUL-terminated C string and `out` a valid,
/// writable [`LoadResult`]. `out` is written only on
/// [`DELUGE_AUDIO_LOAD_READY`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_audio_load_collect(
    file_path: *const c_char,
    out: *mut LoadResult,
) -> u8 {
    if out.is_null() {
        return DELUGE_AUDIO_LOAD_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(file_path) }) else {
        return DELUGE_AUDIO_LOAD_UNKNOWN;
    };

    // SAFETY: `out` is non-null (checked above) and the caller's contract makes it
    // a suitably aligned, writable `LoadResult` allocation. Viewing it as possibly
    // uninitialised is the weaker assumption, and the honest one: C hands us an
    // output slot, not a value, and nothing here ever reads it.
    let out = unsafe { &mut *out.cast::<MaybeUninit<LoadResult>>() };

    match probe(&path, out) {
        Probe::Ready => DELUGE_AUDIO_LOAD_READY,
        Probe::Pending => DELUGE_AUDIO_LOAD_PENDING,
        Probe::Unknown => DELUGE_AUDIO_LOAD_UNKNOWN,
    }
}

/// What one forward pass over the pipeline found. Deliberately produced by a
/// SINGLE pass: splitting "is it ready?" and "is it still coming?" into two
/// passes reintroduces the very race this ordering exists to close, because the
/// loader can publish between them and the second pass then reports `Unknown`
/// for a result the first pass merely missed.
///
/// A status only — [`Probe::Ready`] carries no payload. A found [`LoadResult`]
/// has exactly one destination, the caller's `out` slot, so [`probe`] writes it
/// there and reports which case it took, instead of handing ~312 bytes back
/// through a return value for the caller to forward.
///
/// **That is a shape argument, not a speed one — do not restate it as an
/// optimisation.** Both forms were measured on the release build of
/// `deluge_audio_load_collect` and cost the same: 368 instructions each, the
/// same `sub $0x488,%rsp` frame, and the same two 312-byte `memcpy`s. `probe`
/// inlines completely either way, so the old form never moved a payload on
/// `Pending`/`Unknown` either, and on `Ready` the copy moved rather than went
/// away — from the enum's payload slot to `take_from`'s return temporary.
///
/// Boxing the payload is the other way to shrink the enum, and it is wrong here
/// for a reason that has nothing to do with size: it puts a heap allocation on
/// the collect path, which the C++ side calls per tick and which must not
/// allocate.
///
/// One genuine behavioural delta, small but worth knowing: the copy into `out`
/// now happens while the `RESULTS` guard is still held — the match scrutinee
/// keeps it alive across the arm — where before it happened in the caller, after
/// `probe` had returned and the guard had dropped. `embassy_sync`'s async
/// `Mutex` guard holds no critical section, so this only widens the window in
/// which a concurrent `publish_result` would `.await`. It cannot deadlock and
/// cannot produce a wrong status.
enum Probe {
    /// `out` has been written. **Only this case writes it**, and the C side is
    /// told so: `audio_load.h` lets a caller pass an uninitialised slot and read
    /// it back only on `READY`. `live_pipeline`'s `collect` seeds a `0xFF`
    /// sentinel and the `PENDING`/`UNKNOWN` tests assert it survives, so a
    /// future branch that writes a placeholder fails there rather than in a
    /// caller that trusted the contract.
    Ready,
    Pending,
    Unknown,
}

/// One forward pass: QUEUE, then IN_FLIGHT, then RESULTS. See
/// [`deluge_audio_load_collect`]'s doc for why the order and the contention
/// mapping are both load-bearing.
fn probe(path: &str, out: &mut MaybeUninit<LoadResult>) -> Probe {
    if is_queued(path) || is_in_flight(path) {
        return Probe::Pending;
    }
    // A contended table means the loader is mid-publish. `Pending` is the honest
    // answer: never claim `Unknown` on contention, or the caller re-requests work
    // that is about to land.
    let Ok(mut results) = RESULTS.try_lock() else {
        return Probe::Pending;
    };
    // The card generation is read HERE, at the take, not sampled earlier: it is
    // the collector's own view of which card is in the slot right now, and
    // deciding from it is what makes a result's fate independent of whether the
    // flush that invalidated it managed to take this lock. See [`take_from`].
    match take_from(&mut results, path, deluge_card_generation::current()) {
        Some(result) => {
            out.write(result);
            Probe::Ready
        }
        None => Probe::Unknown,
    }
}

/// Whether `path` is still waiting in [`QUEUE`].
///
/// **Contention reports `true`, and that is load-bearing, not defensive.** The
/// loader marks a request in flight while still holding this lock, so a `collect`
/// racing that handoff finds the lock taken — mapping it to `false` here would
/// report `UNKNOWN` for a request mid-handoff, which is exactly the bug the probe
/// order exists to prevent. Do not "simplify" this branch.
fn is_queued(path: &str) -> bool {
    match QUEUE.try_lock() {
        Ok(queue) => queue.iter().any(|request| request.path == path),
        Err(_) => true,
    }
}

/// Whether `path` is the load being parsed right now. Contention reports `true`,
/// for the same reason as [`is_queued`] — and with the same warning.
fn is_in_flight(path: &str) -> bool {
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => in_flight.as_deref() == Some(path),
        Err(_) => true,
    }
}

/// Record (or clear) the load being parsed right now.
///
/// Called by [`audio_load_task`] while it still holds the [`QUEUE`] lock, so the
/// QUEUE -> IN_FLIGHT handoff has no window in which a request sits in neither
/// table. Also the seam the host tests drive to exercise
/// [`deluge_audio_load_collect`]'s IN_FLIGHT branch, which no test could reach
/// otherwise because no test runs the task.
pub async fn mark_in_flight(path: Option<LoadPath>) {
    *IN_FLIGHT.lock().await = path;
}

/// Remove and return the entry at `index`, preserving the order of the rest.
///
/// `heapless::Deque` has no positional removal, so rotate the entries in front of
/// `index` around the back, pop the target, then rotate the rest back into place.
/// Bounded by [`RESULT_SLOTS`].
///
/// The second rotation is `len - index` steps, NOT `index`: after the pop, the
/// original front sits `index` places from the back, so rotating forward by the
/// remaining length restores it. Rotating by `index` instead leaves the table in a
/// rotated order, which silently corrupts the oldest-first eviction the whole
/// table depends on — the entry that looked oldest would no longer be.
fn remove_at(
    deque: &mut heapless::Deque<PendingResult, RESULT_SLOTS>,
    index: usize,
) -> PendingResult {
    for _ in 0..index {
        let entry = deque.pop_front().expect("index is in range");
        let _ = deque.push_back(entry);
    }
    let taken = deque.pop_front().expect("index is in range");
    for _ in 0..(deque.len() - index) {
        let entry = deque.pop_front().expect("rotated entry");
        let _ = deque.push_back(entry);
    }
    taken
}

/// Publish `result` under `key` into `table`, stamped with the card `generation`
/// the load began under, replacing any existing entry for that key and evicting
/// the oldest if the table is full. See [`RESULTS`].
///
/// The generation arrives as a PARAMETER rather than being read from
/// [`deluge_card_generation`] here, for the same reason the table is passed by
/// reference: the stamp that belongs on the entry is the one the load BEGAN under,
/// which only the caller knows, and a test must be able to publish and take under
/// generations it chooses. Do not reach for the live counter inside this function.
///
/// Takes the table by reference rather than reaching for the static, so the
/// eviction and replace behaviour is compiled and unit-tested on every build —
/// including the ones where [`RESULTS`] does not exist. That matters: the
/// `remove_at` ordering bug lived in exactly this code and was invisible to
/// every test that ran against a single-entry table.
pub fn publish_into(table: &mut ResultTable, key: LoadPath, result: LoadResult, generation: u32) {
    if let Some(index) = table.iter().position(|entry| entry.key == key) {
        let _ = remove_at(table, index);
    }
    if table.is_full() {
        let _ = table.pop_front();
    }
    let _ = table.push_back(PendingResult {
        key,
        result,
        generation,
    });
}

/// Take `path`'s result out of `table`, if it holds one stamped `generation`.
///
/// Removing is the point: a second collect must report `UNKNOWN`, or a caller
/// could build the same `Sample` twice.
///
/// # The generation check, and why it lives HERE
///
/// A matching entry stamped anything other than `generation` was parsed from a
/// card that is no longer in the slot. It is removed and `None` is returned —
/// removed as well as withheld, because leaving it resident would let a later
/// take at some future generation find it again, and returning it is the
/// wrong-file bug the whole flush exists to prevent. `None` reaches the collector
/// as `UNKNOWN`, whose documented meaning is already "request it again".
///
/// **This is the safety property, and the only one.** `deluge_card_flush`
/// (`card.rs`) cannot guarantee it: the generation bump it performs is
/// unconditional, but [`flush_tables`]'s clears are `try_lock`ed and a contended
/// table stays populated, so an entry can outlive a flush. Deciding at TAKE, from
/// the caller's own current generation, is what makes the outcome independent of
/// which locks the flush managed to take and of which context either side runs
/// in. [`publish_result`]'s matching pre-insert check is an early drop layered on
/// top, not the guarantee.
///
/// Deliberately not paired with a sweep of the other stale entries. One can sit in
/// a slot until it is evicted or asked for, and since it is uncollectable either
/// way the whole cost is one redundant header read on a table of [`RESULT_SLOTS`]
/// — bounded waste, not a correctness question.
pub fn take_from(table: &mut ResultTable, path: &str, generation: u32) -> Option<LoadResult> {
    let index = table.iter().position(|entry| entry.key == path)?;
    let entry = remove_at(table, index);
    (entry.generation == generation).then_some(entry.result)
}

/// Publish into the live [`RESULTS`] table, stamped with the card generation the
/// load began under, and skip the insert entirely if that card is already gone.
///
/// `generation` is the value [`deluge_card_generation::current`] returned when the
/// load STARTED — see [`take_next_request`], which samples it, and the module doc
/// for why sampling it here instead would defeat the whole mechanism.
///
/// # The early-drop check is an optimisation, not the guarantee
///
/// The check below is an EARLY DROP: it keeps a result already known to be doomed
/// from occupying one of [`RESULT_SLOTS`] slots and evicting a live neighbour. It
/// is not what makes the stale result unusable — [`take_from`] is, by comparing
/// the stamp this call records against the taker's own current generation.
///
/// That distinction is load-bearing rather than pedantic. There is no
/// happens-before edge from [`deluge_card_generation::bump`]'s `fetch_add` to
/// this `load` when this task wins the `RESULTS` lock (the two synchronise only
/// through the lock, in the other direction), so this check can legitimately read
/// the pre-flush generation, pass, and insert. The entry it inserts still carries
/// the pre-flush stamp, and the take drops it. Do not rewrite this as the safety
/// property.
///
/// The check happens with the table lock already held, not before acquiring it, so
/// that it is at least ordered against a flush that DID take the lock.
pub async fn publish_result(key: LoadPath, result: LoadResult, generation: u32) {
    let mut table = RESULTS.lock().await;
    if generation != deluge_card_generation::current() {
        // The card this was read from is gone. Dropping costs the I/O that
        // produced it and nothing else (`LoadResult` owns nothing), and the
        // collector reads the resulting `UNKNOWN` as "request it again" -- which
        // it then does, against the card that is actually in the slot.
        return;
    }
    publish_into(&mut table, key, result, generation);
}

/// Decode the candidate buffer the C side filled: one NUL-terminated path per
/// [`DELUGE_MAX_PATH`] slot. `None` if any slot is unterminated, not UTF-8, or
/// there are more slots than [`MAX_LOAD_CANDIDATES`].
///
/// Allocation-free, like everything else here — see [`LoadPath`].
pub fn decode_candidates(buffer: &[u8]) -> Option<CandidateList> {
    let slots = buffer.len() / DELUGE_MAX_PATH;
    if slots > MAX_LOAD_CANDIDATES {
        return None;
    }
    let mut out = CandidateList::new();
    for slot in 0..slots {
        let bytes = &buffer[slot * DELUGE_MAX_PATH..(slot + 1) * DELUGE_MAX_PATH];
        // Anything after the NUL is the caller's untouched buffer, never read.
        let end = bytes.iter().position(|b| *b == 0)?;
        let path = LoadPath::try_from(core::str::from_utf8(&bytes[..end]).ok()?).ok()?;
        out.push(path).ok()?;
    }
    Some(out)
}

/// Copy a C string into a fixed-capacity [`LoadPath`]. `None` for NULL, non-UTF-8,
/// or a path longer than [`DELUGE_MAX_PATH`] — a path that does not fit is
/// refused rather than truncated, because a truncated path names a *different*
/// file that may well exist.
///
/// `pub(crate)` for the host test that runs it against the corpus C++ checks the
/// same rule with (`path_admission_corpus.inc`); nothing outside this module
/// calls it.
///
/// # Safety
///
/// `ptr` must be NULL or a valid NUL-terminated C string.
pub(crate) unsafe fn cstr_to_path(ptr: *const c_char) -> Option<LoadPath> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: the caller's contract, above.
    let cstr = unsafe { CStr::from_ptr(ptr) };
    LoadPath::try_from(cstr.to_str().ok()?).ok()
}

// ── The parse driver ────────────────────────────────────────────────────────

mod driver {
    use super::{
        DELUGE_ERROR_FILE_CORRUPTED, DELUGE_ERROR_FILE_UNREADABLE, DELUGE_ERROR_FILE_UNSUPPORTED,
        DELUGE_ERROR_NONE, DELUGE_ERROR_TOO_MANY_OPEN_STREAMS, LoadGeometry, LoadRequest,
        LoadResult,
    };
    use deluge_audio_format::{
        AudioHeader, FeedResult, HeaderError, HeaderParser, MAX_WINDOW_BYTES,
    };

    impl From<&AudioHeader> for LoadGeometry {
        /// Exactly the fields the synchronous `AudioFile::loadFile` writes onto a
        /// `Sample`. Anything missed here loads without error and renders wrong.
        fn from(header: &AudioHeader) -> Self {
            Self {
                sample_rate: header.sample_rate,
                audio_data_start_pos_bytes: header.audio_data_start_pos_bytes,
                audio_data_length_bytes: header.audio_data_length_bytes,
                file_loop_start_samples: header.file_loop_start_samples,
                file_loop_end_samples: header.file_loop_end_samples,
                wavetable_cycle_size: header.wavetable.cycle_size,
                // The C++ side turns "not present" into the -1.0f sentinel
                // `Sample::midiNoteFromFile` uses; this side only reports
                // presence, so a value carried alongside a zero flag is never
                // read.
                midi_note_value: header.midi_note.unwrap_or(0.0),
                num_channels: header.num_channels,
                byte_depth: header.byte_depth,
                raw_data_format: header.raw_data_format as u8,
                midi_note_present: u8::from(header.midi_note.is_some()),
                wavetable_explicitly_specifies_self: u8::from(
                    header.wavetable.explicitly_specifies_self,
                ),
            }
        }
    }

    /// Map a parser failure onto the Deluge `Error` the C++ side already branches
    /// on. Identical to the synchronous path's own mapping
    /// (`audio_file_format.cpp`'s `errorFromParseStatus`), which is what preserves
    /// the error identity `sample_browser.cpp` reads as "not a wavetable, try it
    /// as a sample".
    pub fn error_code_for(error: HeaderError) -> u8 {
        match error {
            // Neither RIFF/WAVE nor FORM/AIFF; or a bit depth, format tag,
            // channel count or sample rate we cannot play.
            HeaderError::NotAudio | HeaderError::Unsupported(_) => DELUGE_ERROR_FILE_UNSUPPORTED,
            // No fmt/COMM or no data/SSND chunk; or the file ends inside its own
            // header.
            HeaderError::Malformed(_) | HeaderError::Truncated { .. } => {
                DELUGE_ERROR_FILE_CORRUPTED
            }
        }
    }

    /// Why one candidate's open did not yield a handle.
    ///
    /// A plain `Option` collapsed the two, and the collapse was a real defect:
    /// "this file is not here" is a CANDIDATE verdict (try the next one, and if
    /// none opens the load has genuinely missed, which is what earns the
    /// alternate-dir probe), while "the handle table is full" is a RESOURCE
    /// verdict that says nothing at all about where the file lives. Reported as a
    /// miss, a full table sent the load probing the alternate directory,
    /// promoted the load's alternate-dir status for every later sample, and
    /// could answer with a same-named file from the alternate directory — where
    /// the synchronous path reports `Error::TOO_MANY_OPEN_STREAMS` and reclaims.
    /// The two must stay distinguishable all the way out to the collector.
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum OpenVerdict {
        /// Opened. The efatfs handle, which the caller must close.
        Opened(u32),
        /// Nothing to read at this path: missing, unmounted, or any other open
        /// failure the backing does not distinguish. A candidate verdict.
        Missing,
        /// The efatfs handle table has no free slot. A resource verdict, and not
        /// specific to this candidate — every other candidate would hit it too.
        TableFull,
    }

    /// The filesystem operations [`load_io_once`] needs. **All four are `async`
    /// and none reaches C++** — that is the module doc's single rule, expressed as
    /// a type. Adding a synchronous C++ call to this trait would reintroduce the
    /// interrupt-priority deadlock the split exists to remove.
    pub trait LoadOps {
        /// Open `path` for reading, resolving to an efatfs handle. See
        /// [`OpenVerdict`] for why a failure is not simply `None`.
        async fn open(&self, path: &str) -> OpenVerdict;

        /// The size of the file behind `handle`, in bytes. Must read the same
        /// handle table [`LoadOps::open`] filled, or the parse gets a bound
        /// belonging to a different file.
        async fn size_of(&self, handle: u32) -> Option<u32>;

        /// Fill `buf` from absolute `offset` of the file behind `handle`. `false`
        /// if the full buffer could not be read.
        ///
        /// Never short at end of file, and that is load-bearing:
        /// `HeaderParser::new(size)` clamps every request so `start + len` is
        /// inside the file it was given the size of, and that size comes from
        /// [`LoadOps::size_of`] on this same handle. So a `false` here is a real
        /// read failure, never an end-of-file that a byte count would have had to
        /// distinguish.
        async fn read_at(&self, handle: u32, offset: u32, buf: &mut [u8]) -> bool;

        /// Release the efatfs handle. The table holds only 128 slots, so a leak
        /// here is silent capacity loss rather than an error anything reports.
        async fn close(&self, handle: u32);
    }

    /// Run one queued load's I/O: try each candidate in order until one both
    /// opens and sizes, then walk its header to a verdict.
    ///
    /// Always returns a [`LoadResult`] — success or failure, there is always
    /// something for the C++ side to collect and act on. Every handle this opens
    /// is closed before it returns, on every path.
    ///
    /// # Which candidate is the "regular" one
    ///
    /// By POSITION, never by string equality. `proposeLoadCandidates` always
    /// pushes the regular path LAST and says so in its own comment, and
    /// `resolveFileSize` classifies on that position for a reason: when
    /// the load context's `alternateDir` happens to be the file's own directory, the
    /// bare-name alternate candidate stringifies to exactly the nominal path. A
    /// `winning_path != request.path` test then calls that hit "regular", leaves
    /// `alternate_path` empty, and `Sample::loadedFromAlternatePath` stays empty
    /// where the synchronous path fills it — which is what `save_song_ui.cpp`
    /// gates the collect-media copy-back on, so the sample is silently not
    /// copied into the saved song folder. The C++ side removed that bug class by
    /// classifying positionally; this is the same rule on this side of the
    /// boundary.
    pub async fn load_io_once<O: LoadOps>(ops: &O, request: &LoadRequest) -> LoadResult {
        // The C++ side supplied only the policy (which paths, in what order);
        // trying them is I/O and belongs here.
        //
        // A candidate is taken only once it has BOTH opened and sized, matching
        // `tryOpenCandidate`, whose own `sz.has_value()` check treats a file that
        // opens but cannot be sized as a plain candidate miss and moves on. Taking
        // it on the open alone let an openable-but-unsizeable candidate shadow a
        // good later one and fail the whole load, on the async path only.
        let mut taken: Option<(u32, &str, u32, usize)> = None;
        for (index, candidate) in request.candidates.iter().enumerate() {
            let handle = match ops.open(candidate).await {
                OpenVerdict::Opened(handle) => handle,
                OpenVerdict::Missing => continue,
                // Not a verdict about this candidate: every remaining one would
                // hit the same full table. Stop, and report the resource failure
                // under its own identity so the collector can reclaim and retry
                // rather than reading it as "nothing is here" and probing the
                // alternate directory.
                OpenVerdict::TableFull => {
                    return LoadResult::failed(DELUGE_ERROR_TOO_MANY_OPEN_STREAMS);
                }
            };
            match ops.size_of(handle).await {
                Some(size_bytes) => {
                    taken = Some((handle, candidate.as_str(), size_bytes, index));
                    break;
                }
                None => ops.close(handle).await,
            }
        }
        let Some((handle, winning_path, size_bytes, index)) = taken else {
            return LoadResult::failed(DELUGE_ERROR_FILE_UNREADABLE);
        };

        let mut result = parse_open_file(ops, handle, size_bytes).await;

        // The single close, on the one path out of the parse.
        ops.close(handle).await;

        // `usingAlternateLocation` is empty exactly when the LAST candidate won —
        // the regular path's slot. See this function's doc comment for why this is
        // positional and must stay so.
        let is_regular_path = index + 1 == request.candidates.len();
        // `set_alternate_path` is the WRITE, not a test, and `&&` is what keeps it
        // one: it runs only when the first two hold, exactly as the nested form
        // did. Reordering these terms would call it on the regular path too.
        if result.error == DELUGE_ERROR_NONE
            && !is_regular_path
            && !result.set_alternate_path(winning_path)
        {
            // Cannot happen for a C-proposed candidate (bounded by
            // DELUGE_MAX_PATH), but a path we cannot report is a path the
            // collector would open at the wrong location.
            return LoadResult::failed(DELUGE_ERROR_FILE_UNREADABLE);
        }
        result
    }

    /// Drive the header windows of an open, already-sized file to a verdict.
    /// Never closes `handle` — see [`load_io_once`].
    async fn parse_open_file<O: LoadOps>(ops: &O, handle: u32, size_bytes: u32) -> LoadResult {
        let mut parser = HeaderParser::new(size_bytes);
        let mut window = [0u8; MAX_WINDOW_BYTES as usize];
        while let Some((start, len)) = parser.pending() {
            let len = len as usize;
            if len > window.len() {
                // Unreachable by contract: `MAX_WINDOW_BYTES` is a compile-time
                // promise the parser checks over every window it can ask for.
                // Refuse rather than panic if it is ever broken — a slice out of
                // range would take the whole executor down.
                return LoadResult::failed(DELUGE_ERROR_FILE_CORRUPTED);
            }
            // A zero-length request is legitimate: `pending` clamps `len` to what
            // remains of the file, so a walk positioned at or past the end asks
            // for nothing. It must still be READ and FED — the parser resolves
            // the end of the file inside `feed`, and never terminates without
            // one, so skipping a zero-length request stalls this loop forever.
            // A zero-length read is a no-op that trivially succeeds.
            if !ops.read_at(handle, start, &mut window[..len]).await {
                return LoadResult::failed(DELUGE_ERROR_FILE_UNREADABLE);
            }
            match parser.feed(&window[..len], start) {
                Ok(FeedResult::Done(header)) => {
                    return LoadResult::succeeded(
                        LoadGeometry::from(&header),
                        u64::from(size_bytes),
                    );
                }
                Ok(FeedResult::Need { .. }) => continue,
                Err(error) => return LoadResult::failed(error_code_for(error)),
            }
        }

        // `pending` went None without a `Done`: unreachable for a fresh parser
        // (its first phase is always non-terminal, and every terminal phase is
        // reported through `feed`), but a walk that ends with no header has no
        // geometry to publish.
        LoadResult::failed(DELUGE_ERROR_FILE_CORRUPTED)
    }
}

// Re-exported for `tests/audio_loader_host.rs`, which recompiles this file
// through `#[path]`: `driver` itself is private, and the bin target needs only
// some of these, so the rest read as unused here.
#[allow(unused_imports)]
pub use driver::{LoadOps, OpenVerdict, error_code_for, load_io_once};

// ── Production wiring ───────────────────────────────────────────────────────
// Only compiled where the efatfs backing is actually linked: on device under
// `efatfs_streaming`, and on host under `host_app`. Kept separate from the driver
// above (which compiles with no backing at all) so the
// `#[path]`-recompiled host test never emits a reference to an undefined symbol.
//
// Note what is NOT here: any `extern "C"` block. This half calls no C++ at all —
// see the module doc.

#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
mod prod {
    use super::driver::LoadOps;

    /// The efatfs handle table behind the production ops: the device mount on
    /// device, the host shim under `host_app`. One alias rather than a `#[cfg]` at
    /// every call site.
    #[cfg(target_os = "none")]
    use crate::efatfs_fs as backing;
    #[cfg(not(target_os = "none"))]
    use crate::efatfs_host_shim as backing;

    /// The real [`LoadOps`], wired to the efatfs streaming handle table. Holds no
    /// state: every operation is keyed by handle.
    ///
    pub struct ProdOps;

    impl ProdOps {
        pub fn new() -> Self {
            Self
        }
    }

    impl LoadOps for ProdOps {
        async fn open(&self, path: &str) -> super::driver::OpenVerdict {
            use super::driver::OpenVerdict;
            match backing::open(path).await {
                crate::efatfs_core::OpenOutcome::Handle(handle) => OpenVerdict::Opened(handle),
                // A candidate verdict: nothing to read at this path, so try the
                // next. `efatfs_fs::open` collapses every open failure -- missing,
                // unmounted, any granular status -- onto NotFound, which is the
                // same conflation `resolveFileSize`'s tryOpenCandidate makes (see
                // its own TODO(conflation-batch-5)).
                crate::efatfs_core::OpenOutcome::NotFound => OpenVerdict::Missing,
                // NOT a candidate verdict. The loader opens through the same 128-slot
                // `HANDLES` table `open_read_stream` uses, and `sample_stream.cpp`
                // documents that table filling up by construction under browser
                // preview -- this rung's own workload. Kept distinct all the way to
                // the collector so it reaches the reclaim rather than the
                // alternate-directory probe.
                crate::efatfs_core::OpenOutcome::TableFull => OpenVerdict::TableFull,
            }
        }

        async fn size_of(&self, handle: u32) -> Option<u32> {
            backing::size_of_handle(handle).await
        }

        async fn read_at(&self, handle: u32, offset: u32, buf: &mut [u8]) -> bool {
            backing::read_at(handle, offset, buf).await
        }

        async fn close(&self, handle: u32) {
            backing::close(handle).await;
        }
    }
}

#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
#[allow(unused_imports)]
pub use prod::ProdOps;

/// The load task: wakes on [`LOAD_WAKE`], drains every queued request through the
/// real [`ProdOps`], publishes each result, then goes back to sleep.
///
/// [`IN_FLIGHT`] is set before the I/O and cleared only after the result is
/// published, so [`deluge_audio_load_collect`] never sees a gap where a running
/// load looks unknown.
#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
#[embassy_executor::task]
pub async fn audio_load_task() {
    LOAD_WAKE.wait().await;
    let ops = ProdOps::new();
    loop {
        while let Some((request, generation)) = take_next_request().await {
            // After the request is marked in flight, so a collect during the hold answers PENDING,
            // exactly as it would while a slow card was reading the file.
            #[cfg(feature = "host_app")]
            {
                let ms = LATENCY_MS.load(core::sync::atomic::Ordering::Relaxed);
                if ms > 0 {
                    embassy_time::Timer::after_millis(u64::from(ms)).await;
                }
            }
            let key = request.path.clone();
            let result = load_io_once(&ops, &request).await;
            // Publish BEFORE clearing IN_FLIGHT, never after: the entry is briefly
            // in both tables, which is harmless, whereas the reverse leaves a
            // window in which it is in neither and `collect` would report
            // `UNKNOWN` for a load that just succeeded.
            //
            // `generation` was sampled before the I/O above, so a card pulled
            // during it is what makes this publish a no-op.
            publish_result(key, result, generation).await;
            mark_in_flight(None).await;
        }
        LOAD_WAKE.wait().await;
    }
}

/// Pop the next queued request and mark it in flight, atomically with respect to
/// [`deluge_audio_load_collect`], and report the card generation the load begins
/// under.
///
/// The mark happens while the [`QUEUE`] lock is still held, so there is no
/// instant at which the request has left the queue but not yet appeared as
/// in-flight. A `collect` racing this either sees it queued, or blocks on the
/// queue's `try_lock` and reports `PENDING` — never `UNKNOWN`.
///
/// The generation is read HERE, before the first `await` that can suspend this
/// task and therefore before any I/O, because that is what makes it mean "the
/// card this load is reading". Reading it later — at publish time — reads the
/// value a flush has already installed and defeats the check entirely.
#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
async fn take_next_request() -> Option<(LoadRequest, u32)> {
    let mut queue = QUEUE.lock().await;
    let request = queue.pop_front()?;
    let generation = deluge_card_generation::current();
    mark_in_flight(Some(request.path.clone())).await;
    Some((request, generation))
}
