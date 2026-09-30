//! Host tests for the prefix channel: the slot's keying and card-generation stamp, the driver's
//! bounds and handle discipline, and the C ABI's request/collect contract.

use std::cell::{Cell, RefCell};

use deluge_bsp_rust::channel::audio_loader;

use deluge_bsp_rust::channel::file_prefix;

use audio_loader::{LoadOps, OpenVerdict};
use file_prefix::{
    PREFIX_CAPACITY, PrefixSlot, PrefixTake, prefix_io_once, publish_failure_into, publish_into,
    take_from,
};

/// Nonzero, so a publish that dropped its stamp (leaving 0) cannot still satisfy every take.
const CARD: u32 = 3;

#[test]
fn a_published_prefix_is_taken_exactly_once() {
    let mut slot = PrefixSlot::default();
    let mut store = [0u8; PREFIX_CAPACITY];
    publish_into(
        &mut slot,
        &mut store,
        "SONGS/A.XML",
        b"<song preview=\"00\"",
        CARD,
    );
    let mut out = [0u8; 64];
    let PrefixTake::Bytes(n) =
        take_from(&mut slot, &store, "SONGS/A.XML", CARD, &mut out).expect("published")
    else {
        panic!("a published prefix must take as bytes, never as a failure");
    };
    assert_eq!(&out[..n], b"<song preview=\"00\"");
    assert!(take_from(&mut slot, &store, "SONGS/A.XML", CARD, &mut out).is_none());
}

#[test]
fn a_prefix_for_another_path_is_absent() {
    let mut slot = PrefixSlot::default();
    let mut store = [0u8; PREFIX_CAPACITY];
    publish_into(&mut slot, &mut store, "SONGS/A.XML", b"aaa", CARD);
    let mut out = [0u8; 64];
    assert!(take_from(&mut slot, &store, "SONGS/B.XML", CARD, &mut out).is_none());
}

/// A stale prefix is removed at the take, not merely withheld.
#[test]
fn a_prefix_from_a_previous_card_is_dropped_not_returned() {
    let mut slot = PrefixSlot::default();
    let mut store = [0u8; PREFIX_CAPACITY];
    publish_into(&mut slot, &mut store, "SONGS/A.XML", b"old card", CARD);
    let mut out = [0u8; 64];
    assert!(take_from(&mut slot, &store, "SONGS/A.XML", CARD + 1, &mut out).is_none());
    assert!(
        slot.is_empty(),
        "the stale prefix must be removed, not merely withheld"
    );
}

/// A publish stamped before a flush's bump can land after it; the take refuses it.
#[test]
fn a_prefix_stamped_before_a_flush_cannot_be_taken_after_it() {
    let mut slot = PrefixSlot::default();
    let mut store = [0u8; PREFIX_CAPACITY];
    publish_into(&mut slot, &mut store, "SONGS/A.XML", b"landed late", CARD); // stamped pre-bump
    let mut out = [0u8; 64];
    assert!(take_from(&mut slot, &store, "SONGS/A.XML", CARD + 1, &mut out).is_none());
    assert!(slot.is_empty());
}

/// A buffer smaller than the prefix is a caller bug, and terminal: the entry is consumed and an
/// empty prefix reported. Leaving it resident would make the caller re-request and re-read the card
/// every tick.
#[test]
fn a_too_small_out_buffer_consumes_the_prefix_and_takes_nothing() {
    let mut slot = PrefixSlot::default();
    let mut store = [0u8; PREFIX_CAPACITY];
    publish_into(&mut slot, &mut store, "SONGS/A.XML", b"0123456789", CARD);
    let mut out = [0u8; 4];
    assert_eq!(
        take_from(&mut slot, &store, "SONGS/A.XML", CARD, &mut out),
        Some(PrefixTake::Bytes(0))
    );
    assert!(
        slot.is_empty(),
        "a short buffer must not leave a prefix the caller would re-request forever"
    );
    assert_eq!(
        out, [0u8; 4],
        "nothing may be written into a buffer that could not hold the prefix"
    );
}

/// The generation is checked before the buffer size: a stale entry meeting a short buffer must
/// report `None` (re-request), not `Bytes(0)`, which the caller would accept as blank artwork for
/// the current card.
#[test]
fn a_stale_prefix_meeting_a_short_buffer_is_refused_not_blanked() {
    let mut slot = PrefixSlot::default();
    let mut store = [0u8; PREFIX_CAPACITY];
    publish_into(&mut slot, &mut store, "SONGS/A.XML", b"0123456789", CARD);
    let mut out = [0u8; 4];
    assert!(take_from(&mut slot, &store, "SONGS/A.XML", CARD + 1, &mut out).is_none());
    assert!(slot.is_empty());
}

/// A failed read is an answer, taken once as a failure; absence would read as `UNKNOWN` and be
/// re-read every tick.
#[test]
fn a_published_failure_is_taken_as_a_failure_exactly_once() {
    let mut slot = PrefixSlot::default();
    let store = [0u8; PREFIX_CAPACITY];
    publish_failure_into(&mut slot, "SONGS/A.XML", CARD);
    let mut out = [0xEEu8; 64];
    assert_eq!(
        take_from(&mut slot, &store, "SONGS/A.XML", CARD, &mut out),
        Some(PrefixTake::Failed)
    );
    assert_eq!(
        out, [0xEEu8; 64],
        "a failure writes nothing into the taker's buffer"
    );
    assert!(slot.is_empty());
    assert!(take_from(&mut slot, &store, "SONGS/A.XML", CARD, &mut out).is_none());
}

/// An empty file and a failed read are different answers; both have zero bytes, so only the
/// variant tells them apart.
#[test]
fn an_empty_file_and_a_failed_read_are_different_answers() {
    let mut empty = PrefixSlot::default();
    let mut empty_store = [0u8; PREFIX_CAPACITY];
    publish_into(&mut empty, &mut empty_store, "SONGS/A.XML", b"", CARD);
    let mut failed = PrefixSlot::default();
    let failed_store = [0u8; PREFIX_CAPACITY];
    publish_failure_into(&mut failed, "SONGS/A.XML", CARD);

    let mut out = [0u8; 64];
    assert_eq!(
        take_from(&mut empty, &empty_store, "SONGS/A.XML", CARD, &mut out),
        Some(PrefixTake::Bytes(0))
    );
    assert_eq!(
        take_from(&mut failed, &failed_store, "SONGS/A.XML", CARD, &mut out),
        Some(PrefixTake::Failed)
    );
}

/// A stale failure is refused too: served after a swap it would show an error for a file on the
/// new card.
#[test]
fn a_failure_from_a_previous_card_is_dropped_not_returned() {
    let mut slot = PrefixSlot::default();
    let store = [0u8; PREFIX_CAPACITY];
    publish_failure_into(&mut slot, "SONGS/A.XML", CARD);
    let mut out = [0u8; 64];
    assert!(take_from(&mut slot, &store, "SONGS/A.XML", CARD + 1, &mut out).is_none());
    assert!(
        slot.is_empty(),
        "a stale failure must be removed, not merely withheld"
    );
}

/// The slot must hold the largest consumer's read, a DX7 32-voice bank (4104 bytes, which a 4096
/// slot would truncate and fail its checksum).
#[test]
fn the_slot_holds_a_dx7_bank() {
    /// 6 header bytes + 32 voices x 128 bytes + checksum + EOX.
    const DX7_32_VOICE_BANK: usize = 4104;
    assert!(
        PREFIX_CAPACITY >= DX7_32_VOICE_BANK,
        "a DX7 32-voice bank is {DX7_32_VOICE_BANK} bytes and must survive the slot whole"
    );
    let mut slot = PrefixSlot::default();
    let mut store = [0u8; PREFIX_CAPACITY];
    let bank = vec![0xA5u8; DX7_32_VOICE_BANK];
    publish_into(&mut slot, &mut store, "DX7/BANK.SYX", &bank, CARD);
    let mut out = [0u8; PREFIX_CAPACITY];
    assert_eq!(
        take_from(&mut slot, &store, "DX7/BANK.SYX", CARD, &mut out),
        Some(PrefixTake::Bytes(DX7_32_VOICE_BANK)),
        "the whole bank, not a truncation"
    );
    assert_eq!(&out[..DX7_32_VOICE_BANK], &bank[..]);
}

/// A publish writes its bytes into the store (the SDRAM buffer on device), not the slot.
#[test]
fn a_publish_writes_its_bytes_into_the_store() {
    let mut slot = PrefixSlot::default();
    let mut store = [0u8; PREFIX_CAPACITY];
    publish_into(&mut slot, &mut store, "SONGS/A.XML", b"<song preview", CARD);
    assert_eq!(&store[..13], b"<song preview");
}

/// A take copies from the store it is given, not from anything the slot kept.
#[test]
fn a_take_copies_its_bytes_out_of_the_store() {
    let mut slot = PrefixSlot::default();
    let mut store = [0u8; PREFIX_CAPACITY];
    publish_into(&mut slot, &mut store, "SONGS/A.XML", b"abc", CARD);
    store[..3].copy_from_slice(b"xyz");
    let mut out = [0u8; 64];
    assert_eq!(
        take_from(&mut slot, &store, "SONGS/A.XML", CARD, &mut out),
        Some(PrefixTake::Bytes(3))
    );
    assert_eq!(&out[..3], b"xyz");
}

// The driver: the read is bounded by both the file size and the caller's ceiling, the handle is
// closed on every path, and a failure is distinct from an empty file.

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
/// Tests assert `open_handles` (the live count) is zero at the end: a leaked efatfs handle is
/// silent capacity loss. Every operation resolves its handle, so a driver using a stale one panics
/// here.
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

    /// Asserts no handle is left open.
    fn assert_settled(&self) {
        assert_eq!(
            self.open_handles.get(),
            0,
            "efatfs handle leaked: {:?}",
            self.calls()
        );
    }

    /// The path and bytes behind a live handle. Panics if `handle` was never issued or is closed.
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
        // Handles are never reused, so a stale one cannot land on a live entry.
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
            return true; // An empty fill trivially succeeds, as in production.
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

/// The ceiling, not the file size, bounds a long file's read.
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

/// An empty file reads as a zero-length prefix, not a failure.
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

/// A full handle table is reported as a failed read; this channel has no transient failure.
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

/// A size failure and a read failure both still close the handle.
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

// The C ABI over the process-wide statics.

use deluge_bsp_rust::storage::card;

mod live_pipeline {
    use super::file_prefix::{
        PREFIX_CAPACITY, PrefixPath, deluge_file_prefix_collect, deluge_file_prefix_request,
        mark_in_flight, publish_result,
    };
    use super::{block_on, card};
    use deluge_bsp_rust::channel::{
        DELUGE_CHANNEL_FAILED, DELUGE_CHANNEL_PENDING, DELUGE_CHANNEL_READY, DELUGE_CHANNEL_UNKNOWN,
    };
    use std::ffi::CString;

    /// The statics are process-wide and tests run on parallel threads, so tests take this lock.
    static PIPELINE_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

    /// What [`collect`] seeds its outputs with; neither may be written unless the status is READY.
    const UNTOUCHED: u8 = 0xFF;
    const UNTOUCHED_LEN: u32 = 0xDEAD_BEEF;

    /// Takes the pipeline lock and flushes the channel, whatever the previous test left.
    fn reset() -> std::sync::MutexGuard<'static, ()> {
        let guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        card::deluge_card_flush();
        guard
    }

    /// Collects into a full-capacity seeded buffer, returning the status, the bytes and the
    /// reported length.
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

    /// Asserts the outputs are still as [`collect`] seeded them: the "written only on READY" half
    /// of `file_prefix.h`'s contract.
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

    /// Publishes as the task would for a read that began under the current card generation.
    fn publish(path: &str, bytes: &[u8]) {
        block_on(publish_result(
            path,
            Some(bytes),
            deluge_bsp_rust::storage::card::generation(),
        ));
    }

    /// Publishes as the task would for a failed read.
    fn publish_failure(path: &str) {
        block_on(publish_result(
            path,
            None,
            deluge_bsp_rust::storage::card::generation(),
        ));
    }

    /// A path never asked about is `UNKNOWN`; `PENDING` would make the caller wait forever.
    #[test]
    fn an_unrequested_path_is_unknown() {
        let _guard = reset();
        let (status, buffer, len) = collect("never/asked.XML");
        assert_eq!(status, DELUGE_CHANNEL_UNKNOWN);
        assert_untouched(&buffer, len);
    }

    #[test]
    fn a_requested_path_is_pending() {
        let _guard = reset();
        request("SONGS/PENDING.XML", 4096);
        let (status, buffer, len) = collect("SONGS/PENDING.XML");
        assert_eq!(status, DELUGE_CHANNEL_PENDING);
        assert_untouched(&buffer, len);
    }

    /// A read running now reports `PENDING`, not `UNKNOWN` (which would make the caller re-request
    /// it every tick). No test runs the task, so `mark_in_flight` is driven directly.
    #[test]
    fn an_in_flight_read_is_pending_not_unknown() {
        let _guard = reset();
        block_on(mark_in_flight(Some(
            PrefixPath::try_from("SONGS/INFLIGHT.XML").unwrap(),
        )));
        assert_eq!(collect("SONGS/INFLIGHT.XML").0, DELUGE_CHANNEL_PENDING);
        // Another path is still unknown: the branch matches on the path.
        assert_eq!(collect("SONGS/OTHER.XML").0, DELUGE_CHANNEL_UNKNOWN);
        block_on(mark_in_flight(None));
        assert_eq!(collect("SONGS/INFLIGHT.XML").0, DELUGE_CHANNEL_UNKNOWN);
    }

    #[test]
    fn a_published_prefix_collects_ready_exactly_once() {
        let _guard = reset();
        publish("SONGS/READY.XML", b"<song preview=\"00\"");
        let (status, buffer, len) = collect("SONGS/READY.XML");
        assert_eq!(status, DELUGE_CHANNEL_READY);
        assert_eq!(len as usize, b"<song preview=\"00\"".len());
        assert_eq!(&buffer[..len as usize], b"<song preview=\"00\"");
        let (status, buffer, len) = collect("SONGS/READY.XML");
        assert_eq!(status, DELUGE_CHANNEL_UNKNOWN);
        assert_untouched(&buffer, len);
    }

    /// An empty prefix (an empty file) collects READY with zero length, not `UNKNOWN`, which would
    /// make the caller re-request it every tick.
    #[test]
    fn an_empty_prefix_collects_ready_with_zero_length() {
        let _guard = reset();
        publish("SONGS/EMPTY.XML", b"");
        let (status, _, len) = collect("SONGS/EMPTY.XML");
        assert_eq!(status, DELUGE_CHANNEL_READY);
        assert_eq!(len, 0);
    }

    /// A failed read collects FAILED once, writing neither output, and is `UNKNOWN` afterwards.
    #[test]
    fn a_failed_read_collects_failed_exactly_once() {
        let _guard = reset();
        publish_failure("SONGS/BROKEN.XML");
        let (status, buffer, len) = collect("SONGS/BROKEN.XML");
        assert_eq!(status, DELUGE_CHANNEL_FAILED);
        assert_untouched(&buffer, len);
        // Terminal, like READY: the answer is consumed.
        let (status, buffer, len) = collect("SONGS/BROKEN.XML");
        assert_eq!(status, DELUGE_CHANNEL_UNKNOWN);
        assert_untouched(&buffer, len);
    }

    /// FAILED and an empty READY are distinct statuses; the two tests above would each still pass
    /// if both collapsed onto one status.
    #[test]
    fn an_empty_file_and_a_failed_read_collect_differently() {
        let _guard = reset();
        publish("SONGS/A.XML", b"");
        let empty = collect("SONGS/A.XML").0;
        publish_failure("SONGS/A.XML");
        let failed = collect("SONGS/A.XML").0;
        assert_ne!(
            empty, failed,
            "an empty file and a failed read must not report the same status"
        );
        assert_eq!(empty, DELUGE_CHANNEL_READY);
        assert_eq!(failed, DELUGE_CHANNEL_FAILED);
    }

    /// A failure published before the card went is not served after it.
    #[test]
    fn a_failure_published_before_an_ejection_is_not_collectable_after_it() {
        let _guard = reset();
        publish_failure("SONGS/EJECTED.XML");
        card::deluge_card_flush();
        let (status, buffer, len) = collect("SONGS/EJECTED.XML");
        assert_eq!(status, DELUGE_CHANNEL_UNKNOWN);
        assert_untouched(&buffer, len);
    }

    /// A second request replaces the first, so the abandoned path is `UNKNOWN`, not stuck
    /// `PENDING`.
    #[test]
    fn a_second_request_supersedes_the_first() {
        let _guard = reset();
        request("SONGS/FIRST.XML", 4096);
        request("SONGS/SECOND.XML", 4096);
        assert_eq!(collect("SONGS/SECOND.XML").0, DELUGE_CHANNEL_PENDING);
        assert_eq!(collect("SONGS/FIRST.XML").0, DELUGE_CHANNEL_UNKNOWN);
    }

    /// `deluge_card_flush` reaches this channel: a prefix published before the card went is
    /// uncollectable after it.
    ///
    /// This pins the wiring, not the take-time stamp check: uncontended, the flush's own clear
    /// succeeds, so this passes even if `take_from` ignores the stamp. The unit tests above pin
    /// that.
    #[test]
    fn a_prefix_published_before_an_ejection_is_not_collectable_after_it() {
        let _guard = reset();
        publish("SONGS/EJECTED.XML", b"card A");
        card::deluge_card_flush();
        let (status, buffer, len) = collect("SONGS/EJECTED.XML");
        assert_eq!(status, DELUGE_CHANNEL_UNKNOWN);
        assert_untouched(&buffer, len);
    }

    /// A read in flight when the card went publishes afterwards, stamped with its old generation,
    /// and is uncollectable.
    ///
    /// What refuses it here is `publish_result`'s early drop, not the take; the unit tests above
    /// pin the take.
    #[test]
    fn a_read_in_flight_across_an_ejection_publishes_uncollectably() {
        let _guard = reset();
        let began_under = deluge_bsp_rust::storage::card::generation();
        card::deluge_card_flush(); // the card goes while the read is running
        block_on(publish_result(
            "SONGS/INFLIGHT.XML",
            Some(b"card A"),
            began_under,
        ));
        assert_eq!(collect("SONGS/INFLIGHT.XML").0, DELUGE_CHANNEL_UNKNOWN);
    }
}
