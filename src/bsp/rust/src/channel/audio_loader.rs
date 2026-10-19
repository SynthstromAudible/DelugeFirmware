//! The async audio-file load task: the I/O half of a card sample's load.
//!
//! `AudioFileManager::buildAudioFileFromCard` splits a sample load into a C++ prologue (adopt a
//! `Sample`), this I/O (open a candidate path, size it, walk its header), and a C++ epilogue (apply
//! the geometry). The C++ side requests with [`deluge_audio_load_request`] and polls
//! [`deluge_audio_load_collect`] on its own tier; this module never calls into C++.
//!
//! # Tier contract
//!
//! On device the task runs on the preemptive streaming interrupt executor (a `std::thread` under
//! `host_app`), and it holds the efatfs FS mutex across SD awaits (`with_fs`). It therefore must
//! never spin or call C++: a synchronous C++ storage call `block_on`-spins, and at interrupt
//! priority that spin cannot be preempted by the thread-mode context holding the mutex, so it
//! deadlocks; a C++ call may also allocate or mutate model state while thread mode is
//! mid-mutation. The rule is: everything here awaits, nothing spins, nothing calls C++. That is
//! why [`LoadOps`] is four `async` filesystem operations, and why the candidate
//! list arrives in the request (building it probes a directory and allocates, so it runs on the
//! requesting tier). This half owns no C++ object; its only resource is the efatfs handle, closed
//! on the single path out of [`load_io_once`].
//!
//! # Card swaps
//!
//! Results are keyed by path, and a path outlives its card: a header parsed from card A must not be
//! applied to card B's file at the same path. Each load is stamped with the
//! [card generation](crate::storage::card::generation) it began under (sampled in
//! `take_next_request`, before any I/O — sampling at publish time would read the post-flush value
//! and let a stale result through). [`take_from`] returns an entry only when its stamp matches the
//! taker's current generation, and drops a mismatched one. That check at take is the guarantee;
//! `deluge_card_flush`'s table clears (`flush_tables`) are `try_lock`ed housekeeping that may
//! fail, and a load in flight across the flush publishes afterwards anyway.
//!
//! # Build coverage
//!
//! The tables, the C ABI and the driver ([`LoadOps`], [`load_io_once`]) build everywhere, and are
//! the only definitions of these symbols — there is no C++ fallback. `ProdOps` and
//! `audio_load_task` need the efatfs backing, so they build only on device and under `host_app`;
//! `tests/audio_loader_host.rs` drives the driver through an in-memory [`LoadOps`]. The tables live
//! in `.bss`, on-chip SRAM shared with the C++ `FAST_INTERNAL` heap.

use core::cell::RefCell;
use core::ffi::{CStr, c_char};
use core::mem::MaybeUninit;

use crate::channel::{
    DELUGE_CHANNEL_PENDING, DELUGE_CHANNEL_READY, DELUGE_CHANNEL_UNKNOWN, DELUGE_PATH_MAX,
    DelugeChannelStatus,
};
pub use deluge_sample_fill::FileLocation;
use embassy_sync::blocking_mutex::Mutex as BlockingMutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;

/// `AudioFileManager::kMaxLoadCandidates`: the most candidates the C side proposes.
const MAX_LOAD_CANDIDATES: usize = 3;

/// The candidate policy's "buffer is untrustworthy" sentinel (a candidate did not fit, or an
/// allocation failed). Never a legitimate count; a request carrying it is refused, not clamped.
pub(crate) const DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED: u32 = 0xFFFF_FFFF;

/// One path, stored inline.
///
/// Nothing in this module may touch the Rust global heap. That heap (`DelugeGlobalAlloc` over
/// `EFATFS_ARENA`) assumes single-threaded use, and this task preempts thread mode, so an
/// allocation here could corrupt the TLSF free lists mid-`malloc`.
pub type LoadPath = heapless::String<MAX_PATH_CHARS>;

/// Longest path in characters: [`DELUGE_PATH_MAX`] minus the NUL.
///
/// Sizing [`LoadPath`] at `DELUGE_PATH_MAX` would accept a 256-character key that
/// [`LoadResult::set_alternate_path`] then refuses, turning a loadable file into
/// `FILE_UNREADABLE`.
pub(crate) const MAX_PATH_CHARS: usize = DELUGE_PATH_MAX - 1;

/// The candidate paths for one load, stored inline.
pub type CandidateList = heapless::Vec<LoadPath, MAX_LOAD_CANDIDATES>;

/// Queued loads held before further requests are refused. A refusal is safe: the caller
/// re-requests on its next tick.
const LOAD_QUEUE_DEPTH: usize = 8;

/// Finished-but-uncollected results held.
///
/// At least `LOAD_QUEUE_DEPTH`, so a fan-out of accepted requests cannot evict its own earliest
/// results and re-read those headers every round.
pub const RESULT_SLOTS: usize = LOAD_QUEUE_DEPTH;

const _: () = assert!(
    RESULT_SLOTS >= LOAD_QUEUE_DEPTH,
    "the result table must be able to report every request the queue accepts"
);

// Deluge `Error` codes as they cross the ABI, as fixed numbers (`audio_load.h`). `enum class
// Error` has no fixed underlying type; `sample_build.cpp` pins the same numbers with
// `static_assert`s.
/// `Error::NONE`.
pub const DELUGE_ERROR_NONE: u8 = 0;
/// `Error::FILE_CORRUPTED`.
pub const DELUGE_ERROR_FILE_CORRUPTED: u8 = 5;
/// `Error::FILE_UNREADABLE` (also "not found").
pub const DELUGE_ERROR_FILE_UNREADABLE: u8 = 6;
/// `Error::FILE_UNSUPPORTED`.
pub const DELUGE_ERROR_FILE_UNSUPPORTED: u8 = 7;
/// `Error::TOO_MANY_OPEN_STREAMS`: the efatfs handle table had no free slot.
///
/// Kept distinct from [`DELUGE_ERROR_FILE_UNREADABLE`], which the collector answers by probing
/// the alternate directory — the wrong recovery for resource exhaustion, and one that can load a
/// different file. This code routes to the reclaim instead.
pub const DELUGE_ERROR_TOO_MANY_OPEN_STREAMS: u8 = 34;

// C++ values no header declares, so bindgen cannot pin them: asserting each against its literal
// makes a change take two deliberate edits. `MAX_LOAD_CANDIDATES` matters most — it sets how many
// candidate slots are read.
const _: () = {
    assert!(MAX_LOAD_CANDIDATES == 3); // AudioFileManager::kMaxLoadCandidates
    assert!(DELUGE_ERROR_NONE == 0);
    assert!(DELUGE_ERROR_FILE_CORRUPTED == 5);
    assert!(DELUGE_ERROR_FILE_UNREADABLE == 6);
    assert!(DELUGE_ERROR_FILE_UNSUPPORTED == 7);
    assert!(DELUGE_ERROR_TOO_MANY_OPEN_STREAMS == 34);
};

/// The parsed header geometry; mirrors `DelugeAudioLoadGeometry` (`audio_load.h`).
///
/// Named fields rather than positional scalars, so a transposition of two same-typed values
/// cannot compile.
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
    /// `RawDataFormat`'s numeric value. The C++ side refuses an out-of-range byte rather than
    /// defaulting it.
    pub raw_data_format: u8,
    pub midi_note_present: u8,
    pub wavetable_explicitly_specifies_self: u8,
}

/// A finished parse; mirrors `DelugeAudioLoadResult` (`audio_load.h`).
///
/// Plain data that owns nothing, so an uncollected result can be discarded without leaking.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LoadResult {
    /// Valid only when `error` is [`DELUGE_ERROR_NONE`].
    pub geometry: LoadGeometry,
    pub size_bytes: u64,
    /// The alternate location the bytes were found at, NUL-terminated, or an
    /// empty string when the regular path won.
    pub alternate_path: [u8; DELUGE_PATH_MAX],
    pub error: u8,
    /// The candidates key of the request this answers. A collect is handed the result only when it presents the same
    /// key; see [`take_from`].
    pub candidates_key: u32,
    /// Where the file the verdict was reached on is, so the collector opens its read stream without looking the path
    /// up again. Valid only when `error` is [`DELUGE_ERROR_NONE`].
    pub location: FileLocation,
}

/// The result slot type in `deluge_audio_load_collect`'s signature: bindgen's
/// `DelugeAudioLoadResult` where there is bindgen output (so `abi_pin!` holds), else
/// [`LoadResult`]. The export writes through a cast, so the two must share a layout.
#[cfg(has_app)]
type AbiLoadResult = crate::sys::DelugeAudioLoadResult;
#[cfg(not(has_app))]
type AbiLoadResult = LoadResult;

#[cfg(has_app)]
const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<AbiLoadResult>() == size_of::<LoadResult>());
    assert!(align_of::<AbiLoadResult>() == align_of::<LoadResult>());
    assert!(offset_of!(AbiLoadResult, geometry) == offset_of!(LoadResult, geometry));
    assert!(offset_of!(AbiLoadResult, size_bytes) == offset_of!(LoadResult, size_bytes));
    assert!(offset_of!(AbiLoadResult, alternate_path) == offset_of!(LoadResult, alternate_path));
    assert!(offset_of!(AbiLoadResult, error) == offset_of!(LoadResult, error));
    assert!(offset_of!(AbiLoadResult, candidates_key) == offset_of!(LoadResult, candidates_key));
    assert!(offset_of!(AbiLoadResult, location) == offset_of!(LoadResult, location));
};

impl LoadResult {
    /// Returns a failure result: only `error` is meaningful; geometry and path are zeroed. The candidates key is
    /// left 0 for [`load_io_once`] to stamp.
    pub fn failed(error: u8) -> Self {
        Self {
            geometry: LoadGeometry::default(),
            size_bytes: 0,
            alternate_path: [0; DELUGE_PATH_MAX],
            error,
            candidates_key: 0,
            location: FileLocation::NONE,
        }
    }

    /// Returns a success result with an empty `alternate_path` (the regular path won); the caller
    /// fills it in when an alternate won. The candidates key is left 0 for [`load_io_once`] to stamp.
    pub fn succeeded(geometry: LoadGeometry, size_bytes: u64) -> Self {
        Self {
            geometry,
            size_bytes,
            alternate_path: [0; DELUGE_PATH_MAX],
            error: DELUGE_ERROR_NONE,
            candidates_key: 0,
            location: FileLocation::NONE,
        }
    }

    /// Writes `path` into `alternate_path`, NUL-terminated. Returns `false`, writing nothing, if
    /// it does not fit; a path is never truncated.
    #[must_use]
    pub fn set_alternate_path(&mut self, path: &str) -> bool {
        let bytes = path.as_bytes();
        if bytes.len() + 1 > DELUGE_PATH_MAX {
            return false;
        }
        self.alternate_path[..bytes.len()].copy_from_slice(bytes);
        self.alternate_path[bytes.len()] = 0;
        true
    }
}

// `DelugeAudioLoadResult`'s layout as numbers, for the build with no bindgen output.
// `sample_build.cpp` asserts the same numbers; `abi::pins` checks `LoadGeometry` field by field.
const _: () = {
    use core::mem::{align_of, offset_of, size_of};

    assert!(size_of::<LoadResult>() == 384);
    assert!(align_of::<LoadResult>() == 8);
    assert!(offset_of!(LoadResult, geometry) == 0);
    assert!(offset_of!(LoadResult, size_bytes) == 40);
    assert!(offset_of!(LoadResult, alternate_path) == 48);
    assert!(offset_of!(LoadResult, error) == 304);
    assert!(offset_of!(LoadResult, candidates_key) == 308);
    assert!(offset_of!(LoadResult, location) == 312);
};

/// One queued load: the nominal path (the key a result is collected by), the candidate paths to
/// try, in order, and the requester's key for that candidate list.
pub struct LoadRequest {
    pub path: LoadPath,
    pub candidates: CandidateList,
    /// Opaque to the loader: echoed into the result, and presented again by the collect that may take it.
    pub candidates_key: u32,
}

/// A finished parse waiting to be collected, tagged with the nominal path it
/// belongs to and the card generation the load that produced it began under. Its
/// `result.candidates_key` says which candidate list it answers.
pub struct PendingResult {
    key: LoadPath,
    result: LoadResult,
    /// The [card generation](crate::storage::card::generation) the load began under. Stored on
    /// the entry so the taker decides whether it is honoured; see [`take_from`].
    generation: u32,
}

/// Wakes `audio_load_task` when a load is requested. Signalling with no task waiting is a
/// harmless flag set.
pub static LOAD_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// The request queue's storage type, named so the table logic can be tested without the statics.
pub type RequestQueue = heapless::Deque<LoadRequest, LOAD_QUEUE_DEPTH>;

/// The result table's storage type. See [`RequestQueue`].
pub type ResultTable = heapless::Deque<PendingResult, RESULT_SLOTS>;

/// Requests [`deluge_audio_load_request`] pushes and `audio_load_task` drains.
static QUEUE: Mutex<CriticalSectionRawMutex, RequestQueue> = Mutex::new(heapless::Deque::new());

/// The path being parsed right now, if any, so [`deluge_audio_load_collect`] answers `PENDING`
/// (not `UNKNOWN`, which would make the caller re-request running work) between a request leaving
/// [`QUEUE`] and its result reaching [`RESULTS`].
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<LoadPath>> = Mutex::new(None);

/// Finished parses waiting to be collected, oldest at the front.
///
/// An uncollected result is evicted, oldest first, once [`RESULT_SLOTS`] later loads finish. That
/// costs one redundant read (the collector gets `UNKNOWN` and re-requests), whereas back-pressure
/// would let a browser cursor that never collects wedge the loader. A repeat request for a path
/// and candidates key already in the table replaces its entry, so a coalescing caller cannot fill
/// the table with duplicates. Answers for the same path under different keys are kept apart: two
/// live loads of one path whose candidate lists differ each wait for their own.
static RESULTS: Mutex<CriticalSectionRawMutex, ResultTable> = Mutex::new(heapless::Deque::new());

/// Forget marks held at once: more than every entry the queue, the in-flight slot and the result table can hold
/// together. A full table drops a mark whose path is no longer outstanding, which always exists while at most that
/// many paths are outstanding; a mark for an outstanding path is dropped only if the table holds nothing else
/// (possible only under contention, where every path counts as outstanding).
pub const FORGET_SLOTS: usize = 32;

const _: () = assert!(
    FORGET_SLOTS > LOAD_QUEUE_DEPTH + 1 + RESULT_SLOTS,
    "every outstanding path must be able to hold a forget mark at once"
);

/// Paths whose requester dropped its load while the loader still owed it an answer.
///
/// That answer is discarded, at publish or at the take, instead of holding a result slot nobody will collect until
/// later loads evict it. Each mark is a hash of the path, so a table of them costs no path buffers; two
/// paths that collide cost at most one redundant read, never a wrong answer. A request for a marked path clears the
/// mark: a request is made only once a collect has found nothing outstanding for it, so the mark it clears is spent.
#[derive(Default)]
pub struct ForgetMarks {
    marks: heapless::Vec<u32, FORGET_SLOTS>,
}

impl ForgetMarks {
    /// Returns an empty table.
    pub const fn new() -> Self {
        Self {
            marks: heapless::Vec::new(),
        }
    }

    /// Marks `path`. A full table drops the oldest mark `owed` reports as no longer outstanding, so a stale mark
    /// never costs a live one; if `owed` reports every mark outstanding, it drops the oldest.
    ///
    /// `owed` is given a mark's hash, since the table keeps no paths.
    pub fn mark(&mut self, path: &str, owed: impl Fn(u32) -> bool) {
        let hash = path_hash(path);
        if self.marks.contains(&hash) {
            return;
        }
        if self.marks.is_full() {
            let victim = self.marks.iter().position(|&mark| !owed(mark)).unwrap_or(0);
            self.marks.remove(victim);
        }
        let _ = self.marks.push(hash);
    }

    /// Removes `path`'s mark. Returns whether there was one.
    pub fn take(&mut self, path: &str) -> bool {
        let hash = path_hash(path);
        match self.marks.iter().position(|&mark| mark == hash) {
            Some(index) => {
                self.marks.remove(index);
                true
            }
            None => false,
        }
    }

    /// Removes every mark.
    pub fn clear(&mut self) {
        self.marks.clear();
    }

    /// How many marks are held.
    pub fn len(&self) -> usize {
        self.marks.len()
    }

    /// Whether no mark is held.
    pub fn is_empty(&self) -> bool {
        self.marks.is_empty()
    }
}

/// 32-bit FNV-1a of `path`: a forget mark's key. Public for host tests that stand in for [`ForgetMarks::mark`]'s `owed`.
#[doc(hidden)]
pub fn path_hash(path: &str) -> u32 {
    path.bytes().fold(0x811C_9DC5, |hash, byte| {
        (hash ^ u32::from(byte)).wrapping_mul(0x0100_0193)
    })
}

/// The live forget marks. A critical-section lock, never contended for longer than one table operation, so
/// [`deluge_audio_load_forget`] and [`enqueue_request`] can take it from C++ in any context without waiting, and the
/// loader can take it while it holds [`RESULTS`].
static FORGOTTEN: BlockingMutex<CriticalSectionRawMutex, RefCell<ForgetMarks>> =
    BlockingMutex::new(RefCell::new(ForgetMarks::new()));

/// Removes `path`'s forget mark. Returns whether there was one.
fn take_forget_mark(path: &str) -> bool {
    FORGOTTEN.lock(|marks| marks.borrow_mut().take(path))
}

/// How many forget marks the live table holds. For host tests, which cannot otherwise see a mark a request cleared.
#[doc(hidden)]
pub fn forget_marks_for_test() -> usize {
    FORGOTTEN.lock(|marks| marks.borrow().len())
}

/// Clears the queue, the in-flight mark and the result table.
///
/// Called by `deluge_card_flush` after it bumps the card generation. It runs synchronously from
/// C++ and must never wait, so each table is `try_lock`ed, and any clear may fail harmlessly: a
/// surviving result carries its old stamp and [`take_from`] drops it; a surviving in-flight mark
/// reports `PENDING` once more and is cleared when its load publishes; a surviving queued request
/// has done no I/O yet and is stamped with whichever card it runs against. The stamp checked at
/// take is the safety property. Don't reason from scheduling: the loader has its own executor on
/// device but a `std::thread` under `host_app`, so "these cannot run at once" is false somewhere.
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
    FORGOTTEN.lock(|marks| marks.borrow_mut().clear());
}

/// Milliseconds `audio_load_task` waits after marking each load in flight. See
/// [`deluge_audio_load_set_latency_ms`].
#[cfg(feature = "host_app")]
static LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: makes every audio load wait `ms` milliseconds after it is marked in flight,
/// so a test can hold a load while a consumer keeps collecting `PENDING`. 0 (the default) means no
/// wait.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_load_set_latency_ms(ms: u32) {
    LATENCY_MS.store(ms, core::sync::atomic::Ordering::Relaxed);
}

/// Host harness knob: how many entries the loader holds for `file_path` under any candidates key —
/// queued, in flight, published. A table locked at that moment counts as one. See `audio_load.h`.
///
/// # Safety
///
/// `file_path` must be a valid NUL-terminated C string.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_audio_load_held_for_path(file_path: *const c_char) -> u32 {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(file_path) }) else {
        return 0;
    };
    let queued = QUEUE.try_lock().map_or(1, |queue| {
        queue.iter().filter(|request| request.path == path).count()
    });
    let in_flight = IN_FLIGHT.try_lock().map_or(1, |in_flight| {
        usize::from(in_flight.as_deref() == Some(path.as_str()))
    });
    let published = RESULTS.try_lock().map_or(1, |results| {
        results.iter().filter(|entry| entry.key == path).count()
    });
    u32::try_from(queued + in_flight + published).unwrap_or(u32::MAX)
}

/// Queues `file_path` for the async loader and wakes it. See `audio_load.h`.
///
/// Returns `false` if the request is malformed or the queue is full or contended; never waits.
/// The request is built on the caller's stack (under ~2 KB on device), which is charged to
/// whatever context C++ calls this from.
///
/// # Safety
///
/// `file_path` must be a valid NUL-terminated C string, and `candidates` must
/// point to at least `num_candidates * DELUGE_PATH_MAX` readable bytes. Both are
/// copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_audio_load_request(
    file_path: *const c_char,
    candidates: *const c_char,
    num_candidates: u32,
    candidates_key: u32,
) -> bool {
    // SAFETY: the caller's contract, above.
    let accepted = unsafe { request(file_path, candidates, num_candidates, candidates_key) };
    if !accepted {
        // Its requester asks again on a later walk: that walk must not be skipped.
        bump_epoch();
    }
    accepted
}

/// Bumped whenever asking the loader again could get a different answer: a result published, or a
/// request refused (its requester asks again later). See `deluge_audio_load_epoch`.
static EPOCH: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

fn bump_epoch() {
    EPOCH.fetch_add(1, core::sync::atomic::Ordering::AcqRel);
}

/// Whether collecting or requesting `file_path` now could move its load on: a result is published
/// for it, or nothing is loading for it and the queue has room for a request. See `audio_load.h`.
/// A table locked at that moment answers `true`, which sends the caller down the full collect:
/// never a wrong "nothing to do".
///
/// # Safety
///
/// `file_path` must be a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_audio_load_can_progress(file_path: *const c_char) -> bool {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(file_path) }) else {
        return true;
    };
    let Ok(results) = RESULTS.try_lock() else {
        return true;
    };
    if results.iter().any(|entry| entry.key == path) {
        return true;
    }
    drop(results);
    let Ok(in_flight) = IN_FLIGHT.try_lock() else {
        return true;
    };
    if in_flight.as_deref() == Some(path.as_str()) {
        return false;
    }
    drop(in_flight);
    let Ok(queue) = QUEUE.try_lock() else {
        return true;
    };
    // Queued: wait for it. Not queued: a request is worth making only if it would be accepted.
    !queue.iter().any(|request| request.path == path) && !queue.is_full()
}

/// The loader's epoch; see [`EPOCH`] and `audio_load.h`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_load_epoch() -> u32 {
    EPOCH.load(core::sync::atomic::Ordering::Acquire)
}

/// [`deluge_audio_load_request`]'s body.
///
/// # Safety
///
/// As [`deluge_audio_load_request`].
unsafe fn request(
    file_path: *const c_char,
    candidates: *const c_char,
    num_candidates: u32,
    candidates_key: u32,
) -> bool {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(file_path) }) else {
        return false;
    };
    if candidates.is_null() {
        return false;
    }
    if num_candidates == DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED {
        // The buffer must not be read: a truncated slot could name a different real file.
        return false;
    }
    let slots = (num_candidates as usize).min(MAX_LOAD_CANDIDATES);
    // SAFETY: the caller promises `num_candidates * DELUGE_PATH_MAX` readable
    // bytes, and `slots` is clamped at or below `num_candidates`.
    let buffer =
        unsafe { core::slice::from_raw_parts(candidates.cast::<u8>(), slots * DELUGE_PATH_MAX) };
    let Some(candidates) = decode_candidates(buffer) else {
        return false;
    };
    if candidates.is_empty() {
        return false;
    }

    enqueue_request(LoadRequest {
        path,
        candidates,
        candidates_key,
    })
}

/// Pushes a decoded request onto [`QUEUE`] and wakes the loader. `try_lock`s: it is called
/// synchronously from C++ in any context and must never wait.
fn enqueue_request(request: LoadRequest) -> bool {
    // Nothing is outstanding for this path, or the collect before this request would not have answered UNKNOWN: any
    // mark it has is spent, and must not discard the answer to this request.
    let _ = take_forget_mark(&request.path);
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

/// Collects the finished parse for `file_path` made with `candidates_key`'s candidates, if one is
/// ready. See `audio_load.h`.
///
/// Any outstanding request for the path answers `PENDING`, whatever its key: a collector waits for
/// the loader to finish with the path before it asks with its own candidates.
///
/// # Probe order
///
/// An entry only moves forward, `QUEUE` → `IN_FLIGHT` → `RESULTS`, and the probe reads in
/// that order. Three rules make a forward pass unable to miss it:
///
/// 1. The task sets `IN_FLIGHT` while still holding the `QUEUE` lock.
/// 2. It publishes to `RESULTS` before clearing `IN_FLIGHT` (a result may read `PENDING` for one
///    extra tick).
/// 3. `is_queued` and `is_in_flight` report `true` when their lock is contended. During the
///    QUEUE → IN_FLIGHT handoff the entry is in neither table; only the held lock covers it.
///
/// Reading in reverse order can report `UNKNOWN` for a load that just succeeded (miss `RESULTS`,
/// task publishes and clears `IN_FLIGHT`, miss `IN_FLIGHT`), so the caller re-requests finished
/// work and orphans its result.
///
/// # Safety
///
/// `file_path` must be a valid NUL-terminated C string and `out` a valid, writable
/// [`LoadResult`]. `out` is written only on [`DELUGE_CHANNEL_READY`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_audio_load_collect(
    file_path: *const c_char,
    candidates_key: u32,
    out: *mut AbiLoadResult,
) -> DelugeChannelStatus {
    if out.is_null() {
        return DELUGE_CHANNEL_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(file_path) }) else {
        return DELUGE_CHANNEL_UNKNOWN;
    };

    // SAFETY: `out` is non-null and, by the caller's contract, an aligned, writable
    // `LoadResult`. It is viewed as uninitialised because C hands over a slot, not a value, and
    // nothing here reads it.
    let out = unsafe { &mut *out.cast::<MaybeUninit<LoadResult>>() };

    match probe(&path, candidates_key, out) {
        Probe::Ready => DELUGE_CHANNEL_READY,
        Probe::Pending => DELUGE_CHANNEL_PENDING,
        Probe::Unknown => DELUGE_CHANNEL_UNKNOWN,
    }
}

/// Discards the answer the loader still owes for `file_path`, if it owes one. See `audio_load.h`.
///
/// Marks the path only while something for it is queued, in flight or published (a contended table counts as
/// outstanding, as in [`probe`]), so a path with nothing outstanding leaves no mark. Never waits.
///
/// # Safety
///
/// `file_path` must be a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_audio_load_forget(file_path: *const c_char) {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(file_path) }) else {
        return;
    };
    if is_outstanding(&path) {
        FORGOTTEN.lock(|marks| marks.borrow_mut().mark(&path, hash_is_outstanding));
    }
}

/// Whether the loader still owes `path` an answer: queued, in flight or published. Read in [`probe`]'s order, and
/// `true` on contention, for the same reasons.
fn is_outstanding(path: &str) -> bool {
    if is_queued(path) || is_in_flight(path) {
        return true;
    }
    match RESULTS.try_lock() {
        Ok(results) => results.iter().any(|entry| entry.key == path),
        Err(_) => true,
    }
}

/// Whether any path whose [`path_hash`] is `hash` is queued, in flight or published. Contention reports `true`, as
/// in [`is_outstanding`]. Never waits.
fn hash_is_outstanding(hash: u32) -> bool {
    let queued = match QUEUE.try_lock() {
        Ok(queue) => queue.iter().any(|request| path_hash(&request.path) == hash),
        Err(_) => return true,
    };
    if queued {
        return true;
    }
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => {
            if in_flight
                .as_deref()
                .is_some_and(|path| path_hash(path) == hash)
            {
                return true;
            }
        }
        Err(_) => return true,
    }
    match RESULTS.try_lock() {
        Ok(results) => results.iter().any(|entry| path_hash(&entry.key) == hash),
        Err(_) => true,
    }
}

/// What one forward pass over the pipeline found.
///
/// Produced by a single pass: separate "ready?" and "still coming?" passes would let the loader
/// publish between them and report `Unknown` for a result. A status only — [`probe`] writes a
/// found result straight into the caller's `out` slot. (Boxing a payload instead would allocate on
/// the per-tick collect path.)
enum Probe {
    /// `out` has been written. Only this case writes it: `audio_load.h` lets a caller pass an
    /// uninitialised slot and read it only on `READY`.
    Ready,
    Pending,
    Unknown,
}

/// One forward pass: QUEUE, then IN_FLIGHT, then RESULTS. See [`deluge_audio_load_collect`].
fn probe(path: &str, candidates_key: u32, out: &mut MaybeUninit<LoadResult>) -> Probe {
    if is_queued(path) || is_in_flight(path) {
        return Probe::Pending;
    }
    // A contended table means the loader is mid-publish: `Unknown` would make the caller
    // re-request work that is about to land.
    let Ok(mut results) = RESULTS.try_lock() else {
        return Probe::Pending;
    };
    // The generation is read at the take: the collector's own view of which card is in the slot.
    // See [`take_from`].
    match take_from(
        &mut results,
        path,
        candidates_key,
        crate::storage::card::generation(),
    ) {
        // Published before its requester dropped it: owed to no one.
        Some(_) if take_forget_mark(path) => Probe::Unknown,
        Some(result) => {
            out.write(result);
            Probe::Ready
        }
        None => Probe::Unknown,
    }
}

/// Whether `path` is still waiting in [`QUEUE`].
///
/// Contention reports `true`, deliberately: the loader marks a request in flight while holding
/// this lock, so `false` here would report `UNKNOWN` for a request mid-handoff.
fn is_queued(path: &str) -> bool {
    match QUEUE.try_lock() {
        Ok(queue) => queue.iter().any(|request| request.path == path),
        Err(_) => true,
    }
}

/// Whether `path` is the load being parsed right now. Contention reports `true`, as in
/// [`is_queued`].
fn is_in_flight(path: &str) -> bool {
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => in_flight.as_deref() == Some(path),
        Err(_) => true,
    }
}

/// Records (or clears) the load being parsed right now. Clearing it wakes the app
/// (`DELUGE_WAKE_STORAGE`): the load's result is visible once this returns.
///
/// The task calls this while holding the `QUEUE` lock (see [`deluge_audio_load_collect`]).
/// Host tests call it directly to reach the collect's IN_FLIGHT branch without running the task.
pub async fn mark_in_flight(path: Option<LoadPath>) {
    let done = path.is_none();
    *IN_FLIGHT.lock().await = path;
    if done {
        crate::app_loop::wake(crate::app_loop::WAKE_STORAGE);
    }
}

/// Removes and returns the entry at `index`, preserving the order of the rest.
///
/// `heapless::Deque` has no positional removal, so this rotates the entries before `index` to the
/// back, pops the target, then rotates `len - index` more steps (not `index`) to restore the
/// original order, which oldest-first eviction depends on.
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

/// Publishes `result` under `key` into `table`, stamped with `generation`, replacing any entry for
/// that key and the result's candidates key, and evicting the oldest if the table is full. See
/// `RESULTS`.
///
/// `generation` must be the one the load began under, which only the caller knows — never the
/// live counter. The table is a parameter so tests can drive eviction and replacement directly.
pub fn publish_into(table: &mut ResultTable, key: LoadPath, result: LoadResult, generation: u32) {
    if let Some(index) = table
        .iter()
        .position(|entry| entry.key == key && entry.result.candidates_key == result.candidates_key)
    {
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

/// Takes `path`'s result for `candidates_key` out of `table`, if it holds one stamped `generation`.
///
/// An answer for the same path under another key is left where it is: it was found with a
/// different candidate list, so it is not this collector's answer, and its own collector still
/// wants it. The entry taken is removed, so a second collect reports `UNKNOWN` and a `Sample`
/// cannot be built twice. An entry stamped with another generation was read from a card that has gone: it is
/// removed and `None` returned (the collector re-requests). This check is the card-swap safety
/// property; `flush_tables`' clears can fail and [`publish_result`]'s check is only an early
/// drop. Other stale entries are not swept: they are uncollectable and cost at most a redundant
/// read.
pub fn take_from(
    table: &mut ResultTable,
    path: &str,
    candidates_key: u32,
    generation: u32,
) -> Option<LoadResult> {
    let index = table
        .iter()
        .position(|entry| entry.key == path && entry.result.candidates_key == candidates_key)?;
    let entry = remove_at(table, index);
    (entry.generation == generation).then_some(entry.result)
}

/// Publishes into the live `RESULTS` table, stamped with `generation`, unless that card has
/// already gone.
///
/// `generation` is the value sampled when the load began (`take_next_request`); sampling it here
/// would defeat the stamp. The check below is only an early drop that saves a slot: with no
/// happens-before edge from [`deluge_card_flush`](crate::storage::card::deluge_card_flush)'s bump,
/// it can read the pre-flush value and insert, and [`take_from`] then drops the entry. It runs
/// under the table lock so it is at least ordered against a flush that took that lock.
pub async fn publish_result(key: LoadPath, result: LoadResult, generation: u32) {
    publish_or_discard(key, result, generation).await;
    // Whatever became of it, the load is over: a requester waiting on it may now get a different
    // answer, even if only `UNKNOWN` and a re-request.
    bump_epoch();
}

async fn publish_or_discard(key: LoadPath, result: LoadResult, generation: u32) {
    let mut table = RESULTS.lock().await;
    if generation != crate::storage::card::generation() {
        // Read from a card that has gone; the collector gets `UNKNOWN` and re-requests.
        return;
    }
    if take_forget_mark(&key) {
        // Its requester dropped the load; see [`ForgetMarks`].
        return;
    }
    publish_into(&mut table, key, result, generation);
}

/// Decodes the candidate buffer the C side filled: one NUL-terminated path per
/// [`DELUGE_PATH_MAX`] slot.
///
/// Returns `None` if any slot is unterminated, not UTF-8 or too long, or there are more slots than
/// `MAX_LOAD_CANDIDATES`.
pub fn decode_candidates(buffer: &[u8]) -> Option<CandidateList> {
    let slots = buffer.len() / DELUGE_PATH_MAX;
    if slots > MAX_LOAD_CANDIDATES {
        return None;
    }
    let mut out = CandidateList::new();
    for slot in 0..slots {
        let bytes = &buffer[slot * DELUGE_PATH_MAX..(slot + 1) * DELUGE_PATH_MAX];
        // Anything after the NUL is the caller's untouched buffer, never read.
        let end = bytes.iter().position(|b| *b == 0)?;
        let path = LoadPath::try_from(core::str::from_utf8(&bytes[..end]).ok()?).ok()?;
        out.push(path).ok()?;
    }
    Some(out)
}

/// Copies a C string into a [`LoadPath`].
///
/// Returns `None` for NULL, non-UTF-8, or a path longer than `MAX_PATH_CHARS`. A path that does
/// not fit is refused, not truncated: a truncated path names a different file. Public for the host
/// test that checks it against the C++ side's `path_admission_corpus.inc`.
///
/// # Safety
///
/// `ptr` must be NULL or a valid NUL-terminated C string.
pub unsafe fn cstr_to_path(ptr: *const c_char) -> Option<LoadPath> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: the caller's contract, above.
    let cstr = unsafe { CStr::from_ptr(ptr) };
    LoadPath::try_from(cstr.to_str().ok()?).ok()
}

// The parse driver: compiles with no efatfs backing, so host tests can drive it.
mod driver {
    use super::{
        DELUGE_ERROR_FILE_CORRUPTED, DELUGE_ERROR_FILE_UNREADABLE, DELUGE_ERROR_FILE_UNSUPPORTED,
        DELUGE_ERROR_NONE, DELUGE_ERROR_TOO_MANY_OPEN_STREAMS, FileLocation, LoadGeometry,
        LoadRequest, LoadResult,
    };
    use deluge_audio_format::{
        AudioHeader, FeedResult, HeaderError, HeaderParser, MAX_WINDOW_BYTES,
    };

    impl From<&AudioHeader> for LoadGeometry {
        /// Copies exactly the fields the synchronous `AudioFile::loadFile` writes onto a `Sample`;
        /// a field missed here loads without error and renders wrong.
        fn from(header: &AudioHeader) -> Self {
            Self {
                sample_rate: header.sample_rate,
                audio_data_start_pos_bytes: header.audio_data_start_pos_bytes,
                audio_data_length_bytes: header.audio_data_length_bytes,
                file_loop_start_samples: header.file_loop_start_samples,
                file_loop_end_samples: header.file_loop_end_samples,
                wavetable_cycle_size: header.wavetable.cycle_size,
                // Read only when `midi_note_present` is set; the C++ side maps "absent" to
                // `Sample::midiNoteFromFile`'s -1.0f sentinel.
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

    /// Maps a parser failure onto a Deluge `Error` code.
    ///
    /// Must match the synchronous path's `errorFromParseStatus` (`audio_file_format.cpp`):
    /// `sample_browser.cpp` branches on these identities.
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

    /// The outcome of opening one candidate.
    ///
    /// "Not here" is a candidate verdict (try the next; if none opens, the collector probes the
    /// alternate directory). "Table full" is a resource verdict that says nothing about where the
    /// file lives; treated as a miss it could load a same-named file from the alternate directory.
    /// The two must stay distinct all the way to the collector.
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum OpenVerdict {
        /// Opened; the caller must close this efatfs handle.
        Opened(u32),
        /// Nothing to read at this path: missing, unmounted, or any other open failure.
        Missing,
        /// The efatfs handle table has no free slot; every other candidate would hit it too.
        TableFull,
    }

    /// The filesystem operations [`load_io_once`] needs.
    ///
    /// All `async`, none reaching C++: a synchronous C++ call here would deadlock at interrupt
    /// priority (see the module doc).
    pub trait LoadOps {
        /// Opens `path` for reading.
        async fn open(&self, path: &str) -> OpenVerdict;

        /// Returns the size in bytes of the file behind `handle`, from the same handle table
        /// [`LoadOps::open`] filled.
        async fn size_of(&self, handle: u32) -> Option<u32>;

        /// Fills `buf` from absolute `offset` of the file behind `handle`; `false` if the whole
        /// buffer could not be read.
        ///
        /// `HeaderParser` clamps every request to the size [`LoadOps::size_of`] reported, so a
        /// `false` is always a real read failure, never end of file.
        async fn read_at(&self, handle: u32, offset: u32, buf: &mut [u8]) -> bool;

        /// Releases the efatfs handle. The table has `MAX_HANDLES` slots, and a leak is silent capacity
        /// loss.
        async fn close(&self, handle: u32);

        /// Where the file behind `handle` is, for the collector to open without a directory walk;
        /// `None` if it cannot say, and the collector then opens by path.
        async fn location(&self, handle: u32) -> Option<FileLocation>;
    }

    /// Runs one queued load's I/O: tries each candidate in order until one both opens and sizes,
    /// then walks its header to a verdict.
    ///
    /// Always returns a result for the C++ side to collect, and closes every handle it opens.
    ///
    /// The regular path is identified by position (it is always the last candidate), never by
    /// string equality: when the alternate directory is the file's own directory, the alternate
    /// candidate equals the nominal path, and treating that hit as regular would leave
    /// `Sample::loadedFromAlternatePath` empty and skip the collect-media copy on save.
    ///
    /// Every result carries the request's candidates key, so only a collect for the same candidate
    /// list takes it.
    pub async fn load_io_once<O: LoadOps>(ops: &O, request: &LoadRequest) -> LoadResult {
        let mut result = try_candidates(ops, request).await;
        result.candidates_key = request.candidates_key;
        result
    }

    /// [`load_io_once`]'s walk, before the result is stamped with the request's key.
    async fn try_candidates<O: LoadOps>(ops: &O, request: &LoadRequest) -> LoadResult {
        // A candidate is taken only once it has both opened and sized, like the synchronous
        // `tryOpenCandidate`; an openable but unsizeable file is a miss, so it cannot shadow a
        // good later candidate.
        let mut taken: Option<(u32, &str, u32, usize)> = None;
        for (index, candidate) in request.candidates.iter().enumerate() {
            let handle = match ops.open(candidate).await {
                OpenVerdict::Opened(handle) => handle,
                OpenVerdict::Missing => continue,
                // Every remaining candidate would hit the same full table. Report the resource
                // failure so the collector reclaims rather than probing the alternate directory.
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
        if result.error == DELUGE_ERROR_NONE {
            result.location = ops.location(handle).await.unwrap_or(FileLocation::NONE);
        }

        ops.close(handle).await;

        // Positional; see the doc comment.
        let is_regular_path = index + 1 == request.candidates.len();
        // `set_alternate_path` is a write: `&&` runs it only on success from an alternate, so
        // don't reorder these terms.
        if result.error == DELUGE_ERROR_NONE
            && !is_regular_path
            && !result.set_alternate_path(winning_path)
        {
            // A path we cannot report would be opened at the wrong location.
            return LoadResult::failed(DELUGE_ERROR_FILE_UNREADABLE);
        }
        result
    }

    /// Drives the header windows of an open, sized file to a verdict. Never closes `handle`.
    async fn parse_open_file<O: LoadOps>(ops: &O, handle: u32, size_bytes: u32) -> LoadResult {
        let mut parser = HeaderParser::new(size_bytes);
        let mut window = [0u8; MAX_WINDOW_BYTES as usize];
        while let Some((start, len)) = parser.pending() {
            let len = len as usize;
            if len > window.len() {
                // Unreachable: the parser never asks for more than `MAX_WINDOW_BYTES`. Refuse
                // rather than panic, which would take the executor down.
                return LoadResult::failed(DELUGE_ERROR_FILE_CORRUPTED);
            }
            // A zero-length window (a walk at end of file) must still be read and fed: the parser
            // resolves end of file inside `feed`, so skipping it would loop forever.
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

        // Unreachable for a fresh parser (every terminal phase is reported through `feed`), but
        // a walk that ends with no header has no geometry to publish.
        LoadResult::failed(DELUGE_ERROR_FILE_CORRUPTED)
    }
}

// Public for `tests/audio_loader_host.rs`.
#[allow(unused_imports)]
pub use driver::{LoadOps, OpenVerdict, error_code_for, load_io_once};

// Production wiring: only where the efatfs backing is linked (device, and host under `host_app`).
#[cfg(has_app)]
mod prod {
    use super::FileLocation;
    use super::driver::LoadOps;

    use crate::storage::efatfs::backend as backing;

    /// The production [`LoadOps`], over the efatfs streaming handle table. Stateless: every
    /// operation is keyed by handle.
    pub struct ProdOps;

    impl ProdOps {
        pub fn new() -> Self {
            Self
        }
    }

    impl LoadOps for ProdOps {
        async fn open(&self, path: &str) -> super::driver::OpenVerdict {
            use super::driver::OpenVerdict;
            use crate::storage::fill_trace::{LoaderWork, loader};
            let start = embassy_time::Instant::now();
            crate::storage::fill_trace::open_span_begin();
            let outcome = backing::open(path).await;
            let card = crate::storage::fill_trace::open_span_end();
            let work = match outcome {
                crate::storage::efatfs::portable::OpenOutcome::Handle(_) => LoaderWork::OpenHit,
                _ => LoaderWork::OpenMiss,
            };
            let ms = embassy_time::Instant::now()
                .saturating_duration_since(start)
                .as_millis();
            if ms >= 20 {
                log::info!(
                    "LOADER-OPEN ms={} card_reads={} sectors={} card_ms={} hit={} path={}",
                    ms,
                    card.reads,
                    card.sectors,
                    card.us / 1000,
                    matches!(work, LoaderWork::OpenHit),
                    path
                );
            }
            loader(work, start);
            match outcome {
                crate::storage::efatfs::portable::OpenOutcome::Handle(handle) => {
                    OpenVerdict::Opened(handle)
                }
                // Every open failure counts as missing, the same conflation the synchronous
                // `tryOpenCandidate` makes (see the TODO at its unknown-as-absent branch).
                crate::storage::efatfs::portable::OpenOutcome::Failed(_) => OpenVerdict::Missing,
                // The streaming handle table shared with `open_read_stream` fills up under browser
                // preview, this loader's own workload. Kept distinct so the collector reclaims.
                crate::storage::efatfs::portable::OpenOutcome::TableFull => OpenVerdict::TableFull,
            }
        }

        async fn size_of(&self, handle: u32) -> Option<u32> {
            let start = embassy_time::Instant::now();
            let size = backing::size_of_handle(handle).await;
            crate::storage::fill_trace::loader(
                crate::storage::fill_trace::LoaderWork::Other,
                start,
            );
            size
        }

        async fn read_at(&self, handle: u32, offset: u32, buf: &mut [u8]) -> bool {
            let start = embassy_time::Instant::now();
            let ok = backing::read_at(handle, offset, buf).await;
            crate::storage::fill_trace::loader(crate::storage::fill_trace::LoaderWork::Read, start);
            ok
        }

        async fn close(&self, handle: u32) {
            let start = embassy_time::Instant::now();
            backing::close(handle).await;
            crate::storage::fill_trace::loader(
                crate::storage::fill_trace::LoaderWork::Other,
                start,
            );
        }

        async fn location(&self, handle: u32) -> Option<FileLocation> {
            let start = embassy_time::Instant::now();
            let location = backing::location_of(handle).await;
            crate::storage::fill_trace::loader(
                crate::storage::fill_trace::LoaderWork::Other,
                start,
            );
            location
        }
    }
}

#[cfg(has_app)]
#[allow(unused_imports)]
pub use prod::ProdOps;

/// The load task: wakes on [`LOAD_WAKE`], drains every queued request through [`ProdOps`], and
/// publishes each result, on the storage tier.
///
/// `IN_FLIGHT` is set before the I/O and cleared only after the result is published, so
/// [`deluge_audio_load_collect`] never sees a running load as unknown.
#[cfg(has_app)]
#[embassy_executor::task]
pub async fn audio_load_task() {
    crate::tier::scoped(crate::tier::Tier::Storage, audio_load_task_body()).await
}

#[cfg(has_app)]
async fn audio_load_task_body() {
    LOAD_WAKE.wait().await;
    let ops = ProdOps::new();
    loop {
        while let Some((request, generation)) = take_next_request().await {
            // After the in-flight mark, so a collect during the hold answers PENDING, as it would
            // for a slow card.
            #[cfg(feature = "host_app")]
            {
                let ms = LATENCY_MS.load(core::sync::atomic::Ordering::Relaxed);
                if ms > 0 {
                    embassy_time::Timer::after_millis(u64::from(ms)).await;
                }
            }
            let key = request.path.clone();
            let start = embassy_time::Instant::now();
            let result = load_io_once(&ops, &request).await;
            crate::storage::fill_trace::loader(
                crate::storage::fill_trace::LoaderWork::Request,
                start,
            );
            // Publish before clearing IN_FLIGHT: the reverse leaves a window in which `collect`
            // reports `UNKNOWN` for a load that just succeeded. `generation` was sampled before
            // the I/O, so a card pulled during it makes this result uncollectable.
            publish_result(key, result, generation).await;
            mark_in_flight(None).await;
        }
        LOAD_WAKE.wait().await;
    }
}

/// Pops the next queued request, marks it in flight, and returns it with the card generation the
/// load begins under.
///
/// The mark is made while the [`QUEUE`] lock is held, so a racing collect sees the request queued
/// or finds the lock contended — `PENDING` either way, never `UNKNOWN`. The generation is read
/// here, before any I/O, so it names the card this load reads.
#[cfg(has_app)]
async fn take_next_request() -> Option<(LoadRequest, u32)> {
    let mut queue = QUEUE.lock().await;
    let request = queue.pop_front()?;
    let generation = crate::storage::card::generation();
    mark_in_flight(Some(request.path.clone())).await;
    Some((request, generation))
}

abi_pin!(
    deluge_audio_load_collect,
    deluge_audio_load_epoch,
    deluge_audio_load_forget,
    deluge_audio_load_can_progress,
    deluge_audio_load_request
);
#[cfg(feature = "host_app")]
abi_pin!(
    deluge_audio_load_set_latency_ms,
    deluge_audio_load_held_for_path
);
