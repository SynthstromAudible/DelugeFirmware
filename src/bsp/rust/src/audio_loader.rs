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
//! ## Two compilation tiers
//!
//! Same shape, and for the same reason, as `streaming_loader.rs`: the selector
//! the C++ side calls (`deluge_audio_load_async_active`) must resolve on the
//! Embassy BSP whether or not the async backing is enabled, because a cargo
//! feature cannot reach a C++ `#define`.
//!
//! - Always compiled whenever this module is (the Embassy BSP: device, or host
//!   under `host_app` — see the `mod audio_loader;` cfg in `main.rs`): the
//!   request/collect C ABI ([`deluge_audio_load_request`],
//!   [`deluge_audio_load_collect`], [`deluge_audio_load_async_active`]) and the
//!   queue/result tables behind it. These override the `__attribute__((weak))`
//!   C++ fallbacks in `audio_load_abi.cpp`; every other BSP (legacy/cooperative
//!   sim, rza1) never links this crate, resolves those fallbacks, and always
//!   takes the synchronous path.
//! - Feature-gated behind `async_audio_load`: the parse driver itself
//!   ([`LoadOps`], [`load_io_once`], `ProdOps`, [`audio_load_task`]).
//!
//! Compiling the driver and *using* it are two different switches.
//! `async_audio_load` (on by default) only builds the driver;
//! `async_audio_load_active` (off by default) is the sole input to
//! [`deluge_audio_load_async_active`], and therefore the only thing that makes
//! the C++ side dispatch a load here at all.
//!
//! ## What the off build costs: nothing
//!
//! The switch gates the STORAGE and the task, not just the behaviour. With it
//! off, `QUEUE`/`RESULTS`/`IN_FLIGHT` do not exist, the task is neither compiled
//! nor spawned, and [`deluge_audio_load_request`]/[`deluge_audio_load_collect`]
//! refuse exactly as `audio_load_abi.cpp`'s weak fallbacks do.
//!
//! That distinction is not cosmetic. These statics live in `.bss` — **on-chip
//! SRAM**, not the SDRAM the app's bulk globals sit in — and they shared a budget
//! with the C++ allocator's `FAST_INTERNAL` heap, whose size steps down as the
//! image grows. Holding ~16 KB of that for a feature nothing can enter was paying
//! a scarce resource for an unreachable path.
//!
//! What stays compiled with the switch off is the *code*: the driver, `ProdOps`,
//! the layout assertions and the FFI entry points, so the default build still
//! type-checks this module against `efatfs_fs` and still exports strong symbols
//! over the weak C++ fallbacks. The task itself is type-checked by the
//! switch-on build, which is a standing gate.
//!
//! ## Why the driver is generic over [`LoadOps`]
//!
//! Same constraint as `streaming_loader`: `host_app` (which links the host-built
//! C++ app closure) and this crate's integration tests do not co-compile, so the
//! orchestration takes its filesystem operations through a trait. `ProdOps` wires
//! that to `crate::efatfs_fs` / `crate::efatfs_host_shim`, and
//! `tests/audio_loader_host.rs` wires it to an in-memory double.

// `async_audio_load_active` gates the accepting `enqueue_request` and the
// QUEUE/IN_FLIGHT/RESULTS statics; `audio_load_task` (the only thing that drains
// them) additionally needs a backing to do I/O through, and on device that is
// `efatfs_streaming`. Turned on without one, this crate builds cleanly, accepts
// every request, and drains none of them: the C++ collector reports
// LOAD_IN_PROGRESS forever, on every tick, for every sample -- silence with no
// error, which is the exact failure shape the request path's permanent-decline
// handling exists to avoid. Unreachable through the default features (which carry
// `efatfs_streaming`), but nothing stopped `--no-default-features --features
// async_audio_load_active` on a device build, so refuse it at compile time rather
// than leaving the livelock as a latent build configuration.
//
// Scoped to the DEVICE build deliberately. On host the module is reached two ways:
// through `main.rs` under `host_app` (which is itself a backing, so the condition
// is already satisfied), and through `tests/audio_loader_host.rs`, which
// `#[path]`-recompiles this file with neither backing and for which
// `cargo test --features async_audio_load_active` is a standing gate -- it drives
// the queue/result statics by hand and needs no task at all.
#[cfg(all(
    feature = "async_audio_load_active",
    target_os = "none",
    not(feature = "efatfs_streaming")
))]
compile_error!(
    "feature `async_audio_load_active` needs `efatfs_streaming` on device: `audio_load_task` is \
     the only drain for the request queue and is not compiled without it, so every accepted \
     request would stay queued forever and the C++ side would see LOAD_IN_PROGRESS on every tick."
);

use core::ffi::{CStr, c_char};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
#[cfg(feature = "async_audio_load_active")]
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
/// belongs to.
pub struct PendingResult {
    key: LoadPath,
    result: LoadResult,
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

#[cfg(feature = "async_audio_load_active")]
static QUEUE: Mutex<CriticalSectionRawMutex, RequestQueue> = Mutex::new(heapless::Deque::new());

/// The path the task is parsing right now, if any. Exists so
/// [`deluge_audio_load_collect`] can answer `PENDING` rather than `UNKNOWN` in
/// the window between a request leaving [`QUEUE`] and its result reaching
/// [`RESULTS`] — an `UNKNOWN` there would tell the C++ side to re-request work
/// that is already running, and it would do so on every tick.
#[cfg(feature = "async_audio_load_active")]
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
#[cfg(feature = "async_audio_load_active")]
static RESULTS: Mutex<CriticalSectionRawMutex, ResultTable> = Mutex::new(heapless::Deque::new());

/// Whether the Rust async loader backs sample loads on this BSP/config — the
/// switch the C++ side asks before choosing the async path over the synchronous
/// one. The symbol itself must always exist so that call site links regardless of
/// which config produced this BSP image; only the return value moves.
///
/// Keyed on `async_audio_load_active`, NOT on `async_audio_load`, and the
/// separation is deliberate. `async_audio_load` says the driver is *compiled*
/// (which keeps the host test running under a plain `cargo test` and keeps the
/// device link honest); this says card samples actually *load through it*, which
/// is a behavioural change and must be an explicit, reversible flip rather than a
/// side effect of the code landing. Off by default.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_load_async_active() -> bool {
    cfg!(feature = "async_audio_load_active")
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
#[cfg(feature = "async_audio_load_active")]
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

/// No loader on this build: refuse, exactly as `audio_load_abi.cpp`'s weak
/// fallback does. Nothing reaches here in practice — the C++ side gates on
/// [`deluge_audio_load_async_active`], which is false in this configuration —
/// and refusing is what keeps a caller that ignores the selector from waiting
/// for a result nothing will produce.
///
/// The queue and result tables do not exist here at all; see the module doc's
/// "What the off build costs".
#[cfg(not(feature = "async_audio_load_active"))]
fn enqueue_request(_request: LoadRequest) -> bool {
    false
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

    match probe(&path) {
        Probe::Ready(result) => {
            // SAFETY: `out` is a valid writable `LoadResult` (checked non-null above).
            unsafe { core::ptr::write(out, result) };
            DELUGE_AUDIO_LOAD_READY
        }
        Probe::Pending => DELUGE_AUDIO_LOAD_PENDING,
        Probe::Unknown => DELUGE_AUDIO_LOAD_UNKNOWN,
    }
}

/// What one forward pass over the pipeline found. Deliberately produced by a
/// SINGLE pass: splitting "is it ready?" and "is it still coming?" into two
/// passes reintroduces the very race this ordering exists to close, because the
/// loader can publish between them and the second pass then reports `Unknown`
/// for a result the first pass merely missed.
enum Probe {
    Ready(LoadResult),
    Pending,
    Unknown,
}

/// One forward pass: QUEUE, then IN_FLIGHT, then RESULTS. See
/// [`deluge_audio_load_collect`]'s doc for why the order and the contention
/// mapping are both load-bearing.
#[cfg(feature = "async_audio_load_active")]
fn probe(path: &str) -> Probe {
    if is_queued(path) || is_in_flight(path) {
        return Probe::Pending;
    }
    // A contended table means the loader is mid-publish. `Pending` is the honest
    // answer: never claim `Unknown` on contention, or the caller re-requests work
    // that is about to land.
    let Ok(mut results) = RESULTS.try_lock() else {
        return Probe::Pending;
    };
    match take_from(&mut results, path) {
        Some(result) => Probe::Ready(result),
        None => Probe::Unknown,
    }
}

/// No loader on this build: nothing was ever requested, so nothing is coming.
/// `Unknown`, never `Pending` — `Pending` would tell a caller to keep asking
/// forever. Matches `audio_load_abi.cpp`'s weak fallback.
#[cfg(not(feature = "async_audio_load_active"))]
fn probe(_path: &str) -> Probe {
    Probe::Unknown
}

/// Whether `path` is still waiting in [`QUEUE`].
///
/// **Contention reports `true`, and that is load-bearing, not defensive.** The
/// loader marks a request in flight while still holding this lock, so a `collect`
/// racing that handoff finds the lock taken — mapping it to `false` here would
/// report `UNKNOWN` for a request mid-handoff, which is exactly the bug the probe
/// order exists to prevent. Do not "simplify" this branch.
#[cfg(feature = "async_audio_load_active")]
fn is_queued(path: &str) -> bool {
    match QUEUE.try_lock() {
        Ok(queue) => queue.iter().any(|request| request.path == path),
        Err(_) => true,
    }
}

/// Whether `path` is the load being parsed right now. Contention reports `true`,
/// for the same reason as [`is_queued`] — and with the same warning.
#[cfg(feature = "async_audio_load_active")]
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
#[cfg(feature = "async_audio_load_active")]
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

/// Publish `result` under `key` into `table`, replacing any existing entry for
/// that key and evicting the oldest if the table is full. See [`RESULTS`].
///
/// Takes the table by reference rather than reaching for the static, so the
/// eviction and replace behaviour is compiled and unit-tested on every build —
/// including the ones where [`RESULTS`] does not exist. That matters: the
/// `remove_at` ordering bug lived in exactly this code and was invisible to
/// every test that ran against a single-entry table.
pub fn publish_into(table: &mut ResultTable, key: LoadPath, result: LoadResult) {
    if let Some(index) = table.iter().position(|entry| entry.key == key) {
        let _ = remove_at(table, index);
    }
    if table.is_full() {
        let _ = table.pop_front();
    }
    let _ = table.push_back(PendingResult { key, result });
}

/// Take `path`'s result out of `table`, if it holds one. Removing is the point:
/// a second collect must report `UNKNOWN`, or a caller could build the same
/// `Sample` twice.
pub fn take_from(table: &mut ResultTable, path: &str) -> Option<LoadResult> {
    let index = table.iter().position(|entry| entry.key == path)?;
    Some(remove_at(table, index).result)
}

/// Publish into the live [`RESULTS`] table.
#[cfg(feature = "async_audio_load_active")]
pub async fn publish_result(key: LoadPath, result: LoadResult) {
    let mut table = RESULTS.lock().await;
    publish_into(&mut table, key, result);
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

#[cfg(feature = "async_audio_load")]
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
    /// promoted `alternateLoadDirStatus` for every later sample of the load, and
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
    /// `alternateAudioFileLoadPath` happens to be the file's own directory, the
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
        if result.error == DELUGE_ERROR_NONE && !is_regular_path {
            if !result.set_alternate_path(winning_path) {
                // Cannot happen for a C-proposed candidate (bounded by
                // DELUGE_MAX_PATH), but a path we cannot report is a path the
                // collector would open at the wrong location.
                return LoadResult::failed(DELUGE_ERROR_FILE_UNREADABLE);
            }
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
#[cfg(feature = "async_audio_load")]
#[allow(unused_imports)]
pub use driver::{LoadOps, OpenVerdict, error_code_for, load_io_once};

// ── Production wiring ───────────────────────────────────────────────────────
// Only compiled where the efatfs backing is actually linked: on device under
// `efatfs_streaming`, and on host under `host_app`. Kept separate from the driver
// above (which compiles under `async_audio_load` alone) so the
// `#[path]`-recompiled host test never emits a reference to an undefined symbol.
//
// Note what is NOT here: any `extern "C"` block. This half calls no C++ at all —
// see the module doc.

#[cfg(all(
    feature = "async_audio_load",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
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
    /// Compiled under `async_audio_load` even when the switch is off, so the
    /// default build still type-checks this wiring against `efatfs_fs` — the
    /// link-checking value of building the module at all. Unused in that
    /// configuration, hence the allow.
    #[allow(dead_code)]
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

#[cfg(all(
    feature = "async_audio_load",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
#[allow(unused_imports)]
pub use prod::ProdOps;

/// The load task: wakes on [`LOAD_WAKE`], drains every queued request through the
/// real [`ProdOps`], publishes each result, then goes back to sleep.
///
/// [`IN_FLIGHT`] is set before the I/O and cleared only after the result is
/// published, so [`deluge_audio_load_collect`] never sees a gap where a running
/// load looks unknown.
#[cfg(all(
    feature = "async_audio_load_active",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
#[embassy_executor::task]
pub async fn audio_load_task() {
    LOAD_WAKE.wait().await;
    let ops = ProdOps::new();
    loop {
        while let Some(request) = take_next_request().await {
            let key = request.path.clone();
            let result = load_io_once(&ops, &request).await;
            // Publish BEFORE clearing IN_FLIGHT, never after: the entry is briefly
            // in both tables, which is harmless, whereas the reverse leaves a
            // window in which it is in neither and `collect` would report
            // `UNKNOWN` for a load that just succeeded.
            publish_result(key, result).await;
            mark_in_flight(None).await;
        }
        LOAD_WAKE.wait().await;
    }
}

/// Pop the next queued request and mark it in flight, atomically with respect to
/// [`deluge_audio_load_collect`].
///
/// The mark happens while the [`QUEUE`] lock is still held, so there is no
/// instant at which the request has left the queue but not yet appeared as
/// in-flight. A `collect` racing this either sees it queued, or blocks on the
/// queue's `try_lock` and reports `PENDING` — never `UNKNOWN`.
#[cfg(all(
    feature = "async_audio_load_active",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
async fn take_next_request() -> Option<LoadRequest> {
    let mut queue = QUEUE.lock().await;
    let request = queue.pop_front()?;
    mark_in_flight(Some(request.path.clone())).await;
    Some(request)
}
