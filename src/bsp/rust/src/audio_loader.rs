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
//! `async_audio_load` (on by default) only builds and spawns the task;
//! `async_audio_load_active` (off by default) is the sole input to
//! [`deluge_audio_load_async_active`], and therefore the only thing that makes
//! the C++ side dispatch a load here at all. With the switch off nothing ever
//! requests, so the task waits on [`LOAD_WAKE`] forever.
//!
//! ## Why the driver is generic over [`LoadOps`]
//!
//! Same constraint as `streaming_loader`: `host_app` (which links the host-built
//! C++ app closure) and this crate's integration tests do not co-compile, so the
//! orchestration takes its filesystem operations through a trait. `ProdOps` wires
//! that to `crate::efatfs_fs` / `crate::efatfs_host_shim`, and
//! `tests/audio_loader_host.rs` wires it to an in-memory double.

// The request path and its candidate list are heap strings: the path bound
// (`DELUGE_MAX_PATH`, 256) times the candidate bound (3) is 768 bytes, which is
// more than belongs in an interrupt-executor stack frame or a task future. This
// module compiles both `no_std` (device) and `std` (the `#[path]`-recompiled
// host test), and `extern crate` visibility is per-module in Rust 2018+, so this
// cannot rely on another module's `extern crate alloc;`.
extern crate alloc;

use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::{CStr, c_char};

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

/// How many queued loads are held before further requests are refused. A refusal
/// is safe: the caller has already been told `LOAD_IN_PROGRESS` and its next
/// cursor move re-requests.
const LOAD_QUEUE_DEPTH: usize = 8;

/// How many finished-but-uncollected results are held. See [`RESULTS`] for what
/// happens when this fills.
const RESULT_SLOTS: usize = 4;

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

/// `deluge_audio_load_collect`: queued or in flight; ask again, do not re-request.
pub const DELUGE_AUDIO_LOAD_PENDING: u8 = 0;
/// `deluge_audio_load_collect`: a result was written out and removed.
pub const DELUGE_AUDIO_LOAD_READY: u8 = 1;
/// `deluge_audio_load_collect`: nothing known about this path.
pub const DELUGE_AUDIO_LOAD_UNKNOWN: u8 = 2;

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

/// `DelugeAudioLoadResult`'s layout, pinned.
///
/// This boundary is hand-declared on both sides — `audio_load.h` is not in
/// `wrapper.h`, so bindgen does not generate this struct and a field reorder on
/// either side would compile clean and misread every field after it. These
/// checks catch a Rust-side change; the C side is held by the header being the
/// single place the field order is written down.
const _: () = {
    assert!(core::mem::size_of::<LoadGeometry>() == 36);
    assert!(core::mem::size_of::<LoadResult>() == 312);
    assert!(core::mem::align_of::<LoadResult>() == 8);
    assert!(core::mem::offset_of!(LoadResult, geometry) == 0);
    assert!(core::mem::offset_of!(LoadResult, size_bytes) == 40);
    assert!(core::mem::offset_of!(LoadResult, alternate_path) == 48);
    assert!(core::mem::offset_of!(LoadResult, error) == 304);
};

/// One queued load: the nominal path (the key a result is collected by) and the
/// candidate paths to try, in order.
pub struct LoadRequest {
    pub path: String,
    pub candidates: Vec<String>,
}

/// A finished parse waiting to be collected, tagged with the nominal path it
/// belongs to.
struct PendingResult {
    key: String,
    result: LoadResult,
}

/// Raised to wake [`audio_load_task`] when a load is newly requested. Always
/// compiled — with the driver absent, or before the task is spawned, nobody
/// awaits it and signalling is a harmless flag set.
pub static LOAD_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// The bounded request queue [`deluge_audio_load_request`] pushes into and
/// [`audio_load_task`] drains.
static QUEUE: Mutex<CriticalSectionRawMutex, heapless::Deque<LoadRequest, LOAD_QUEUE_DEPTH>> =
    Mutex::new(heapless::Deque::new());

/// The path the task is parsing right now, if any. Exists so
/// [`deluge_audio_load_collect`] can answer `PENDING` rather than `UNKNOWN` in
/// the window between a request leaving [`QUEUE`] and its result reaching
/// [`RESULTS`] — an `UNKNOWN` there would tell the C++ side to re-request work
/// that is already running, and it would do so on every tick.
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<String>> = Mutex::new(None);

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
static RESULTS: Mutex<CriticalSectionRawMutex, heapless::Deque<PendingResult, RESULT_SLOTS>> =
    Mutex::new(heapless::Deque::new());

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
    let Some(path) = (unsafe { cstr_to_string(file_path) }) else {
        return false;
    };
    if candidates.is_null() {
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

    // `try_lock` rather than a block: this is called synchronously from C++ in
    // whatever context the app happens to be in, and must never wait.
    let Ok(mut queue) = QUEUE.try_lock() else {
        return false;
    };
    if queue.push_back(LoadRequest { path, candidates }).is_err() {
        return false; // Queue full; the caller re-requests on its next tick.
    }
    drop(queue);
    LOAD_WAKE.signal(());
    true
}

/// Collect the finished parse for `file_path`, if one is ready. See
/// `audio_load.h`.
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
    let Some(path) = (unsafe { cstr_to_string(file_path) }) else {
        return DELUGE_AUDIO_LOAD_UNKNOWN;
    };

    // A contended table means the task is mid-publish. `PENDING` is the honest
    // answer: never claim `UNKNOWN` on contention, or the caller re-requests work
    // that is about to land.
    let Ok(mut results) = RESULTS.try_lock() else {
        return DELUGE_AUDIO_LOAD_PENDING;
    };
    if let Some(index) = results.iter().position(|entry| entry.key == path) {
        let taken = remove_at(&mut results, index);
        // SAFETY: `out` is a valid writable `LoadResult` (checked non-null above).
        unsafe { core::ptr::write(out, taken.result) };
        return DELUGE_AUDIO_LOAD_READY;
    }
    drop(results);

    if is_awaiting_parse(&path) {
        return DELUGE_AUDIO_LOAD_PENDING;
    }
    DELUGE_AUDIO_LOAD_UNKNOWN
}

/// Whether `path` is queued or currently being parsed. Contention on either table
/// reports `true`: the loader is demonstrably busy, so "ask again" is right and
/// "never heard of it" would be wrong.
fn is_awaiting_parse(path: &str) -> bool {
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => {
            if in_flight.as_deref() == Some(path) {
                return true;
            }
        }
        Err(_) => return true,
    }
    match QUEUE.try_lock() {
        Ok(queue) => queue.iter().any(|request| request.path == path),
        Err(_) => true,
    }
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

/// Publish `result` under `key`, replacing any existing entry for that key and
/// evicting the oldest if the table is full. See [`RESULTS`].
pub async fn publish_result(key: String, result: LoadResult) {
    let mut results = RESULTS.lock().await;
    if let Some(index) = results.iter().position(|entry| entry.key == key) {
        let _ = remove_at(&mut results, index);
    }
    if results.is_full() {
        let _ = results.pop_front();
    }
    let _ = results.push_back(PendingResult { key, result });
}

/// Decode the candidate buffer the C side filled: one NUL-terminated path per
/// [`DELUGE_MAX_PATH`] slot. `None` if any slot is unterminated or not UTF-8.
pub fn decode_candidates(buffer: &[u8]) -> Option<Vec<String>> {
    let slots = buffer.len() / DELUGE_MAX_PATH;
    let mut out = Vec::with_capacity(slots);
    for slot in 0..slots {
        let bytes = &buffer[slot * DELUGE_MAX_PATH..(slot + 1) * DELUGE_MAX_PATH];
        // Anything after the NUL is the caller's untouched buffer, never read.
        let end = bytes.iter().position(|b| *b == 0)?;
        out.push(String::from(core::str::from_utf8(&bytes[..end]).ok()?));
    }
    Some(out)
}

/// Copy a C string out to an owned [`String`]. `None` for NULL or non-UTF-8.
///
/// # Safety
///
/// `ptr` must be NULL or a valid NUL-terminated C string.
unsafe fn cstr_to_string(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: the caller's contract, above.
    let cstr = unsafe { CStr::from_ptr(ptr) };
    cstr.to_str().ok().map(String::from)
}

// ── The parse driver ────────────────────────────────────────────────────────

#[cfg(feature = "async_audio_load")]
mod driver {
    // `extern crate` visibility is per-module in Rust 2018+, so the outer
    // module's declaration is not in scope here.
    extern crate alloc;

    use super::{
        DELUGE_ERROR_FILE_CORRUPTED, DELUGE_ERROR_FILE_UNREADABLE, DELUGE_ERROR_FILE_UNSUPPORTED,
        DELUGE_ERROR_NONE, LoadGeometry, LoadRequest, LoadResult,
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

    /// The filesystem operations [`load_io_once`] needs. **All four are `async`
    /// and none reaches C++** — that is the module doc's single rule, expressed as
    /// a type. Adding a synchronous C++ call to this trait would reintroduce the
    /// interrupt-priority deadlock the split exists to remove.
    pub trait LoadOps {
        /// Open `path` for reading, resolving to an efatfs handle. `None` if it
        /// could not be opened (missing, unmounted, or the handle table is full).
        async fn open(&self, path: &str) -> Option<u32>;

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

    /// Run one queued load's I/O: try each candidate in order, size the one that
    /// opens, and walk its header to a verdict.
    ///
    /// Always returns a [`LoadResult`] — success or failure, there is always
    /// something for the C++ side to collect and act on. The efatfs handle is
    /// closed on the single path out of the parse, so no exit can leak it.
    pub async fn load_io_once<O: LoadOps>(ops: &O, request: &LoadRequest) -> LoadResult {
        // One open for the whole load. The C++ side supplied only the policy
        // (which paths, in what order); trying them is I/O and belongs here.
        let mut opened: Option<(u32, &str)> = None;
        for candidate in &request.candidates {
            if let Some(handle) = ops.open(candidate).await {
                opened = Some((handle, candidate.as_str()));
                break;
            }
        }
        let Some((handle, winning_path)) = opened else {
            return LoadResult::failed(DELUGE_ERROR_FILE_UNREADABLE);
        };

        let mut result = parse_open_file(ops, handle).await;

        // The single close, on the one path out of the parse.
        ops.close(handle).await;

        // `usingAlternateLocation` is empty exactly when the regular path won.
        if result.error == DELUGE_ERROR_NONE && winning_path != request.path {
            if !result.set_alternate_path(winning_path) {
                // Cannot happen for a C-proposed candidate (bounded by
                // DELUGE_MAX_PATH), but a path we cannot report is a path the
                // collector would open at the wrong location.
                return LoadResult::failed(DELUGE_ERROR_FILE_UNREADABLE);
            }
        }
        result
    }

    /// Size the open file and drive the header windows to a verdict. Never closes
    /// `handle` — see [`load_io_once`].
    async fn parse_open_file<O: LoadOps>(ops: &O, handle: u32) -> LoadResult {
        let Some(size_bytes) = ops.size_of(handle).await else {
            return LoadResult::failed(DELUGE_ERROR_FILE_UNREADABLE);
        };

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
pub use driver::{LoadOps, error_code_for, load_io_once};

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
    pub struct ProdOps;

    impl ProdOps {
        pub fn new() -> Self {
            Self
        }
    }

    impl LoadOps for ProdOps {
        async fn open(&self, path: &str) -> Option<u32> {
            match backing::open(path).await {
                crate::efatfs_core::OpenOutcome::Handle(handle) => Some(handle),
                // Missing, unmounted, or the handle table is full. All three end
                // this candidate; the next one (if any) is tried.
                _ => None,
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
pub use prod::ProdOps;

/// The load task: wakes on [`LOAD_WAKE`], drains every queued request through the
/// real [`ProdOps`], publishes each result, then goes back to sleep.
///
/// [`IN_FLIGHT`] is set before the I/O and cleared only after the result is
/// published, so [`deluge_audio_load_collect`] never sees a gap where a running
/// load looks unknown.
#[cfg(all(
    feature = "async_audio_load",
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
        while let Some(request) = next_queued_request().await {
            let key = request.path.clone();
            *IN_FLIGHT.lock().await = Some(key.clone());
            let result = load_io_once(&ops, &request).await;
            publish_result(key, result).await;
            *IN_FLIGHT.lock().await = None;
        }
        LOAD_WAKE.wait().await;
    }
}

/// Pop the next queued request, or `None` when the queue is empty.
#[cfg(all(
    feature = "async_audio_load",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
async fn next_queued_request() -> Option<LoadRequest> {
    QUEUE.lock().await.pop_front()
}
