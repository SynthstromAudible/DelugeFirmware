//! The single-slot prefix channel. Song preview is single-flight by construction (one browser, one
//! coalescer), so this is a slot, not a table: no depth, no eviction, no ordering.
//!
//! `deluge-bsp-rust` is bin-only (no `[lib]` target — see `Cargo.toml`), so this recompiles
//! `src/file_prefix.rs` unmodified into this test binary via `#[path]`, the same convention
//! `tests/audio_loader_host.rs` uses. `src/audio_loader.rs` comes along because the prefix driver
//! reuses its [`LoadOps`] rather than declaring a second four-operation filesystem trait; it is a
//! SIBLING module here, which is what makes `file_prefix.rs`'s `super::audio_loader` path resolve
//! in this binary exactly as it does in the firmware.
#![cfg(feature = "async_file_prefix")]
#![cfg(not(target_os = "none"))]

use std::cell::{Cell, RefCell};

// Included for its `LoadOps`/`OpenVerdict` alone, so the rest of the loader is dead here. That is
// not a defect to fix by trimming the shared file -- it is fully exercised by
// `tests/audio_loader_host.rs`, its own suite -- so the lint is silenced at the module that
// causes it rather than anywhere inside production source.
#[allow(dead_code)]
#[path = "../src/audio_loader.rs"]
mod audio_loader;

#[path = "../src/file_prefix.rs"]
mod file_prefix;

use audio_loader::{LoadOps, OpenVerdict};
use file_prefix::{PREFIX_CAPACITY, PrefixSlot, prefix_io_once, publish_into, take_from};

/// Nonzero on purpose: 0 is what a dropped stamp would leave behind, so a slot built at 0 would let a
/// publish that ignored its generation still satisfy every take.
const CARD: u32 = 3;

#[test]
fn a_published_prefix_is_taken_exactly_once() {
    let mut slot = PrefixSlot::default();
    publish_into(&mut slot, "SONGS/A.XML", b"<song preview=\"00\"", CARD);
    let mut out = [0u8; 64];
    let n = take_from(&mut slot, "SONGS/A.XML", CARD, &mut out).expect("published");
    assert_eq!(&out[..n], b"<song preview=\"00\"");
    assert!(take_from(&mut slot, "SONGS/A.XML", CARD, &mut out).is_none());
}

/// A different path is absent, not a mis-served neighbour. With one slot this is the whole of the
/// keying discipline.
#[test]
fn a_prefix_for_another_path_is_absent() {
    let mut slot = PrefixSlot::default();
    publish_into(&mut slot, "SONGS/A.XML", b"aaa", CARD);
    let mut out = [0u8; 64];
    assert!(take_from(&mut slot, "SONGS/B.XML", CARD, &mut out).is_none());
}

/// The belt: a prefix read from the previous card is dropped at the TAKE, and does not stay resident
/// either. Withholding without removing would only defer the problem to the next generation.
#[test]
fn a_prefix_from_a_previous_card_is_dropped_not_returned() {
    let mut slot = PrefixSlot::default();
    publish_into(&mut slot, "SONGS/A.XML", b"old card", CARD);
    let mut out = [0u8; 64];
    assert!(take_from(&mut slot, "SONGS/A.XML", CARD + 1, &mut out).is_none());
    assert!(
        slot.is_empty(),
        "the stale prefix must be removed, not merely withheld"
    );
}

/// The race flush cannot win: there is no happens-before edge from a flush's bump to a concurrent
/// publish's read of the generation, so a publish stamped BEFORE the bump can land after it. The take
/// is what refuses it.
#[test]
fn a_prefix_stamped_before_a_flush_cannot_be_taken_after_it() {
    let mut slot = PrefixSlot::default();
    publish_into(&mut slot, "SONGS/A.XML", b"landed late", CARD); // stamped pre-bump
    let mut out = [0u8; 64];
    assert!(take_from(&mut slot, "SONGS/A.XML", CARD + 1, &mut out).is_none());
    assert!(slot.is_empty());
}

/// A buffer smaller than the prefix is a caller bug, not a truncation to paper over — and the answer
/// is TERMINAL: consume the entry and report an empty prefix (which reaches the caller as READY with
/// no bytes, i.e. blank artwork).
///
/// Leaving it resident for "a correctly-sized retry" is what this replaced, and it was a livelock:
/// the caller is told UNKNOWN, the header tells it to request again, the task re-reads the file from
/// the CARD, and the next collect with the same buffer says UNKNOWN again — once per tick, with real
/// SD I/O each time.
#[test]
fn a_too_small_out_buffer_consumes_the_prefix_and_takes_nothing() {
    let mut slot = PrefixSlot::default();
    publish_into(&mut slot, "SONGS/A.XML", b"0123456789", CARD);
    let mut out = [0u8; 4];
    assert_eq!(take_from(&mut slot, "SONGS/A.XML", CARD, &mut out), Some(0));
    assert!(
        slot.is_empty(),
        "a short buffer must not leave a prefix the caller would re-request forever"
    );
    assert_eq!(
        out, [0u8; 4],
        "nothing may be written into a buffer that could not hold the prefix"
    );
}

/// The branch ORDER inside `take_from` is load-bearing: the generation is checked BEFORE the buffer
/// size. Hoisting the size check above it — a plausible "simplify", since both end the take — would
/// make a stale entry meeting a short buffer report `Some(0)`, which the collector turns into READY
/// with blank artwork for a song on the card that is in the slot NOW. The caller would take that as
/// a real answer and never re-request, so a card swap would silently blank artwork.
///
/// Stale wins: `None`, so the collector says UNKNOWN and asks again against the current card. The
/// entry is still removed either way.
#[test]
fn a_stale_prefix_meeting_a_short_buffer_is_refused_not_blanked() {
    let mut slot = PrefixSlot::default();
    publish_into(&mut slot, "SONGS/A.XML", b"0123456789", CARD);
    let mut out = [0u8; 4];
    assert!(take_from(&mut slot, "SONGS/A.XML", CARD + 1, &mut out).is_none());
    assert!(slot.is_empty());
}

// ── The driver ──────────────────────────────────────────────────────────────
// `prefix_io_once` is the only code in this module that touches a file, and its whole contract is
// three properties a review cannot check: the read is bounded by BOTH the file's size and the
// caller's ceiling, the handle is released on every path out, and a failure is distinguishable from
// a genuinely empty file.

/// One call [`FakeOps`] observed, in order.
#[derive(Debug, PartialEq, Eq, Clone)]
enum Call {
    Open(String),
    SizeOf,
    Read { offset: u32, len: usize },
    Close,
}

/// Counting double for [`LoadOps`], serving files from memory.
///
/// `open_handles` is the LIVE count, asserted zero at the end of every test: the efatfs handle table
/// has 128 slots, so a leaked handle is silent capacity loss rather than an error anything reports.
/// Every operation resolves its HANDLE (not "whatever was opened last"), so a driver that read
/// through a stale one panics here instead of quietly serving the right bytes for the wrong file.
struct FakeOps {
    files: Vec<(String, Vec<u8>)>,
    /// Paths whose `open` reports a full efatfs handle table rather than a miss.
    table_full_for: Vec<String>,
    /// Paths that open but cannot be sized.
    size_fails_for: Vec<String>,
    /// `read_at` fails outright (a card pulled mid-read).
    read_fails: bool,

    calls: RefCell<Vec<Call>>,
    open_handles: Cell<i32>,
    next_handle: Cell<u32>,
    live: RefCell<Vec<(u32, String, Vec<u8>)>>,
}

impl FakeOps {
    fn new() -> Self {
        Self {
            files: Vec::new(),
            table_full_for: Vec::new(),
            size_fails_for: Vec::new(),
            read_fails: false,
            calls: RefCell::new(Vec::new()),
            open_handles: Cell::new(0),
            next_handle: Cell::new(7),
            live: RefCell::new(Vec::new()),
        }
    }

    fn with_file(mut self, path: &str, bytes: Vec<u8>) -> Self {
        self.files.push((String::from(path), bytes));
        self
    }

    fn with_table_full_for(mut self, path: &str) -> Self {
        self.table_full_for.push(String::from(path));
        self
    }

    fn with_size_failing_for(mut self, path: &str) -> Self {
        self.size_fails_for.push(String::from(path));
        self
    }

    fn with_read_failing(mut self) -> Self {
        self.read_fails = true;
        self
    }

    fn calls(&self) -> Vec<Call> {
        self.calls.borrow().clone()
    }

    fn record(&self, call: Call) {
        self.calls.borrow_mut().push(call);
    }

    /// The invariant that holds at the end of EVERY read: no handle left open.
    fn assert_settled(&self) {
        assert_eq!(
            self.open_handles.get(),
            0,
            "efatfs handle leaked: {:?}",
            self.calls()
        );
    }

    /// The path and bytes behind a live handle. Panics if `handle` was never issued or has already
    /// been closed — either is a real defect in the driver, not a harness detail.
    fn entry_for(&self, handle: u32) -> (String, Vec<u8>) {
        let live = self.live.borrow();
        live.iter()
            .find(|(live_handle, _, _)| *live_handle == handle)
            .map(|(_, path, bytes)| (path.clone(), bytes.clone()))
            .unwrap_or_else(|| {
                panic!(
                    "operation on handle {handle}, which is not open (live: {:?})",
                    live.iter().map(|(h, _, _)| *h).collect::<Vec<_>>()
                )
            })
    }
}

impl LoadOps for FakeOps {
    async fn open(&self, path: &str) -> OpenVerdict {
        self.record(Call::Open(String::from(path)));
        if self.table_full_for.iter().any(|p| p == path) {
            return OpenVerdict::TableFull;
        }
        let Some(bytes) = self
            .files
            .iter()
            .find(|(candidate, _)| candidate == path)
            .map(|(_, bytes)| bytes.clone())
        else {
            return OpenVerdict::Missing;
        };
        // A fresh handle every time, never reused, so a driver holding a stale one cannot
        // accidentally land on a live entry.
        let handle = self.next_handle.get();
        self.next_handle.set(handle + 1);
        self.live
            .borrow_mut()
            .push((handle, String::from(path), bytes));
        self.open_handles.set(self.open_handles.get() + 1);
        OpenVerdict::Opened(handle)
    }

    async fn size_of(&self, handle: u32) -> Option<u32> {
        self.record(Call::SizeOf);
        let (path, bytes) = self.entry_for(handle);
        if self.size_fails_for.contains(&path) {
            return None;
        }
        Some(bytes.len() as u32)
    }

    async fn read_at(&self, handle: u32, offset: u32, buf: &mut [u8]) -> bool {
        self.record(Call::Read {
            offset,
            len: buf.len(),
        });
        let (_, bytes) = self.entry_for(handle);
        if self.read_fails {
            return false;
        }
        if buf.is_empty() {
            return true; // Mirrors `efatfs_fs::read_at`: an empty fill trivially succeeds.
        }
        let start = offset as usize;
        let Some(end) = start.checked_add(buf.len()) else {
            return false;
        };
        if end > bytes.len() {
            return false; // A real read failure: the driver clamps inside the file.
        }
        buf.copy_from_slice(&bytes[start..end]);
        true
    }

    async fn close(&self, handle: u32) {
        self.record(Call::Close);
        let mut live = self.live.borrow_mut();
        let before = live.len();
        live.retain(|(live_handle, _, _)| *live_handle != handle);
        assert_eq!(before, live.len() + 1, "close of a handle that is not open");
        self.open_handles.set(self.open_handles.get() - 1);
    }
}

fn block_on<F: core::future::Future>(future: F) -> F::Output {
    embassy_futures::block_on(future)
}

#[test]
fn a_short_file_is_read_whole_and_the_handle_released() {
    let ops = FakeOps::new().with_file("SONGS/A.XML", b"<song>".to_vec());
    let mut buffer = [0u8; PREFIX_CAPACITY];
    let n = block_on(prefix_io_once(&ops, "SONGS/A.XML", 4096, &mut buffer)).expect("read");
    assert_eq!(&buffer[..n], b"<song>");
    assert_eq!(
        ops.calls(),
        vec![
            Call::Open(String::from("SONGS/A.XML")),
            Call::SizeOf,
            Call::Read { offset: 0, len: 6 },
            Call::Close,
        ]
    );
    ops.assert_settled();
}

/// The ceiling, not the file, bounds a long file's read. Asking for the whole file would blow the
/// slot the result has to fit in.
#[test]
fn a_long_file_is_read_only_up_to_the_ceiling() {
    let ops = FakeOps::new().with_file("SONGS/A.XML", vec![b'x'; PREFIX_CAPACITY * 2]);
    let mut buffer = [0u8; PREFIX_CAPACITY];
    let n = block_on(prefix_io_once(&ops, "SONGS/A.XML", 128, &mut buffer)).expect("read");
    assert_eq!(n, 128);
    assert_eq!(
        ops.calls(),
        vec![
            Call::Open(String::from("SONGS/A.XML")),
            Call::SizeOf,
            Call::Read {
                offset: 0,
                len: 128
            },
            Call::Close,
        ]
    );
    ops.assert_settled();
}

/// An empty file reports a zero-length prefix, NOT a failure: there is genuinely nothing to read,
/// which is a different answer from "the read did not happen".
#[test]
fn an_empty_file_reads_an_empty_prefix() {
    let ops = FakeOps::new().with_file("SONGS/A.XML", Vec::new());
    let mut buffer = [0u8; PREFIX_CAPACITY];
    assert_eq!(
        block_on(prefix_io_once(&ops, "SONGS/A.XML", 4096, &mut buffer)),
        Some(0)
    );
    ops.assert_settled();
}

#[test]
fn a_missing_file_reads_nothing_and_opens_no_handle() {
    let ops = FakeOps::new();
    let mut buffer = [0u8; PREFIX_CAPACITY];
    assert_eq!(
        block_on(prefix_io_once(&ops, "SONGS/A.XML", 4096, &mut buffer)),
        None
    );
    assert_eq!(ops.calls(), vec![Call::Open(String::from("SONGS/A.XML"))]);
    ops.assert_settled();
}

/// A full efatfs handle table is not a verdict about this file, but this channel has no way to say
/// so and nothing that could act on it if it did — the artwork is simply not available this tick.
#[test]
fn a_full_handle_table_reads_nothing() {
    let ops = FakeOps::new()
        .with_file("SONGS/A.XML", b"<song>".to_vec())
        .with_table_full_for("SONGS/A.XML");
    let mut buffer = [0u8; PREFIX_CAPACITY];
    assert_eq!(
        block_on(prefix_io_once(&ops, "SONGS/A.XML", 4096, &mut buffer)),
        None
    );
    ops.assert_settled();
}

/// Every failure after the open still releases the handle: the size failure and the read failure are
/// the two paths out of the read that are not the happy one.
#[test]
fn a_failure_after_the_open_still_closes_the_handle() {
    let sized = FakeOps::new()
        .with_file("SONGS/A.XML", b"<song>".to_vec())
        .with_size_failing_for("SONGS/A.XML");
    let mut buffer = [0u8; PREFIX_CAPACITY];
    assert_eq!(
        block_on(prefix_io_once(&sized, "SONGS/A.XML", 4096, &mut buffer)),
        None
    );
    assert_eq!(sized.calls().last(), Some(&Call::Close));
    sized.assert_settled();

    let read = FakeOps::new()
        .with_file("SONGS/A.XML", b"<song>".to_vec())
        .with_read_failing();
    assert_eq!(
        block_on(prefix_io_once(&read, "SONGS/A.XML", 4096, &mut buffer)),
        None
    );
    assert_eq!(read.calls().last(), Some(&Call::Close));
    read.assert_settled();
}

// ── The live request/collect pipeline ───────────────────────────────────────
// The C ABI over the process-wide statics. Only exists where its storage does, so the whole module
// is gated on the switch — with it off there is no REQUEST, no IN_FLIGHT and no RESULT to drive.

/// The ejection hook, recompiled here the same way `tests/audio_loader_host.rs` does it: the flush
/// test below drives `deluge_card_flush` rather than `flush_tables` directly, because "the bump
/// precedes the clears, and the take is what refuses a survivor" is the whole of what it pins.
///
/// It reaches its loaders through `super::`, which is what makes it resolve both here (at this
/// crate's root) and one module deeper in `crates/deluge_audio_loader_host`.
#[cfg(feature = "async_file_prefix_active")]
#[path = "../src/card.rs"]
mod card;

#[cfg(feature = "async_file_prefix_active")]
mod live_pipeline {
    use super::file_prefix::{
        DELUGE_FILE_PREFIX_PENDING, DELUGE_FILE_PREFIX_READY, DELUGE_FILE_PREFIX_UNKNOWN,
        PREFIX_CAPACITY, PrefixPath, deluge_file_prefix_async_active, deluge_file_prefix_collect,
        deluge_file_prefix_request, mark_in_flight, publish_result,
    };
    use super::{block_on, card};
    use std::ffi::CString;

    /// `REQUEST`/`IN_FLIGHT`/`RESULT` are process-wide and cargo runs tests on parallel threads, so
    /// these must not interleave.
    static PIPELINE_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

    /// What [`collect`] seeds its outputs with. Neither may be written unless the status is READY.
    const UNTOUCHED: u8 = 0xFF;
    const UNTOUCHED_LEN: u32 = 0xDEAD_BEEF;

    /// Start from a known-empty channel, whatever the test before this one left behind.
    fn reset() -> std::sync::MutexGuard<'static, ()> {
        let guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        card::deluge_card_flush();
        guard
    }

    /// Collect into a full-capacity buffer, returning the status, the bytes, and the reported
    /// length. The buffer is seeded so the "written only on READY" half of the contract can be
    /// asserted rather than assumed.
    fn collect(path: &str) -> (u8, Vec<u8>, u32) {
        let key = CString::new(path).unwrap();
        let mut buffer = vec![UNTOUCHED; PREFIX_CAPACITY];
        let mut len = UNTOUCHED_LEN;
        // SAFETY: `key` is a valid NUL-terminated C string, `buffer` holds the capacity passed, and
        // `len` is a live `u32`.
        let status = unsafe {
            deluge_file_prefix_collect(
                key.as_ptr(),
                buffer.as_mut_ptr(),
                buffer.len() as u32,
                &mut len,
            )
        };
        (status, buffer, len)
    }

    /// Assert the outputs came back exactly as [`collect`] seeded them — the NEGATIVE half of
    /// `file_prefix.h`'s contract, which a status-only assertion cannot see: a branch that wrote a
    /// placeholder length, or hoisted the copy above the take, would leave every status right.
    fn assert_untouched(buffer: &[u8], len: u32) {
        assert_eq!(len, UNTOUCHED_LEN, "`out_len` is written only on READY");
        assert!(
            buffer.iter().all(|&b| b == UNTOUCHED),
            "`out_buf` is written only on READY"
        );
    }

    fn request(path: &str, max_bytes: u32) {
        let key = CString::new(path).unwrap();
        // SAFETY: `key` is a valid NUL-terminated C string.
        unsafe { deluge_file_prefix_request(key.as_ptr(), max_bytes) };
    }

    /// Publish as the task would for a read that began just now: stamped with the CURRENT card
    /// generation, so nothing here is accidentally testing the flush's drop path.
    fn publish(path: &str, bytes: &[u8]) {
        block_on(publish_result(
            path,
            bytes,
            deluge_card_generation::current(),
        ));
    }

    /// The switch is what the C++ side asks before using this channel at all, and this build has it
    /// on — otherwise every test below would be driving storage the firmware never reaches.
    #[test]
    fn the_switch_reports_active_on_this_build() {
        assert!(deluge_file_prefix_async_active());
    }

    /// A path never asked about is `UNKNOWN`, not `PENDING` — `PENDING` would tell the caller to
    /// keep asking for something that will never arrive.
    #[test]
    fn an_unrequested_path_is_unknown() {
        let _guard = reset();
        let (status, buffer, len) = collect("never/asked.XML");
        assert_eq!(status, DELUGE_FILE_PREFIX_UNKNOWN);
        assert_untouched(&buffer, len);
    }

    /// A requested-but-unread path reports `PENDING`, so the caller waits instead of re-requesting
    /// work already outstanding.
    #[test]
    fn a_requested_path_is_pending() {
        let _guard = reset();
        request("SONGS/PENDING.XML", 4096);
        let (status, buffer, len) = collect("SONGS/PENDING.XML");
        assert_eq!(status, DELUGE_FILE_PREFIX_PENDING);
        assert_untouched(&buffer, len);
    }

    /// A read running RIGHT NOW reports `PENDING`, not `UNKNOWN`. No test runs the task, so
    /// `mark_in_flight` — the seam the task uses — is driven directly to reach the real branch.
    ///
    /// A false `UNKNOWN` here would tell the caller to re-request a read that is already running,
    /// on every tick.
    #[test]
    fn an_in_flight_read_is_pending_not_unknown() {
        let _guard = reset();
        block_on(mark_in_flight(Some(
            PrefixPath::try_from("SONGS/INFLIGHT.XML").unwrap(),
        )));
        assert_eq!(collect("SONGS/INFLIGHT.XML").0, DELUGE_FILE_PREFIX_PENDING);
        // A DIFFERENT path is still unknown — the branch matches on identity, not on "something is
        // in flight".
        assert_eq!(collect("SONGS/OTHER.XML").0, DELUGE_FILE_PREFIX_UNKNOWN);
        block_on(mark_in_flight(None));
        assert_eq!(collect("SONGS/INFLIGHT.XML").0, DELUGE_FILE_PREFIX_UNKNOWN);
    }

    /// The whole round trip: a published prefix collects READY once, with its bytes and length, and
    /// is gone afterwards.
    #[test]
    fn a_published_prefix_collects_ready_exactly_once() {
        let _guard = reset();
        publish("SONGS/READY.XML", b"<song preview=\"00\"");
        let (status, buffer, len) = collect("SONGS/READY.XML");
        assert_eq!(status, DELUGE_FILE_PREFIX_READY);
        assert_eq!(len as usize, b"<song preview=\"00\"".len());
        assert_eq!(&buffer[..len as usize], b"<song preview=\"00\"");
        let (status, buffer, len) = collect("SONGS/READY.XML");
        assert_eq!(status, DELUGE_FILE_PREFIX_UNKNOWN);
        assert_untouched(&buffer, len);
    }

    /// An empty prefix is a real answer, not an absence: it is what a failed read publishes, and it
    /// must reach the caller as READY with a zero length rather than as UNKNOWN, which would have
    /// it re-request the unreadable file on every tick forever.
    #[test]
    fn an_empty_prefix_collects_ready_with_zero_length() {
        let _guard = reset();
        publish("SONGS/EMPTY.XML", b"");
        let (status, _, len) = collect("SONGS/EMPTY.XML");
        assert_eq!(status, DELUGE_FILE_PREFIX_READY);
        assert_eq!(len, 0);
    }

    /// Single flight, end to end: a second request replaces the first, so the abandoned path is
    /// UNKNOWN — "request it (again)" — rather than sitting PENDING for a read nobody will do.
    #[test]
    fn a_second_request_supersedes_the_first() {
        let _guard = reset();
        request("SONGS/FIRST.XML", 4096);
        request("SONGS/SECOND.XML", 4096);
        assert_eq!(collect("SONGS/SECOND.XML").0, DELUGE_FILE_PREFIX_PENDING);
        assert_eq!(collect("SONGS/FIRST.XML").0, DELUGE_FILE_PREFIX_UNKNOWN);
    }

    /// `deluge_card_flush` covers this channel along with every other loader, in one call: a prefix
    /// published before the card went is uncollectable after it.
    ///
    /// **This does not pin the take-time belt, and must not be read as if it did.** With nothing
    /// contending the slot the flush's own `try_lock` clear succeeds, so this passes even against a
    /// `take_from` that ignores the stamp entirely (verified by re-running it under exactly that
    /// neutering). What it pins is the wiring — that `card.rs` reaches THIS channel at all. The
    /// take-time property is pinned by the two unit tests above, which are the ones that fail when
    /// the stamp comparison is removed.
    #[test]
    fn a_prefix_published_before_an_ejection_is_not_collectable_after_it() {
        let _guard = reset();
        publish("SONGS/EJECTED.XML", b"card A");
        card::deluge_card_flush();
        let (status, buffer, len) = collect("SONGS/EJECTED.XML");
        assert_eq!(status, DELUGE_FILE_PREFIX_UNKNOWN);
        assert_untouched(&buffer, len);
    }

    /// The half a cleared table cannot cover: a read already in flight when the card went publishes
    /// AFTERWARDS, stamped with the generation it began under, and is uncollectable.
    ///
    /// **What refuses it here is `publish_result`'s early-drop check, not the take** — that check
    /// calls itself "an optimisation, not the guarantee", and this test passes against a
    /// stamp-ignoring `take_from` (verified). It is worth keeping for the drop it does pin, but the
    /// guarantee is the take, and only the unit tests above hold that.
    #[test]
    fn a_read_in_flight_across_an_ejection_publishes_uncollectably() {
        let _guard = reset();
        let began_under = deluge_card_generation::current();
        card::deluge_card_flush(); // the card goes while the read is running
        block_on(publish_result("SONGS/INFLIGHT.XML", b"card A", began_under));
        assert_eq!(collect("SONGS/INFLIGHT.XML").0, DELUGE_FILE_PREFIX_UNKNOWN);
    }
}
