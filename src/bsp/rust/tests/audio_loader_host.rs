//! Host unit tests for `audio_loader` — the awaited-I/O half of a card sample's
//! load, plus the result table that carries its output to the C++ collector.
//!
//! See `src/audio_loader.rs`'s module doc for why the halves are split at all
//! (each executor forbids what the other requires), and why [`LoadOps`] is only
//! four async filesystem operations with no route into C++.
//!
//! `deluge-bsp-rust` is bin-only (no `[lib]` target — see `Cargo.toml`), so this
//! recompiles `src/audio_loader.rs` unmodified into this test binary via
//! `#[path]`, the same convention `tests/streaming_fill_host.rs` and
//! `tests/owner_host.rs` use. Only the `async_audio_load`-gated driver and the
//! always-compiled request/collect tier are needed — the `prod` submodule (the
//! efatfs backing) is gated on device/`host_app` and stays uncompiled, so this
//! test never needs the C++ app linked.
//!
//! What these tests pin, in one line each: the efatfs handle is closed on every
//! path; a parse verdict always reaches the caller with the error identity the
//! synchronous path produced; a zero-length window is still read and fed; and an
//! uncollected result is evicted rather than leaked.
#![cfg(feature = "async_audio_load")]
#![cfg(not(target_os = "none"))]

use std::cell::{Cell, RefCell};
use std::ffi::CString;

#[path = "../src/audio_loader.rs"]
mod audio_loader;

use audio_loader::{
    DELUGE_AUDIO_LOAD_PENDING, DELUGE_AUDIO_LOAD_READY, DELUGE_AUDIO_LOAD_UNKNOWN,
    DELUGE_ERROR_FILE_CORRUPTED, DELUGE_ERROR_FILE_UNREADABLE, DELUGE_ERROR_FILE_UNSUPPORTED,
    DELUGE_ERROR_NONE, DELUGE_MAX_PATH, LoadGeometry, LoadOps, LoadRequest, LoadResult,
    decode_candidates, deluge_audio_load_async_active, deluge_audio_load_collect,
    deluge_audio_load_request, load_io_once, publish_result,
};

/// One call `FakeOps` observed, in order.
#[derive(Debug, PartialEq, Eq, Clone)]
enum Call {
    Open(String),
    SizeOf,
    Read { offset: u32, len: usize },
    Close,
}

/// Counting double for [`LoadOps`].
///
/// `open_handles` is the LIVE count: incremented by `open`, decremented by
/// `close`, and asserted zero at the end of every test because the efatfs handle
/// table has only `MAX_HANDLES` (128) slots — a leaked handle is silent capacity
/// loss, not an error anything reports.
struct FakeOps {
    /// The file's bytes, served from memory, keyed by path.
    files: Vec<(String, Vec<u8>)>,
    /// `size_of` fails.
    size_fails: bool,
    /// `read_at` fails from this call onwards (a card pulled mid-parse).
    fail_read_after: Option<u32>,

    calls: RefCell<Vec<Call>>,
    open_handles: Cell<i32>,
    next_handle: Cell<u32>,
    reads_done: Cell<u32>,
    /// The bytes of whichever file is currently open. The fake only ever holds
    /// one handle at a time, which is exactly what `load_io_once` does.
    opened: RefCell<Vec<u8>>,
}

impl FakeOps {
    fn new() -> Self {
        Self {
            files: Vec::new(),
            size_fails: false,
            fail_read_after: None,
            calls: RefCell::new(Vec::new()),
            open_handles: Cell::new(0),
            next_handle: Cell::new(7),
            reads_done: Cell::new(0),
            opened: RefCell::new(Vec::new()),
        }
    }

    fn with_file(mut self, path: &str, bytes: Vec<u8>) -> Self {
        self.files.push((String::from(path), bytes));
        self
    }

    fn with_size_failing(mut self) -> Self {
        self.size_fails = true;
        self
    }

    fn with_read_failing_after(mut self, reads: u32) -> Self {
        self.fail_read_after = Some(reads);
        self
    }

    fn open_handles(&self) -> i32 {
        self.open_handles.get()
    }

    fn calls(&self) -> Vec<Call> {
        self.calls.borrow().clone()
    }

    fn record(&self, call: Call) {
        self.calls.borrow_mut().push(call);
    }

    /// Assert the invariant that holds at the end of EVERY load: no handle left
    /// open.
    fn assert_settled(&self) {
        assert_eq!(
            self.open_handles(),
            0,
            "efatfs handle leaked: {:?}",
            self.calls()
        );
    }

    /// Every window the driver asked the file for, in order.
    fn reads(&self) -> Vec<(u32, usize)> {
        self.calls()
            .into_iter()
            .filter_map(|c| match c {
                Call::Read { offset, len } => Some((offset, len)),
                _ => None,
            })
            .collect()
    }
}

impl LoadOps for FakeOps {
    async fn open(&self, path: &str) -> Option<u32> {
        self.record(Call::Open(String::from(path)));
        let bytes = self
            .files
            .iter()
            .find(|(candidate, _)| candidate == path)
            .map(|(_, bytes)| bytes.clone())?;
        *self.opened.borrow_mut() = bytes;
        let handle = self.next_handle.get();
        self.next_handle.set(handle + 1);
        self.open_handles.set(self.open_handles.get() + 1);
        Some(handle)
    }

    async fn size_of(&self, _handle: u32) -> Option<u32> {
        self.record(Call::SizeOf);
        if self.size_fails {
            return None;
        }
        Some(self.opened.borrow().len() as u32)
    }

    async fn read_at(&self, _handle: u32, offset: u32, buf: &mut [u8]) -> bool {
        self.record(Call::Read {
            offset,
            len: buf.len(),
        });
        let done = self.reads_done.get();
        self.reads_done.set(done + 1);
        if self.fail_read_after.is_some_and(|limit| done >= limit) {
            return false;
        }
        // Mirrors `efatfs_fs::read_at`: an empty fill trivially succeeds, whatever
        // the offset. This is the case the driver MUST still feed.
        if buf.is_empty() {
            return true;
        }
        let bytes = self.opened.borrow();
        let start = offset as usize;
        let Some(end) = start.checked_add(buf.len()) else {
            return false;
        };
        if end > bytes.len() {
            return false; // A real read failure: the parser clamps inside the file.
        }
        buf.copy_from_slice(&bytes[start..end]);
        true
    }

    async fn close(&self, _handle: u32) {
        self.record(Call::Close);
        self.open_handles.set(self.open_handles.get() - 1);
    }
}

/// Smallest RIFF/WAVE that parses: the 44-byte canonical header (12-byte RIFF
/// container + 24-byte `fmt ` chunk + 8-byte `data` chunk header), 16-bit mono
/// 44100, then 4 frames of audio.
fn minimal_wav() -> Vec<u8> {
    let audio: [u8; 8] = [0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00];
    let mut wav = Vec::new();
    wav.extend_from_slice(b"RIFF");
    wav.extend_from_slice(&(36u32 + audio.len() as u32).to_le_bytes());
    wav.extend_from_slice(b"WAVE");
    wav.extend_from_slice(b"fmt ");
    wav.extend_from_slice(&16u32.to_le_bytes());
    wav.extend_from_slice(&1u16.to_le_bytes()); // PCM
    wav.extend_from_slice(&1u16.to_le_bytes()); // mono
    wav.extend_from_slice(&44100u32.to_le_bytes());
    wav.extend_from_slice(&(44100u32 * 2).to_le_bytes()); // byte rate
    wav.extend_from_slice(&2u16.to_le_bytes()); // block align
    wav.extend_from_slice(&16u16.to_le_bytes()); // bits per sample
    wav.extend_from_slice(b"data");
    wav.extend_from_slice(&(audio.len() as u32).to_le_bytes());
    wav.extend_from_slice(&audio);
    assert_eq!(wav.len(), 44 + audio.len());
    wav
}

/// Run a future to completion on the test thread — the same helper
/// `tests/streaming_fill_host.rs` uses, rather than a second executor dependency.
fn block_on<F: core::future::Future>(future: F) -> F::Output {
    embassy_futures::block_on(future)
}

fn request(path: &str, candidates: &[&str]) -> LoadRequest {
    LoadRequest {
        path: String::from(path),
        candidates: candidates.iter().map(|c| String::from(*c)).collect(),
    }
}

/// The alternate path a result reports, as a `String`.
fn alternate_of(result: &LoadResult) -> String {
    let end = result
        .alternate_path
        .iter()
        .position(|b| *b == 0)
        .expect("NUL-terminated");
    String::from_utf8(result.alternate_path[..end].to_vec()).expect("UTF-8")
}

// ── The happy path ──────────────────────────────────────────────────────────

/// A good file parses to a geometry, a size, and no error — and closes its handle.
#[test]
fn a_good_file_parses_to_a_geometry() {
    let ops = FakeOps::new().with_file("A.wav", minimal_wav());
    let result = block_on(load_io_once(&ops, &request("A.wav", &["A.wav"])));
    assert_eq!(result.error, DELUGE_ERROR_NONE);
    assert_eq!(result.size_bytes, 52);
    ops.assert_settled();
}

/// The geometry crossing the ABI is the parsed header, field for field. A wrong
/// or missed field here loads without any error and renders wrong.
#[test]
fn the_parsed_geometry_matches_the_header() {
    let ops = FakeOps::new().with_file("A.wav", minimal_wav());
    let result = block_on(load_io_once(&ops, &request("A.wav", &["A.wav"])));
    let geometry: LoadGeometry = result.geometry;
    assert_eq!(geometry.num_channels, 1);
    assert_eq!(geometry.byte_depth, 2);
    assert_eq!(geometry.sample_rate, 44100);
    assert_eq!(geometry.audio_data_start_pos_bytes, 44);
    assert_eq!(geometry.audio_data_length_bytes, 8);
    assert_eq!(geometry.file_loop_start_samples, 0);
    assert_eq!(geometry.file_loop_end_samples, 0);
    // `RawDataFormat::Native` — a 16-bit little-endian WAV needs no conversion.
    assert_eq!(geometry.raw_data_format, 0);
    // The file declares no root note and no `clm ` tag.
    assert_eq!(geometry.midi_note_present, 0);
    assert_eq!(geometry.wavetable_explicitly_specifies_self, 0);
    // The parser's "the file said nothing" default, mirroring the C++ parser's
    // Serum convention.
    assert_eq!(geometry.wavetable_cycle_size, 2048);
}

// ── The handle is released on every path ────────────────────────────────────

/// Nothing opens: no handle was taken, and the verdict is `FILE_UNREADABLE` —
/// there is always a result for the collector, never silence.
#[test]
fn no_candidate_opening_reports_unreadable() {
    let ops = FakeOps::new();
    let result = block_on(load_io_once(
        &ops,
        &request("A.wav", &["A.wav", "ALT/A.wav"]),
    ));
    assert_eq!(result.error, DELUGE_ERROR_FILE_UNREADABLE);
    assert_eq!(ops.open_handles(), 0);
    // Both candidates were tried, in order.
    assert_eq!(
        ops.calls(),
        vec![
            Call::Open(String::from("A.wav")),
            Call::Open(String::from("ALT/A.wav")),
        ]
    );
}

/// A `size_of` failure still closes the handle the successful open took.
#[test]
fn a_failed_size_closes_the_handle() {
    let ops = FakeOps::new()
        .with_file("A.wav", minimal_wav())
        .with_size_failing();
    let result = block_on(load_io_once(&ops, &request("A.wav", &["A.wav"])));
    assert_eq!(result.error, DELUGE_ERROR_FILE_UNREADABLE);
    ops.assert_settled();
    assert!(ops.calls().contains(&Call::Close));
}

/// A read that fails mid-parse (a card pulled) still closes the handle and still
/// produces a verdict.
#[test]
fn a_failed_read_mid_parse_closes_the_handle() {
    let ops = FakeOps::new()
        .with_file("A.wav", minimal_wav())
        .with_read_failing_after(1);
    let result = block_on(load_io_once(&ops, &request("A.wav", &["A.wav"])));
    assert_eq!(result.error, DELUGE_ERROR_FILE_UNREADABLE);
    ops.assert_settled();
}

// ── Parse verdicts keep the C++ side's error identity ───────────────────────

/// Not audio at all: `FILE_UNSUPPORTED`, the same code the synchronous path
/// produced, which is what `sample_browser.cpp` reads as "not a wavetable, try it
/// as a sample".
#[test]
fn a_non_audio_file_reports_file_unsupported() {
    let ops = FakeOps::new().with_file("A.wav", b"NOTAUDIOATALLREALLY!".to_vec());
    let result = block_on(load_io_once(&ops, &request("A.wav", &["A.wav"])));
    assert_eq!(result.error, DELUGE_ERROR_FILE_UNSUPPORTED);
    ops.assert_settled();
}

/// A RIFF/WAVE container with no `data` chunk is malformed, not unsupported:
/// `FILE_CORRUPTED`.
#[test]
fn a_wav_without_audio_data_reports_file_corrupted() {
    let mut wav = minimal_wav();
    wav.truncate(36); // Drop the whole `data` chunk, keeping `fmt `.
    let riff_size = (wav.len() - 8) as u32;
    wav[4..8].copy_from_slice(&riff_size.to_le_bytes());
    let ops = FakeOps::new().with_file("A.wav", wav);
    let result = block_on(load_io_once(&ops, &request("A.wav", &["A.wav"])));
    assert_eq!(result.error, DELUGE_ERROR_FILE_CORRUPTED);
    ops.assert_settled();
}

// ── Candidate order and the alternate location ──────────────────────────────

/// Candidates are tried in order and the FIRST that opens wins. A winner that is
/// not the nominal path is reported as the alternate location — exactly
/// `usingAlternateLocation`, and the path the collector must bind its read stream
/// to.
#[test]
fn the_first_candidate_that_opens_becomes_the_alternate_location() {
    let ops = FakeOps::new().with_file("SAMPLES/SONG/A.wav", minimal_wav());
    let result = block_on(load_io_once(
        &ops,
        &request("SAMPLES/A.wav", &["SAMPLES/A.wav", "SAMPLES/SONG/A.wav"]),
    ));
    assert_eq!(result.error, DELUGE_ERROR_NONE);
    assert_eq!(alternate_of(&result), "SAMPLES/SONG/A.wav");
    ops.assert_settled();
}

/// The regular path winning means NO alternate location — an empty string, which
/// is what `deluge_audio_load_begin` documents for "resolved at `file_path`
/// directly" (and never NULL).
#[test]
fn the_regular_path_winning_reports_no_alternate_location() {
    let ops = FakeOps::new().with_file("SAMPLES/A.wav", minimal_wav());
    let result = block_on(load_io_once(
        &ops,
        &request("SAMPLES/A.wav", &["SAMPLES/A.wav"]),
    ));
    assert_eq!(alternate_of(&result), "");
}

// ── The window protocol ─────────────────────────────────────────────────────

/// Every window stays inside the size the parser was given
/// (`start + len <= size`), which is what makes [`LoadOps::read_at`]'s
/// full-fill-or-`false` contract usable: a short read can only be a real failure,
/// never an end of file that a byte count would have had to distinguish.
#[test]
fn every_window_stays_inside_the_file() {
    let bytes = minimal_wav();
    let ops = FakeOps::new().with_file("A.wav", bytes.clone());
    assert_eq!(
        block_on(load_io_once(&ops, &request("A.wav", &["A.wav"]))).error,
        DELUGE_ERROR_NONE
    );
    assert!(
        ops.reads()
            .iter()
            .all(|(offset, len)| *offset as usize + *len <= bytes.len()),
        "the parser must never name a byte past the size it was given; reads were {:?}",
        ops.reads()
    );
    // The audio payload is stepped over by arithmetic, never read: the walk stops
    // at the `data` chunk HEADER (offset 36, 8 bytes) and does not read the 8
    // audio bytes behind it.
    assert_eq!(ops.reads(), vec![(0, 12), (12, 8), (20, 16), (36, 8)]);
}

/// A zero-length window request must still be READ and FED. The parser resolves
/// the end of the file inside `feed` and never terminates without a `feed` call,
/// so a driver that treated `len == 0` as "nothing to do" and skipped it would
/// spin in the parse loop forever — this test would hang rather than fail.
///
/// A zero-byte file is the case that produces one: `pending` clamps the container
/// header request to what remains, which is nothing. In production a zero-byte
/// file is refused before a `Sample` is ever built, so this is defence in depth on
/// the driver's own loop rather than a reachable card state — which is precisely
/// why it needs a test: nothing else would notice if the loop stopped feeding.
#[test]
fn a_zero_length_window_is_still_read_and_fed() {
    let ops = FakeOps::new().with_file("A.wav", Vec::new());
    let result = block_on(load_io_once(&ops, &request("A.wav", &["A.wav"])));
    assert_eq!(
        ops.reads(),
        vec![(0, 0)],
        "the clamped-to-zero window must be read, then fed"
    );
    // Fed, so the parse reached a verdict rather than looping forever.
    assert_ne!(result.error, DELUGE_ERROR_NONE);
    ops.assert_settled();
}

// ── The result table: what carries a result between the halves ──────────────

/// `RESULTS` and `QUEUE` are process-wide statics, and cargo runs tests on
/// parallel threads. The tests below assert on table *capacity* (eviction), so
/// they must not interleave with each other's publishes. Distinct key prefixes
/// are not enough on their own — a concurrent publish still consumes a slot.
static TABLE_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

fn collect(path: &str) -> (u8, LoadResult) {
    let key = CString::new(path).unwrap();
    let mut out = LoadResult::failed(0xFF);
    // SAFETY: `key` is a valid NUL-terminated C string and `out` a live,
    // writable `LoadResult`.
    let status = unsafe { deluge_audio_load_collect(key.as_ptr(), &mut out) };
    (status, out)
}

fn publish(path: &str, error: u8) {
    block_on(publish_result(
        String::from(path),
        LoadResult::failed(error),
    ));
}

/// A published result is collected exactly once: `READY` with the payload, then
/// `UNKNOWN`, because collecting REMOVES it. A second collect returning the same
/// result would let a caller build the same Sample twice.
#[test]
fn a_result_is_collected_exactly_once() {
    let _guard = TABLE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    publish("once/A.wav", DELUGE_ERROR_FILE_CORRUPTED);
    let (status, result) = collect("once/A.wav");
    assert_eq!(status, DELUGE_AUDIO_LOAD_READY);
    assert_eq!(result.error, DELUGE_ERROR_FILE_CORRUPTED);
    assert_eq!(collect("once/A.wav").0, DELUGE_AUDIO_LOAD_UNKNOWN);
}

/// A path the loader has never been asked about is `UNKNOWN`, not `PENDING` —
/// `PENDING` would tell the caller to keep asking for something that will never
/// arrive.
#[test]
fn an_unrequested_path_is_unknown() {
    let _guard = TABLE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    assert_eq!(collect("never/asked.wav").0, DELUGE_AUDIO_LOAD_UNKNOWN);
}

/// A queued-but-unparsed request reports `PENDING`, so the caller waits instead
/// of re-requesting work already in the queue.
#[test]
fn a_queued_request_is_pending() {
    let _guard = TABLE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let path = CString::new("queued/A.wav").unwrap();
    let mut candidates = vec![0u8; DELUGE_MAX_PATH];
    candidates[..12].copy_from_slice(b"queued/A.wav");
    candidates[12] = 0;
    // SAFETY: both pointers are valid for the lengths the ABI documents.
    let accepted = unsafe {
        deluge_audio_load_request(
            path.as_ptr(),
            candidates.as_ptr().cast::<core::ffi::c_char>(),
            1,
        )
    };
    assert!(accepted);
    assert_eq!(collect("queued/A.wav").0, DELUGE_AUDIO_LOAD_PENDING);
}

/// Re-publishing the same key REPLACES its entry rather than adding a second, so
/// a coalescing caller (the browser re-requesting as the cursor settles) cannot
/// grow the table with duplicates — and the newest parse is the one collected.
#[test]
fn republishing_a_key_replaces_its_entry() {
    let _guard = TABLE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    publish("dup/A.wav", DELUGE_ERROR_FILE_CORRUPTED);
    publish("dup/A.wav", DELUGE_ERROR_FILE_UNSUPPORTED);
    let (status, result) = collect("dup/A.wav");
    assert_eq!(status, DELUGE_AUDIO_LOAD_READY);
    assert_eq!(result.error, DELUGE_ERROR_FILE_UNSUPPORTED, "latest wins");
    // Exactly one entry existed, so there is nothing left behind it.
    assert_eq!(collect("dup/A.wav").0, DELUGE_AUDIO_LOAD_UNKNOWN);
}

/// An uncollected result is EVICTED, oldest first, once the table fills — it is
/// not held forever and it is not leaked. That is safe only because `LoadResult`
/// owns nothing: eviction costs the I/O that produced it and nothing else, and the
/// caller sees `UNKNOWN`, whose documented meaning is "request it again".
#[test]
fn an_uncollected_result_is_evicted_rather_than_leaked() {
    let _guard = TABLE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    // Five results into four slots.
    for index in 0..5 {
        publish(&format!("evict/{index}.wav"), DELUGE_ERROR_FILE_CORRUPTED);
    }
    // The oldest was evicted...
    assert_eq!(collect("evict/0.wav").0, DELUGE_AUDIO_LOAD_UNKNOWN);
    // ...and the four newest all survived, in their own slots.
    for index in 1..5 {
        assert_eq!(
            collect(&format!("evict/{index}.wav")).0,
            DELUGE_AUDIO_LOAD_READY,
            "slot {index} should have survived"
        );
    }
}

// ── The candidate buffer decoder ────────────────────────────────────────────

/// Build the buffer `deluge_audio_load_candidates` would have written: one
/// NUL-terminated path per `DELUGE_MAX_PATH` slot, the rest left as the caller had
/// it.
fn candidate_buffer(paths: &[&str]) -> Vec<u8> {
    // 0xAA, not zero: a slot the C side never wrote must not decode as an empty
    // string.
    let mut buffer = vec![0xAAu8; paths.len() * DELUGE_MAX_PATH];
    for (slot, path) in paths.iter().enumerate() {
        let bytes = path.as_bytes();
        let base = slot * DELUGE_MAX_PATH;
        buffer[base..base + bytes.len()].copy_from_slice(bytes);
        buffer[base + bytes.len()] = 0;
    }
    buffer
}

/// Each slot decodes to its own NUL-terminated path, in order.
#[test]
fn candidates_decode_slot_by_slot() {
    let buffer = candidate_buffer(&["A.wav", "ALT/A.wav", "A2.wav"]);
    assert_eq!(
        decode_candidates(&buffer).unwrap(),
        vec!["A.wav", "ALT/A.wav", "A2.wav"]
    );
}

/// An unterminated slot is refused rather than read past: the C side always
/// NUL-terminates what it writes, so an unterminated slot means the buffer is not
/// what it claims to be.
#[test]
fn an_unterminated_candidate_slot_is_refused() {
    let buffer = vec![0xAAu8; DELUGE_MAX_PATH];
    assert!(decode_candidates(&buffer).is_none());
}

/// A request naming more candidates than the C side ever proposes reads only the
/// slots within that bound, never past the buffer the caller sized.
#[test]
fn a_request_reads_only_the_candidate_slots_it_was_given() {
    let _guard = TABLE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let buffer = candidate_buffer(&["bound/A.wav"]);
    let path = CString::new("bound/A.wav").unwrap();
    // SAFETY: the buffer really is 1 * DELUGE_MAX_PATH bytes, matching the count.
    let accepted = unsafe {
        deluge_audio_load_request(
            path.as_ptr(),
            buffer.as_ptr().cast::<core::ffi::c_char>(),
            1,
        )
    };
    assert!(accepted);
    assert_eq!(collect("bound/A.wav").0, DELUGE_AUDIO_LOAD_PENDING);
}

// ── The switch ──────────────────────────────────────────────────────────────

/// `deluge_audio_load_async_active()` must report false unless
/// `async_audio_load_active` was explicitly enabled — it must NOT follow
/// `async_audio_load`, which is on by default and only says the driver is
/// compiled. Running under default features, this test asserts the shipped
/// default is off; run with `--features async_audio_load_active` it asserts the
/// flip actually reaches the selector.
///
/// This replaces an `objdump` check on the linked image: with no C++ caller yet,
/// `--gc-sections` drops the symbol from the firmware entirely, so there is
/// nothing there to disassemble. A behavioural assertion is durable where that
/// was incidental.
#[test]
fn the_async_switch_follows_only_its_own_feature() {
    if cfg!(feature = "async_audio_load_active") {
        assert!(
            deluge_audio_load_async_active(),
            "the flip must reach the selector"
        );
    } else {
        assert!(
            !deluge_audio_load_async_active(),
            "async loading must be off unless async_audio_load_active is set, even though \
             async_audio_load (which only compiles the driver) is on by default"
        );
    }
}

/// Collecting from the MIDDLE of the table must leave the remaining entries in
/// their original order, because eviction is oldest-first and reads that order.
///
/// This is a regression test for a real bug in `remove_at`: it rotated the
/// surviving entries instead of restoring them, so after one mid-table collect
/// the "oldest" entry was no longer the oldest, and the next eviction discarded
/// the wrong result. Nothing else would have caught it — every other test here
/// collects from a single-entry table, where index 0 is correct either way.
///
/// A,B,C,D fill the table; collecting C must leave A,B,D in that order, so that
/// two further publishes evict A first and D still survives. The buggy rotation
/// left D,A,B and evicted D.
#[test]
fn collecting_from_the_middle_keeps_the_eviction_order() {
    let _guard = TABLE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    for key in ["ord/A", "ord/B", "ord/C", "ord/D"] {
        publish(key, DELUGE_ERROR_FILE_CORRUPTED);
    }
    assert_eq!(collect("ord/C").0, DELUGE_AUDIO_LOAD_READY);

    // Table now holds A,B,D. Two more publishes push it past four entries, so the
    // two OLDEST (A, then B) are the ones evicted.
    publish("ord/E", DELUGE_ERROR_FILE_CORRUPTED);
    publish("ord/F", DELUGE_ERROR_FILE_CORRUPTED);

    assert_eq!(
        collect("ord/A").0,
        DELUGE_AUDIO_LOAD_UNKNOWN,
        "A was oldest"
    );
    assert_eq!(
        collect("ord/D").0,
        DELUGE_AUDIO_LOAD_READY,
        "D was newer than A and B, so it must survive"
    );
    assert_eq!(collect("ord/E").0, DELUGE_AUDIO_LOAD_READY);
    assert_eq!(collect("ord/F").0, DELUGE_AUDIO_LOAD_READY);
}
