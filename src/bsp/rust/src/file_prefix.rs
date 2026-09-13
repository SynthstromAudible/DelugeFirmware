//! The single-slot prefix channel: read the first N bytes of a file on the storage tier and hand
//! them back to a caller that is not allowed to wait for them.
//!
//! `include/libdeluge/file_prefix.h` is the C side. The shape is the request/collect split
//! `audio_loader.rs` already uses — ask, return to your own loop, collect on a later tick — and it
//! exists for the same reason: the caller (a browser drawing a song's artwork) runs on the
//! interaction tier, where blocking on the card is what this whole boundary exists to stop.
//!
//! ## Why this is a SLOT and not a table
//!
//! The audio loader keeps a queue and an eight-slot result table because a song load fans out into
//! many concurrent sample loads. Song preview does not fan out at all: there is one browser, one
//! cursor, and one coalescer behind it, so at most one prefix is ever wanted at a time. A single
//! slot is therefore the honest structure — no depth, no eviction policy, no ordering — and a newer
//! request simply replaces an older one, which is exactly what a cursor moving down a folder means.
//!
//! ## Everything here awaits, and nothing calls into C++
//!
//! The driver runs on the same executor as `audio_load_task` and inherits that module's single
//! rule verbatim: it holds the `efatfs` FS mutex across an SD await, so it can neither run on the
//! thread-mode executor (whose `sim_block::block_on` spin would starve the task holding the mutex)
//! nor make a synchronous C++ call from its own interrupt executor (which would deadlock the other
//! way round, and could preempt thread-mode mid-mutation). See `audio_loader.rs`'s module doc for
//! the full argument.
//!
//! That is why the filesystem operations arrive as [`super::audio_loader::LoadOps`] rather than as
//! a second trait declared here: one trait means one place where "all async, no route into C++"
//! has to hold, and `ProdOps` — the efatfs wiring behind it — is already written and already
//! type-checked against both the device mount and the host shim.
//!
//! ## A prefix is keyed by path, and a path outlives the card it was read from
//!
//! Identical hazard to the loader's, and closed the same way. The slot is keyed by the nominal
//! path, so a prefix read from card A would be served for card B's file at the same path after a
//! swap — artwork from the wrong song, drawn with no error. Each read is STAMPED with the card
//! generation it began under ([`deluge_card_generation`], shared with every other loader), and
//! [`take_from`] serves an entry only when its stamp equals the generation the taker is asking
//! under, dropping it otherwise.
//!
//! The check lives at the TAKE, not at the flush: `deluge_card_flush`'s bump is unconditional but
//! its table clears are `try_lock`ed, so an entry can outlive a flush, and a read already in flight
//! publishes afterwards. Deciding from the taker's own current generation is what makes the outcome
//! independent of which locks the flush managed to take. None of that may be re-argued from where
//! either side is scheduled: the driver owns its own executor on device but is a plain
//! `std::thread` under `host_app`, so any "these two cannot run at once" claim is false on some
//! configuration that builds this file.
//!
//! ## What compiles where
//!
//! The channel has no feature switch.
//!
//! - Everywhere this module is: the slot logic ([`PrefixSlot`], [`publish_into`], [`take_from`]),
//!   the driver ([`prefix_io_once`]), the statics, and the C ABI ([`deluge_file_prefix_request`],
//!   [`deluge_file_prefix_collect`], [`deluge_file_prefix_async_active`]), which overrides
//!   `file_prefix_abi.cpp`'s `__attribute__((weak))` fallbacks. Every other BSP never links this
//!   crate, resolves those fallbacks, and reads files itself.
//! - Where the storage executor runs (the device under `efatfs_streaming`, the host under
//!   `host_app`): the task and its work buffer.
//!
//! The slots hold metadata only and live in on-chip SRAM. The two [`PREFIX_CAPACITY`] byte buffers
//! -- the published prefix ([`RESULT_BYTES`]) and the read in progress ([`WORK_BYTES`]) -- are in
//! `.sdram_bss` on the device: on-chip SRAM shares a budget with the C++ allocator's
//! `FAST_INTERNAL` heap and grows in 64 KiB steps of it, so 8 KiB buffers do not belong there.

use core::ffi::{CStr, c_char};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::{Mutex, MutexGuard};
use embassy_sync::signal::Signal;

use super::audio_loader::{LoadOps, OpenVerdict};

/// How many bytes one prefix request may ask for. Sized from the channel's LARGEST consumer, which
/// is the DX7 cartridge read (`min(filesize, 8192)`, `gui/menu_item/dx/cartridge.cpp`), not from the
/// song preview.
///
/// It was equal to `deluge::storage::kSongPreviewWindow` while the preview was the only consumer,
/// and that coincidence was pinned by an equality `static_assert`. The cartridge broke it: the two
/// numbers answer different questions -- "how much of a song holds the artwork" (4096) versus "how
/// big is the slot every consumer's result has to land in" (8192). `file_prefix_abi.cpp` now
/// asserts the surviving relation, that a request never exceeds the slot.
///
/// It bounds the request as well as the slot: [`deluge_file_prefix_request`] clamps `max_bytes` to
/// this, so the driver can never be asked for a read that would not fit in the slot the result has
/// to land in.
pub const PREFIX_CAPACITY: usize = 8192;

/// Longest path this channel keys on, in CHARACTERS — `DELUGE_MAX_PATH` minus its NUL, the same
/// bound `audio_loader::LoadPath` carries and for the same reason (a C buffer size counts the
/// terminator; the text it can hold is one byte shorter).
///
/// Spelled here rather than imported from the loader so this module compiles in every carrier that
/// `#[path]`-includes it, including ones where the loader's own tier is switched off. The two are
/// pinned to the same literal by the assertion below and by `file_prefix_abi.cpp`'s
/// `static_assert`s, so they cannot drift apart silently.
const MAX_PATH_CHARS: usize = 255;

/// One path, inline. **Nothing in this module may touch the Rust global heap** — that allocator
/// assumes single-threaded use and this driver runs on a preempting executor. See
/// `audio_loader::LoadPath`, which says it at length.
pub type PrefixPath = heapless::String<MAX_PATH_CHARS>;

/// `deluge_file_prefix_collect`: requested or in flight; ask again, do not re-request.
pub const DELUGE_FILE_PREFIX_PENDING: u8 = 0;
/// `deluge_file_prefix_collect`: a prefix was written out and removed.
pub const DELUGE_FILE_PREFIX_READY: u8 = 1;
/// `deluge_file_prefix_collect`: nothing known about this path. Request it (again).
pub const DELUGE_FILE_PREFIX_UNKNOWN: u8 = 2;
/// `deluge_file_prefix_collect`: the read did not happen. Terminal — do NOT re-request.
///
/// The one bit this channel carries that a caller cannot reconstruct. Everything else about a
/// failure (which failure, on which layer) stays outside the boundary; see `file_prefix.h`.
pub const DELUGE_FILE_PREFIX_FAILED: u8 = 3;

/// The values this module hand-duplicates from `file_prefix.h`, pinned.
///
/// Asserting a constant against its own literal looks tautological, and that is the point: it makes
/// a change take two deliberate edits instead of one, and the mirror of these numbers lives in
/// `file_prefix_abi.cpp`'s `static_assert`s.
///
/// **The status numbering is the audio loader's, value for value** (`DELUGE_AUDIO_LOAD_PENDING`
/// = 0, `_READY` = 1, `_UNKNOWN` = 2), and that is a safety property rather than tidiness. Both
/// headers will plausibly be included in one translation unit — browsing a song wants samples AND
/// artwork — where `deluge_file_prefix_collect(..) == DELUGE_AUDIO_LOAD_READY` compiles clean and
/// nothing cross-checks it (`build.rs`'s bindgen allowlist covers `Deluge.*` TYPES, not macros).
/// Under a divergent numbering that expression would mean PENDING, and the caller would decode
/// bytes the header promises are untouched. Keep them equal.
const _: () = {
    assert!(PREFIX_CAPACITY == 8192); // DELUGE_FILE_PREFIX_CAPACITY; sized for cartridge.cpp
    assert!(MAX_PATH_CHARS == 255); // DELUGE_MAX_PATH - 1, as audio_loader::LoadPath
    assert!(DELUGE_FILE_PREFIX_PENDING == 0);
    assert!(DELUGE_FILE_PREFIX_READY == 1);
    assert!(DELUGE_FILE_PREFIX_UNKNOWN == 2);
    // Outside the loader's set of three, on purpose -- see `file_prefix.h`'s macro doc.
    assert!(DELUGE_FILE_PREFIX_FAILED == 3);
};

/// One finished prefix read, stamped with the card generation the read BEGAN under.
struct PrefixEntry {
    key: PrefixPath,
    generation: u32,
    /// How many bytes of the store are the prefix, or `None` when the READ FAILED.
    ///
    /// A failure is an entry rather than an absence for the same reason a successful read is: the
    /// slot's only two answers to a taker are "here it is" and "never heard of it", and the second
    /// means "request it (again)". A failed read that published nothing would be re-requested and
    /// re-read on every tick. It is terminal, and it is DISTINCT from `Some(0)` — an empty file —
    /// because the caller shows the user an error for one and blank artwork for the other.
    len: Option<usize>,
}

/// What [`take_from`] found in the slot for the path it was asked about.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum PrefixTake {
    /// A prefix, this many bytes of it, copied into the taker's buffer. May legitimately be 0 — an
    /// empty FILE.
    Bytes(usize),
    /// The read did not happen: the file would not open, could not be sized, or the read failed.
    /// Nothing was written to the taker's buffer.
    Failed,
}

/// The one prefix this channel holds, if it holds one.
///
/// Passed to [`publish_into`]/[`take_from`] by reference rather than reached for as a static, so
/// the slot logic is compiled and unit-tested on every build — including the ones where [`RESULT`]
/// does not exist at all.
///
/// It holds the entry and nothing else. The bytes live in a store passed beside it -- on the device
/// [`RESULT_BYTES`], in SDRAM -- because an 8 KiB array inside the `Mutex` would sit in on-chip
/// SRAM, and an empty `Option` is not all-zero bytes, so the slot itself cannot go to `.sdram_bss`.
#[derive(Default)]
pub struct PrefixSlot {
    entry: Option<PrefixEntry>,
}

impl PrefixSlot {
    /// An empty slot, in a `const` context — what [`RESULT`] is initialised with. [`Default`] does
    /// the same thing everywhere else; this exists only because `Default::default` is not `const`.
    pub const fn new() -> Self {
        Self { entry: None }
    }

    /// Whether the slot holds nothing. The belt tests assert this after a stale take: a stamp that
    /// was refused must also have been REMOVED.
    pub fn is_empty(&self) -> bool {
        self.entry.is_none()
    }
}

/// Publish `bytes` under `key`, stamped with `generation`, replacing whatever the slot held.
///
/// A longer-than-capacity read is a driver bug, not a runtime condition: [`prefix_io_once`] is
/// bounded by the `max_bytes` [`deluge_file_prefix_request`] already clamped to [`PREFIX_CAPACITY`].
/// Truncating silently here would hand the decoder a short buffer that looks exactly like a
/// genuinely short file — and "the preview attribute is not in this window" is precisely how a
/// decoded prefix reports blank artwork, so the truncation would present as a song losing its
/// artwork with nothing to say why.
///
/// A key too long to store is refused the same way — see [`new_entry`].
///
/// The bytes go into `store`, which [`take_from`] copies them back out of. Both halves are written in
/// the one call, so a take never pairs this entry with another publish's bytes.
pub fn publish_into(
    slot: &mut PrefixSlot,
    store: &mut [u8; PREFIX_CAPACITY],
    key: &str,
    bytes: &[u8],
    generation: u32,
) {
    debug_assert!(bytes.len() <= PREFIX_CAPACITY);
    let len = bytes.len().min(PREFIX_CAPACITY);
    if new_entry(slot, key, generation, Some(len)) {
        store[..len].copy_from_slice(&bytes[..len]);
    }
}

/// Publish "the read for `key` did not happen", stamped with `generation`, replacing whatever the
/// slot held.
///
/// A separate entry point rather than a `bytes: Option<&[u8]>` parameter on [`publish_into`],
/// because the two are not variants of one operation at the call site: the driver either has bytes
/// or it has a failure, and a `None` threaded through the successful path is how "publish nothing"
/// and "publish a failure" get confused.
///
/// Refuses an over-long key and clears the slot, exactly as [`publish_into`] does and for the same
/// reason.
pub fn publish_failure_into(slot: &mut PrefixSlot, key: &str, generation: u32) {
    let _ = new_entry(slot, key, generation, None);
}

/// Install a fresh entry under `key` and return `true`, or clear the slot and return `false` when the
/// key does not fit.
///
/// A key too long to store is refused, and the slot is CLEARED rather than left holding its previous
/// entry: unreachable through the FFI (which refuses an over-long path before it ever reaches the
/// driver), but a prefix nobody can key is a prefix nobody can ever take, and leaving a stale
/// neighbour resident in its place is strictly worse than an empty slot.
fn new_entry(slot: &mut PrefixSlot, key: &str, generation: u32, len: Option<usize>) -> bool {
    let Ok(key) = PrefixPath::try_from(key) else {
        slot.entry = None;
        return false;
    };
    slot.entry = Some(PrefixEntry {
        key,
        generation,
        len,
    });
    true
}

/// Take `path`'s entry if the slot holds one stamped `generation` AND `out` is big enough, copying its
/// bytes out of `store` -- the store [`publish_into`] wrote them to.
///
/// Removing on success is the point: a second collect must report `UNKNOWN`, or a caller could act
/// on the same bytes twice. A published FAILURE ([`PrefixTake::Failed`]) is removed on exactly the
/// same terms — it is an answer, not a state, and a caller that read it once has been told.
///
/// # The generation check, and why it lives HERE
///
/// A key match with a stale stamp is REMOVED and `None` returned — removed as well as withheld,
/// because leaving it resident would let a later take at some future generation find it again, and
/// returning it is the wrong-file bug the flush exists to prevent. `None` reaches the collector as
/// `UNKNOWN`, whose documented meaning is already "request it (again)".
///
/// **This is the safety property, and the only one.** `deluge_card_flush` cannot provide it: its
/// generation bump is unconditional, but [`flush_tables`]'s clears are `try_lock`ed and a contended
/// slot stays populated, so an entry can outlive a flush. Deciding at TAKE, from the caller's own
/// current generation, is what makes the outcome independent of which locks the flush managed to
/// take and of which context either side runs in.
///
/// # An undersized `out` is TERMINAL, not a retry
///
/// A key match with a good stamp but an `out` shorter than the prefix is a caller bug — the header
/// requires `out_cap` to be at least [`PREFIX_CAPACITY`], and
/// [`deluge_file_prefix_collect`] `debug_assert!`s it so a dev build fails at the real cause. The
/// entry is CONSUMED anyway and `Some(PrefixTake::Bytes(0))` returned, which the caller reads as
/// READY with no bytes. Deliberately NOT [`PrefixTake::Failed`]: the read succeeded, and reporting a
/// caller's own buffer bug as an I/O failure would put an SD-card error on screen for a file that
/// is perfectly readable.
///
/// Leaving the prefix resident for "a correctly-sized retry" was the first shape and it was worse
/// than doing nothing: the caller that has just been told UNKNOWN follows the header and
/// re-requests, the task re-opens and re-reads the file from the CARD, [`publish_into`] replaces
/// the entry, and the next collect — with the same small buffer — says UNKNOWN again. That is a
/// once-per-tick re-read of real SD I/O for as long as the caller stays interested: the exact
/// livelock the empty-prefix convention exists to prevent, arriving through a different door. The
/// affordance only ever helped a caller that changed its buffer, and nothing tells it to.
///
/// Terminal-and-blank is the same failure convention the rest of this channel already uses, so a
/// release build degrades to one song's artwork missing rather than to hammering the card.
pub fn take_from(
    slot: &mut PrefixSlot,
    store: &[u8; PREFIX_CAPACITY],
    path: &str,
    generation: u32,
    out: &mut [u8],
) -> Option<PrefixTake> {
    let entry = slot.entry.as_ref()?;
    if entry.key != path {
        return None;
    }
    if entry.generation != generation {
        slot.entry = None; // removed, not merely withheld
        return None;
    }
    let Some(len) = entry.len else {
        // A published failure. Removed like any other answer; `out` is untouched.
        slot.entry = None;
        return Some(PrefixTake::Failed);
    };
    if out.len() < len {
        // Caller bug, and terminal: consume the entry and report an empty prefix rather than
        // leaving one the caller would re-request, and re-read from the card, every tick. See this
        // function's doc.
        slot.entry = None;
        return Some(PrefixTake::Bytes(0));
    }
    out[..len].copy_from_slice(&store[..len]);
    slot.entry = None;
    Some(PrefixTake::Bytes(len))
}

/// One request waiting to be read.
struct PrefixRequest {
    path: PrefixPath,
    /// Already clamped to [`PREFIX_CAPACITY`] by [`deluge_file_prefix_request`].
    ///
    /// Read by `take_next_request`, which is compiled only where the task is (device under
    /// `efatfs_streaming`, or host under `host_app`). A build that has the statics but no task --
    /// the host test binaries -- writes this field and never reads it, which is the honest shape
    /// rather than a defect.
    #[allow(dead_code)]
    max_bytes: u32,
}

/// Raised to wake [`file_prefix_task`] when a prefix is newly requested. Always compiled — with the
/// task absent nobody awaits it and signalling is a harmless flag set.
pub static PREFIX_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// The one outstanding request, if any.
///
/// A single slot rather than a queue, and a newer request REPLACES an older one. That is the
/// single-flight shape stated as storage: a browser cursor that has moved on does not want the path
/// it left behind, and the caller of a dropped request reads `UNKNOWN` — "request it (again)" — on
/// its next tick, which it will only do if it still wants it.
static REQUEST: Mutex<CriticalSectionRawMutex, Option<PrefixRequest>> = Mutex::new(None);

/// The path being read right now, if any. Exists so [`deluge_file_prefix_collect`] can answer
/// `PENDING` rather than `UNKNOWN` in the window between a request leaving [`REQUEST`] and its
/// bytes reaching [`RESULT`] — an `UNKNOWN` there would tell the caller to re-request work that is
/// already running, and it would do so on every tick.
static IN_FLIGHT: Mutex<CriticalSectionRawMutex, Option<PrefixPath>> = Mutex::new(None);

/// The finished prefix waiting to be collected. See [`PrefixSlot`].
static RESULT: Mutex<CriticalSectionRawMutex, PrefixSlot> = Mutex::new(PrefixSlot::new());

/// The published prefix's bytes, paired with the entry in [`RESULT`]. Reached only through
/// [`result_parts`], which demands the `RESULT` guard.
///
/// In `.sdram_bss` on the device, like the byte channel's buffers: NOLOAD, and zeroed by
/// `boot_mem::init_sdram_memory()` before any task runs. A byte array is valid when zeroed; the
/// `Mutex` slots above are not, which is why they stay on-chip and only the bytes move.
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_bss"))]
static mut RESULT_BYTES: [u8; PREFIX_CAPACITY] = [0; PREFIX_CAPACITY];

/// The slot and its bytes, for as long as `guard` is borrowed.
///
/// Taking the `RESULT` guard by `&mut` is the point: the borrow checker then proves the lock is held
/// for every use of the bytes, and the slot and the store come back as two disjoint borrows, so a
/// publish or a take writes both halves in one lock hold.
fn result_parts<'g>(
    guard: &'g mut MutexGuard<'_, CriticalSectionRawMutex, PrefixSlot>,
) -> (&'g mut PrefixSlot, &'g mut [u8; PREFIX_CAPACITY]) {
    // SAFETY: nothing else names RESULT_BYTES, and a reference is handed out only while an exclusive
    // borrow of the RESULT guard is held -- RESULT is the only lock guarding a `PrefixSlot` in this
    // module -- so this is the only live reference.
    let store = unsafe { &mut *core::ptr::addr_of_mut!(RESULT_BYTES) };
    (&mut **guard, store)
}

/// The read in progress, before it is published. [`file_prefix_task`]'s alone -- see
/// [`claim_work_bytes`]. Placed as [`RESULT_BYTES`] is.
#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_bss"))]
static mut WORK_BYTES: [u8; PREFIX_CAPACITY] = [0; PREFIX_CAPACITY];

/// Grants [`WORK_BYTES`] once.
#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
static WORK_CLAIM: super::file_bytes::ClaimOnce = super::file_bytes::ClaimOnce::new();

/// The work buffer, to the first caller only. [`file_prefix_task`] claims it once, at start.
#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
fn claim_work_bytes() -> Option<&'static mut [u8; PREFIX_CAPACITY]> {
    if !WORK_CLAIM.claim() {
        return None;
    }
    // SAFETY: WORK_CLAIM grants exactly one claim for the life of the program and nothing else names
    // WORK_BYTES, so this is the only reference that will ever exist.
    Some(unsafe { &mut *core::ptr::addr_of_mut!(WORK_BYTES) })
}

/// Drop what this channel is holding. The card generation's bump belongs to the CALLER
/// (`card.rs`'s `deluge_card_flush`), which performs it before calling here.
///
/// Runs synchronously from C++ on the interaction tier and must never wait, so all three are
/// `try_lock`ed. **Every one of them may fail, and none of them costs anything**: the clears are
/// housekeeping, and the safety property is the STAMP compared in [`take_from`]. A surviving entry
/// carries the generation of the card it was read from and is therefore uncollectable, not
/// stale-but-live; a surviving in-flight mark reports `PENDING` for one more collect and is cleared
/// by the read that set it; a surviving request has done no I/O yet, so whenever it runs it reads
/// whatever card is in the slot then and is stamped accordingly.
pub(crate) fn flush_tables() {
    if let Ok(mut request) = REQUEST.try_lock() {
        *request = None;
    }
    if let Ok(mut in_flight) = IN_FLIGHT.try_lock() {
        *in_flight = None;
    }
    if let Ok(mut result) = RESULT.try_lock() {
        result.entry = None;
    }
}

/// Whether the Rust prefix channel backs `file_prefix.h` -- always, on this BSP. The symbol stays
/// because a C++-only build links `file_prefix_abi.cpp`'s weak definition instead, which answers
/// `false`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_prefix_async_active() -> bool {
    true
}

/// Milliseconds `file_prefix_task` waits after marking each read in flight. Host harness only; see
/// [`deluge_file_prefix_set_latency_ms`].
#[cfg(feature = "host_app")]
static LATENCY_MS: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

/// Host harness knob: make every prefix read wait `ms` milliseconds after it is marked in flight, so
/// a gate can hold a read while a consumer keeps collecting `PENDING`. 0, the default, means no wait.
///
/// Compiled only under `host_app`: the device image has neither this function nor the static it sets.
#[cfg(feature = "host_app")]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_file_prefix_set_latency_ms(ms: u32) {
    LATENCY_MS.store(ms, core::sync::atomic::Ordering::Relaxed);
}

/// Request the first `max_bytes` of `file_path` and wake the reader. See `file_prefix.h`.
///
/// Silently drops an unusable request (NULL, non-UTF-8, over-long, or the request slot contended):
/// there is no failure the caller could act on that re-requesting does not already cover, and the
/// collect it will run next tick reports `UNKNOWN`, whose documented meaning is exactly that.
///
/// # Safety
///
/// `file_path` must be a valid NUL-terminated C string. It is copied out before this returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_prefix_request(file_path: *const c_char, max_bytes: u32) {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(file_path) }) else {
        return;
    };
    // Clamped HERE, once, so nothing downstream has to defend against a read that could not fit in
    // the slot: `publish_into` may then treat an over-long prefix as a driver bug rather than a
    // runtime condition it has to truncate.
    let max_bytes = max_bytes.min(PREFIX_CAPACITY as u32);
    enqueue_request(path, max_bytes);
}

/// Install the request and wake the reader, replacing any request not yet picked up.
///
/// `try_lock` rather than a block: this is called synchronously from C++ in whatever context the
/// app happens to be in, and must never wait.
fn enqueue_request(path: PrefixPath, max_bytes: u32) {
    let Ok(mut request) = REQUEST.try_lock() else {
        return; // Contended; the caller re-requests on its next tick.
    };
    *request = Some(PrefixRequest { path, max_bytes });
    drop(request);
    PREFIX_WAKE.signal(());
}

/// Collect the prefix read for `file_path`, if one is ready. See `file_prefix.h`.
///
/// # Why the probe order is REQUEST -> IN_FLIGHT -> RESULT
///
/// An entry only ever moves forward: [`REQUEST`] -> [`IN_FLIGHT`] -> [`RESULT`]. Three rules make a
/// forward pass unable to miss one, and all three are load-bearing — they are the audio loader's,
/// unchanged, and its `deluge_audio_load_collect` doc argues each at length:
///
/// 1. [`take_next_request`] sets `IN_FLIGHT` while still holding the `REQUEST` lock.
/// 2. The task publishes to `RESULT` *before* clearing `IN_FLIGHT`, so that handoff overlaps
///    rather than gaps.
/// 3. [`is_requested`] and [`is_in_flight`] report `true` when they cannot take their lock — it is
///    the LOCK, not the table, that covers the REQUEST -> IN_FLIGHT window, in which the entry
///    genuinely is in neither.
///
/// Reading in the reverse order can report `UNKNOWN` for a read that has already landed, so the
/// caller re-requests work that is done.
///
/// # Safety
///
/// `file_path` must be a valid NUL-terminated C string; `out_buf` must point to at least `out_cap`
/// writable bytes and `out_len` to a writable `uint32_t`. Both outputs are written ONLY on
/// [`DELUGE_FILE_PREFIX_READY`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_file_prefix_collect(
    file_path: *const c_char,
    out_buf: *mut u8,
    out_cap: u32,
    out_len: *mut u32,
) -> u8 {
    if out_buf.is_null() || out_len.is_null() {
        return DELUGE_FILE_PREFIX_UNKNOWN;
    }
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_path(file_path) }) else {
        return DELUGE_FILE_PREFIX_UNKNOWN;
    };
    // The header's requirement, checked where the caller can still be identified. A buffer shorter
    // than the slot cannot hold every prefix this channel can publish, and the degradation
    // (`take_from` consumes the entry and reports an empty prefix) is silent by design — so a dev
    // build fails HERE, at the cause, rather than presenting as artwork that will not draw.
    debug_assert!(
        out_cap as usize >= PREFIX_CAPACITY,
        "`out_cap` must be at least DELUGE_FILE_PREFIX_CAPACITY"
    );
    // SAFETY: `out_buf` is non-null (checked above) and the caller promises `out_cap` writable
    // bytes there. Nothing here reads the buffer, so its prior contents need not be initialised
    // beyond what `&mut [u8]` already requires of the caller's own allocation.
    let out = unsafe { core::slice::from_raw_parts_mut(out_buf, out_cap as usize) };

    match probe(&path, out) {
        Probe::Ready(len) => {
            // SAFETY: `out_len` is non-null (checked above) and the caller's contract makes it a
            // writable, aligned `uint32_t`. Written only on READY, as the header promises.
            unsafe { *out_len = len as u32 };
            DELUGE_FILE_PREFIX_READY
        }
        Probe::Pending => DELUGE_FILE_PREFIX_PENDING,
        Probe::Unknown => DELUGE_FILE_PREFIX_UNKNOWN,
        Probe::Failed => DELUGE_FILE_PREFIX_FAILED,
    }
}

/// What one forward pass over the channel found.
///
/// Deliberately produced by a SINGLE pass. Asking "is it ready?" and "is it still coming?" as two
/// separate passes reintroduces the race the ordering exists to close: the reader can publish and
/// clear its in-flight mark between them, and the second pass then reports `UNKNOWN` for a prefix
/// the first pass merely missed -- so the caller re-requests a read that has already landed and
/// orphans the bytes it left behind.
enum Probe {
    /// `out` has been written, with this many bytes. **Only this case writes it**, and the C side is
    /// told so: `file_prefix.h` lets a caller pass an uninitialised buffer and read it back only on
    /// READY. `live_pipeline`'s `collect` seeds a sentinel and every PENDING/UNKNOWN test asserts it
    /// survives, so a future branch that writes a placeholder fails there rather than in a caller
    /// that trusted the contract.
    Ready(usize),
    Pending,
    Unknown,
    /// The read did not happen, and the caller is told so rather than being handed an empty prefix
    /// it could not tell from an empty file. Terminal — see `file_prefix.h`'s
    /// `DELUGE_FILE_PREFIX_FAILED`.
    Failed,
}

/// One forward pass: REQUEST, then IN_FLIGHT, then RESULT. See [`deluge_file_prefix_collect`]'s doc
/// for why the order and the contention mapping are both load-bearing.
fn probe(path: &str, out: &mut [u8]) -> Probe {
    if is_requested(path) || is_in_flight(path) {
        return Probe::Pending;
    }
    // A contended slot means the reader is mid-publish. `Pending` is the honest answer: never claim
    // `Unknown` on contention, or the caller re-requests work that is about to land.
    let Ok(mut result) = RESULT.try_lock() else {
        return Probe::Pending;
    };
    // The card generation is read HERE, at the take, not sampled earlier: it is the collector's own
    // view of which card is in the slot right now, and deciding from it is what makes a prefix's
    // fate independent of whether the flush that invalidated it managed to take this lock. See
    // [`take_from`].
    let (slot, store) = result_parts(&mut result);
    match take_from(slot, store, path, deluge_card_generation::current(), out) {
        Some(PrefixTake::Bytes(len)) => Probe::Ready(len),
        Some(PrefixTake::Failed) => Probe::Failed,
        None => Probe::Unknown,
    }
}

/// Whether `path` is the request not yet picked up.
///
/// **Contention reports `true`, and that is load-bearing, not defensive.** The reader marks a
/// request in flight while still holding this lock, so a collect racing that handoff finds the lock
/// taken — mapping it to `false` here would report `UNKNOWN` for a request mid-handoff, which is
/// exactly the bug the probe order exists to prevent. Do not "simplify" this branch.
fn is_requested(path: &str) -> bool {
    match REQUEST.try_lock() {
        Ok(request) => request.as_ref().is_some_and(|r| r.path == path),
        Err(_) => true,
    }
}

/// Whether `path` is the read running right now. Contention reports `true`, for the same reason as
/// [`is_requested`] — and with the same warning.
fn is_in_flight(path: &str) -> bool {
    match IN_FLIGHT.try_lock() {
        Ok(in_flight) => in_flight.as_deref() == Some(path),
        Err(_) => true,
    }
}

/// Copy a C string into a fixed-capacity [`PrefixPath`]. `None` for NULL, non-UTF-8, or a path
/// longer than the bound — a path that does not fit is refused rather than truncated, because a
/// truncated path names a *different* file that may well exist.
///
/// # Safety
///
/// `ptr` must be NULL or a valid NUL-terminated C string.
unsafe fn cstr_to_path(ptr: *const c_char) -> Option<PrefixPath> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: the caller's contract, above.
    let cstr = unsafe { CStr::from_ptr(ptr) };
    PrefixPath::try_from(cstr.to_str().ok()?).ok()
}

// ── The read driver ─────────────────────────────────────────────────────────

/// Read up to `max_bytes` of `path`'s opening bytes into `out`, returning how many landed.
///
/// `open` -> `size_of` -> one `read_at(0, ..)` -> `close`, with the handle released on the single
/// path out — the same shape, and the same handle discipline, as `load_io_once`.
///
/// # `None` is not the same as `Some(0)`
///
/// `None` means the read did not happen: the file would not open, could not be sized, or the read
/// itself failed. `Some(0)` means the file is genuinely empty. [`file_prefix_task`] carries the
/// distinction all the way out — `None` publishes a FAILURE and `Some(0)` an empty prefix, and
/// `deluge_file_prefix_collect` reports them as `DELUGE_FILE_PREFIX_FAILED` and READY-with-zero.
///
/// It was not always so: both used to publish an empty prefix, and the caller then had no way to
/// tell an unreadable song from one with no artwork. It drew blank for both while the synchronous
/// backing it stands in for put an error on screen for the first. Collapsing them again undoes
/// that, and does it silently — the two answers are the same length and draw the same pads.
pub async fn prefix_io_once<O: LoadOps>(
    ops: &O,
    path: &str,
    max_bytes: u32,
    out: &mut [u8],
) -> Option<usize> {
    let handle = match ops.open(path).await {
        OpenVerdict::Opened(handle) => handle,
        // Neither verdict leaves anything for this channel to do, and neither is retried: the
        // caller publishes a FAILURE for both, which `file_prefix.h` makes TERMINAL, so there is no
        // next request unless the caller asks for this path again of its own accord.
        //
        // That is deliberate for `TableFull` too, even though a full efatfs handle table says
        // nothing about this file. Making this one failure transient would give one function two
        // conventions and reintroduce a retry that can livelock if handles never free.
        //
        // ## What terminal COSTS here, restated for what a failure now does
        //
        // It used to blank one song's artwork until the cursor next visited it. It no longer does:
        // a published failure reaches the browser as `SongPreviewOutcome::Error` and it shows the
        // user an error popup — for a song that is perfectly readable and that the synchronous
        // backing, on the owner where the file read is serialised, would very likely have opened.
        // Still bounded and still self-healing (the next visit re-asks and the table has drained),
        // but the symptom is now visible rather than silent, and the trade has to be taken on that
        // rather than on the old one.
        //
        // ## The bounded mitigation, recorded and NOT taken
        //
        // The sibling loader shows the shape: `audio_loader.rs`'s `load_io_once` returns
        // `DELUGE_ERROR_TOO_MANY_OPEN_STREAMS` under its own identity, so the C++ collector reclaims
        // unleased handles and asks again rather than reading a full table as "nothing is here".
        // This channel could do the same with ONE more bit on the boundary — a terminal-vs-transient
        // failure, where transient means "reclaim and re-request once". The cost is exactly what
        // this module's doc argues against: a second convention on a boundary whose whole safety
        // property is that every answer is terminal, and a retry that has to be bounded by something
        // if handles never free. Not built. If it is ever wanted, it belongs on `file_prefix.h`
        // beside `DELUGE_FILE_PREFIX_FAILED`, not as a special case here.
        OpenVerdict::Missing | OpenVerdict::TableFull => return None,
    };
    let read = read_prefix(ops, handle, max_bytes, out).await;
    // The single close, on the one path out of the read.
    ops.close(handle).await;
    read
}

/// Fill `out` from the front of the file behind `handle`. Never closes it — see [`prefix_io_once`].
async fn read_prefix<O: LoadOps>(
    ops: &O,
    handle: u32,
    max_bytes: u32,
    out: &mut [u8],
) -> Option<usize> {
    let size = ops.size_of(handle).await?;
    // Bounded by BOTH: `max_bytes` is what the caller asked for, `size` is what exists.
    // `LoadOps::read_at` is documented never to come up short at end of file, so a request past the
    // end would be a read FAILURE rather than a partial fill.
    let want = size.min(max_bytes) as usize;
    // `deluge_file_prefix_request` clamps `max_bytes` to `PREFIX_CAPACITY` and the task's buffer is
    // that size, so this cannot bite; clamping rather than slicing blindly keeps a future caller
    // that passes a smaller `out` from panicking on the interrupt executor, which would take the
    // whole thing down.
    debug_assert!(want <= out.len());
    let len = want.min(out.len());
    // A zero-length read is legitimate (an empty file) and trivially succeeds.
    if !ops.read_at(handle, 0, &mut out[..len]).await {
        return None;
    }
    Some(len)
}

// ── The task ────────────────────────────────────────────────────────────────

/// The prefix reader: wakes on [`PREFIX_WAKE`], serves the outstanding request through the audio
/// loader's [`ProdOps`](super::audio_loader::ProdOps), publishes the bytes, then goes back to sleep.
///
/// [`IN_FLIGHT`] is set before the I/O and cleared only after the prefix is published, so
/// [`deluge_file_prefix_collect`] never sees a gap in which a running read looks unknown.
///
/// # A failed read publishes a FAILURE, and publishing it is not optional
///
/// Publishing nothing would report `UNKNOWN`, whose documented meaning is "request it (again)" —
/// so a file that cannot be read would be re-requested and re-read on every single tick, forever,
/// for as long as the cursor sits on it. A published failure ends that: it is terminal, exactly as
/// a published prefix is.
///
/// It publishes a FAILURE rather than an empty prefix, which is what this did first. An empty
/// prefix is terminal too, but it is the same answer a genuinely empty FILE gets, and the caller
/// this channel was built for shows the user an error for one and blank artwork for the other. It
/// could not, and the popups the synchronous backing shows would have gone missing the moment the
/// channel was switched on.
#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
#[embassy_executor::task]
pub async fn file_prefix_task() {
    PREFIX_WAKE.wait().await;
    let ops = super::audio_loader::ProdOps::new();
    // One buffer for the life of the task, and not in the task: an 8 KiB array held across the awaits
    // below would sit in this task's pool, in on-chip SRAM. See `WORK_BYTES`.
    let Some(work) = claim_work_bytes() else {
        debug_assert!(
            false,
            "file_prefix_task claims its work buffer once; its pool size is 1"
        );
        return;
    };
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
            // `None` is a failed read, and it is published AS a failure -- see this task's doc.
            let read = prefix_io_once(&ops, &request.path, request.max_bytes, &mut work[..]).await;
            // Publish BEFORE clearing IN_FLIGHT, never after: the entry is briefly in both tables,
            // which is harmless, whereas the reverse leaves a window in which it is in neither and
            // `collect` would report `UNKNOWN` for a read that just succeeded.
            //
            // `generation` was sampled before the I/O above, so a card pulled during it is what
            // makes this publish a no-op.
            publish_result(&request.path, read.map(|len| &work[..len]), generation).await;
            mark_in_flight(None).await;
        }
        PREFIX_WAKE.wait().await;
    }
}

/// Take the outstanding request and mark it in flight, atomically with respect to
/// [`deluge_file_prefix_collect`], and report the card generation the read begins under.
///
/// The mark happens while the [`REQUEST`] lock is still held, so there is no instant at which the
/// request has left the slot but not yet appeared as in-flight. A collect racing this either sees
/// it requested, or blocks on the `try_lock` and reports `PENDING` — never `UNKNOWN`.
///
/// The generation is read HERE, before the first `await` that can suspend this task and therefore
/// before any I/O, because that is what makes it mean "the card this read is reading". Reading it
/// later — at publish time — reads the value a flush has already installed and defeats the check
/// entirely.
#[cfg(any(
    all(target_os = "none", feature = "efatfs_streaming"),
    feature = "host_app"
))]
async fn take_next_request() -> Option<(PrefixRequest, u32)> {
    let mut request = REQUEST.lock().await;
    let request = request.take()?;
    let generation = deluge_card_generation::current();
    mark_in_flight(Some(request.path.clone())).await;
    Some((request, generation))
}

/// Record (or clear) the path being read right now. Called by [`file_prefix_task`] while it still
/// holds the [`REQUEST`] lock — see [`take_next_request`].
///
/// Also the seam the host tests drive to exercise [`deluge_file_prefix_collect`]'s IN_FLIGHT
/// branch, which no test could reach otherwise because no test runs the task.
pub async fn mark_in_flight(path: Option<PrefixPath>) {
    *IN_FLIGHT.lock().await = path;
}

/// Publish one finished read into the live [`RESULT`] slot — `Some(bytes)` for a prefix, `None` for
/// a read that did not happen — stamped with the card generation the read began under, and skip the
/// publish entirely if that card is already gone.
///
/// # The early-drop check is an optimisation, not the guarantee
///
/// There is no happens-before edge from `deluge_card_generation::bump`'s `fetch_add` to this `load`
/// when this task wins the `RESULT` lock (the two synchronise only through the lock, in the other
/// direction), so this check can legitimately read the pre-flush generation, pass, and publish. The
/// entry it publishes still carries the pre-flush stamp, and [`take_from`] drops it. Do not rewrite
/// this as the safety property.
///
/// The check happens with the lock already held, not before acquiring it, so that it is at least
/// ordered against a flush that DID take the lock.
pub async fn publish_result(key: &str, bytes: Option<&[u8]>, generation: u32) {
    let mut result = RESULT.lock().await;
    if generation != deluge_card_generation::current() {
        // The card this was read from is gone. Dropping costs the I/O that produced it and nothing
        // else, and the collector reads the resulting `UNKNOWN` as "request it again" -- which it
        // then does, against the card that is actually in the slot.
        //
        // A FAILED read is dropped here too, and must be: the failure belongs to the card that has
        // gone, and reporting it as terminal against the card now in the slot would put an error on
        // screen for a file nobody has tried to read yet.
        return;
    }
    let (slot, store) = result_parts(&mut result);
    match bytes {
        Some(bytes) => publish_into(slot, store, key, bytes, generation),
        None => publish_failure_into(slot, key, generation),
    }
}
