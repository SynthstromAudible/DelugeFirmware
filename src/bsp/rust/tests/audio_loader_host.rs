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
#[cfg(feature = "async_audio_load_active")]
use std::ffi::CString;

#[path = "../src/audio_loader.rs"]
mod audio_loader;

use audio_loader::{
    CandidateList, DELUGE_ERROR_FILE_CORRUPTED, DELUGE_ERROR_FILE_UNREADABLE,
    DELUGE_ERROR_FILE_UNSUPPORTED, DELUGE_ERROR_NONE, DELUGE_MAX_PATH, LoadGeometry, LoadOps,
    LoadPath, LoadRequest, LoadResult, RESULT_SLOTS, ResultTable, decode_candidates,
    deluge_audio_load_async_active, load_io_once, publish_into, take_from,
};

// The live request/collect pipeline only exists where its storage does. See the
// `live_pipeline` module below.
#[cfg(feature = "async_audio_load_active")]
use audio_loader::{
    DELUGE_AUDIO_LOAD_PENDING, DELUGE_AUDIO_LOAD_READY, DELUGE_AUDIO_LOAD_UNKNOWN,
    deluge_audio_load_collect, deluge_audio_load_request, mark_in_flight, publish_result,
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
    /// Live handles: `handle -> the bytes that handle refers to`.
    ///
    /// Keyed by HANDLE, not "whatever was opened last", and that is the point.
    /// `efatfs_fs::size_of_handle` exists because the pre-existing size entry
    /// point reads a DIFFERENT handle table, so sizing through it would silently
    /// bound the parse to another file. A double that ignores the handle cannot
    /// observe that bug at all — it would serve the right bytes for the wrong
    /// handle. Every operation here therefore looks the handle up and panics if
    /// it was never issued or has been closed.
    live: RefCell<Vec<(u32, Vec<u8>)>>,
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
            live: RefCell::new(Vec::new()),
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

    /// The bytes behind a live handle. Panics if `handle` was never issued by
    /// `open`, or has already been closed — either is a real defect in the
    /// driver, not a test-harness detail.
    fn bytes_for(&self, handle: u32) -> Vec<u8> {
        let live = self.live.borrow();
        live.iter()
            .find(|(live_handle, _)| *live_handle == handle)
            .map(|(_, bytes)| bytes.clone())
            .unwrap_or_else(|| {
                panic!(
                    "operation on handle {handle}, which is not open (live: {:?})",
                    live.iter().map(|(h, _)| *h).collect::<Vec<_>>()
                )
            })
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
        // A fresh handle every time, never reused, so a driver that held on to a
        // stale one cannot accidentally land on a live entry.
        let handle = self.next_handle.get();
        self.next_handle.set(handle + 1);
        self.live.borrow_mut().push((handle, bytes));
        self.open_handles.set(self.open_handles.get() + 1);
        Some(handle)
    }

    async fn size_of(&self, handle: u32) -> Option<u32> {
        self.record(Call::SizeOf);
        let bytes = self.bytes_for(handle);
        if self.size_fails {
            return None;
        }
        Some(bytes.len() as u32)
    }

    async fn read_at(&self, handle: u32, offset: u32, buf: &mut [u8]) -> bool {
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
        // the offset. This is the case the driver MUST still feed. The handle is
        // still resolved first, so a zero-length read against a bad handle is
        // caught too.
        let bytes = self.bytes_for(handle);
        if buf.is_empty() {
            return true;
        }
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

    async fn close(&self, handle: u32) {
        self.record(Call::Close);
        let mut live = self.live.borrow_mut();
        let index = live
            .iter()
            .position(|(live_handle, _)| *live_handle == handle)
            .unwrap_or_else(|| panic!("close of handle {handle}, which is not open"));
        live.remove(index);
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

fn path_of(path: &str) -> LoadPath {
    LoadPath::try_from(path).expect("test path fits DELUGE_MAX_PATH")
}

fn request(path: &str, candidates: &[&str]) -> LoadRequest {
    let mut list = CandidateList::new();
    for candidate in candidates {
        list.push(path_of(candidate))
            .expect("candidate list capacity");
    }
    LoadRequest {
        path: path_of(path),
        candidates: list,
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

/// Two loads through one `FakeOps` must each report their OWN file's size, via
/// their own handle.
///
/// The double serves strictly by handle and issues a fresh one per open, so a
/// driver that cached a handle, a size, or "the last file opened" between loads
/// reports the first file's size for the second — which is the shape of the bug
/// `efatfs_fs::size_of_handle` exists to prevent (the pre-existing size entry
/// point reads a different handle table, so it would bound the parse to another
/// file entirely).
#[test]
fn each_load_is_sized_through_its_own_handle() {
    let mut small = minimal_wav();
    // A second `data` byte pair, so the two files differ in size and geometry.
    small.truncate(44 + 4);
    let riff_size = (small.len() - 8) as u32;
    small[4..8].copy_from_slice(&riff_size.to_le_bytes());
    small[40..44].copy_from_slice(&4u32.to_le_bytes());

    let ops = FakeOps::new()
        .with_file("big.wav", minimal_wav())
        .with_file("small.wav", small);

    let big = block_on(load_io_once(&ops, &request("big.wav", &["big.wav"])));
    assert_eq!(big.error, DELUGE_ERROR_NONE);
    assert_eq!(big.size_bytes, 52);
    assert_eq!(big.geometry.audio_data_length_bytes, 8);

    let small = block_on(load_io_once(&ops, &request("small.wav", &["small.wav"])));
    assert_eq!(small.error, DELUGE_ERROR_NONE);
    assert_eq!(
        small.size_bytes, 48,
        "the second load must be sized by its own handle"
    );
    assert_eq!(small.geometry.audio_data_length_bytes, 4);

    ops.assert_settled();
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

/// The order the candidates arrive in is the order they are tried, and the FIRST
/// one that opens wins. Both files exist here, which is what makes the claim
/// testable at all: with only one openable candidate, "first that opens" cannot
/// be told apart from "last", or from "any". The C++ side decides which copy of a
/// duplicated sample a load gets purely by the order it fills the buffer
/// (AudioFileManager::requestAsyncSampleLoad), so this is that contract.
#[test]
fn the_first_candidate_that_opens_wins_when_more_than_one_would() {
    let ops = FakeOps::new()
        .with_file("SAMPLES/A.wav", minimal_wav())
        .with_file("SAMPLES/ALT/A.wav", minimal_wav());

    let regular_first = block_on(load_io_once(
        &ops,
        &request("SAMPLES/A.wav", &["SAMPLES/A.wav", "SAMPLES/ALT/A.wav"]),
    ));
    assert_eq!(regular_first.error, DELUGE_ERROR_NONE);
    assert_eq!(alternate_of(&regular_first), "");

    // The same two candidates, the other way round: the winner follows the order,
    // not the contents.
    let alternate_first = block_on(load_io_once(
        &ops,
        &request("SAMPLES/A.wav", &["SAMPLES/ALT/A.wav", "SAMPLES/A.wav"]),
    ));
    assert_eq!(alternate_first.error, DELUGE_ERROR_NONE);
    assert_eq!(alternate_of(&alternate_first), "SAMPLES/ALT/A.wav");

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
//
// These run against a LOCAL `ResultTable`, not the process-wide `RESULTS`
// static, so they are compiled and run on every build — including the default one
// where that static does not exist at all (its storage now follows
// `async_audio_load_active`; see the module doc). That is deliberate: the
// `remove_at` ordering bug lived in exactly this code, and coverage that
// disappears with a feature flag is coverage that will not be there when it is
// next needed.

fn table_with(paths: &[&str]) -> ResultTable {
    let mut table = ResultTable::new();
    for path in paths {
        publish_into(
            &mut table,
            path_of(path),
            LoadResult::failed(DELUGE_ERROR_FILE_CORRUPTED),
        );
    }
    table
}

/// A published result is taken exactly once: the payload, then nothing, because
/// taking REMOVES it. Returning it twice would let a caller build the same
/// `Sample` twice.
#[test]
fn a_result_is_taken_exactly_once() {
    let mut table = table_with(&["once/A.wav"]);
    let taken = take_from(&mut table, "once/A.wav").expect("published");
    assert_eq!(taken.error, DELUGE_ERROR_FILE_CORRUPTED);
    assert!(take_from(&mut table, "once/A.wav").is_none());
}

/// A path that was never published is simply absent.
#[test]
fn an_unpublished_path_is_absent() {
    let mut table = table_with(&["other/A.wav"]);
    assert!(take_from(&mut table, "never/asked.wav").is_none());
}

/// Re-publishing the same key REPLACES its entry rather than adding a second, so
/// a coalescing caller (the browser re-requesting as the cursor settles) cannot
/// grow the table with duplicates — and the newest parse is the one taken.
///
/// Exercised from the MIDDLE of a populated table, not against a single entry:
/// the replace path rotates through `remove_at`, which is where the ordering bug
/// lived.
#[test]
fn republishing_a_key_replaces_its_entry_in_place() {
    let mut table = table_with(&["dup/A", "dup/B", "dup/C"]);
    publish_into(
        &mut table,
        path_of("dup/B"),
        LoadResult::failed(DELUGE_ERROR_FILE_UNSUPPORTED),
    );
    assert_eq!(table.len(), 3, "replace must not add an entry");
    assert_eq!(
        take_from(&mut table, "dup/B").unwrap().error,
        DELUGE_ERROR_FILE_UNSUPPORTED,
        "latest wins"
    );
    assert!(take_from(&mut table, "dup/B").is_none());
    // The neighbours are untouched.
    assert!(take_from(&mut table, "dup/A").is_some());
    assert!(take_from(&mut table, "dup/C").is_some());
}

/// An uncollected result is EVICTED, oldest first, once the table fills — it is
/// not held forever and it is not leaked. That is safe only because `LoadResult`
/// owns nothing: eviction costs the I/O that produced it and nothing else, and the
/// caller sees `UNKNOWN`, whose documented meaning is "request it again".
#[test]
fn an_uncollected_result_is_evicted_rather_than_leaked() {
    let keys: Vec<String> = (0..=RESULT_SLOTS).map(|i| format!("evict/{i}")).collect();
    let mut table = table_with(&keys.iter().map(String::as_str).collect::<Vec<_>>());
    assert_eq!(table.len(), RESULT_SLOTS);
    // The oldest was evicted...
    assert!(take_from(&mut table, "evict/0").is_none());
    // ...and every newer one survived, in its own slot.
    for index in 1..=RESULT_SLOTS {
        assert!(
            take_from(&mut table, &format!("evict/{index}")).is_some(),
            "slot {index} should have survived"
        );
    }
}

/// Taking from the MIDDLE must leave the remaining entries in their original
/// order, because eviction is oldest-first and reads that order.
///
/// Regression test for a real bug in `remove_at`: it rotated the survivors
/// instead of restoring them, so after one mid-table take the "oldest" entry was
/// no longer oldest and the next eviction discarded the wrong result. Verified to
/// fail against the buggy rotation, not merely to pass against the fix.
#[test]
fn taking_from_the_middle_keeps_the_eviction_order() {
    let keys: Vec<String> = (0..RESULT_SLOTS).map(|i| format!("ord/{i}")).collect();
    let mut table = table_with(&keys.iter().map(String::as_str).collect::<Vec<_>>());

    let middle = RESULT_SLOTS / 2;
    assert!(take_from(&mut table, &format!("ord/{middle}")).is_some());

    // One free slot after the take, so the second of these evicts the oldest.
    publish_into(
        &mut table,
        path_of("ord/x"),
        LoadResult::failed(DELUGE_ERROR_NONE),
    );
    publish_into(
        &mut table,
        path_of("ord/y"),
        LoadResult::failed(DELUGE_ERROR_NONE),
    );

    assert!(
        take_from(&mut table, "ord/0").is_none(),
        "entry 0 was the oldest and must be the one evicted"
    );
    let newest = RESULT_SLOTS - 1;
    assert!(
        take_from(&mut table, &format!("ord/{newest}")).is_some(),
        "the newest original entry was never the oldest, so it must survive"
    );
    assert!(take_from(&mut table, "ord/x").is_some());
    assert!(take_from(&mut table, "ord/y").is_some());
}

// ── The live pipeline: request -> in flight -> collect ──────────────────────
//
// These drive the process-wide statics through the real C ABI, so they only exist
// where those statics do. `cargo test --features async_audio_load_active` is a
// standing gate for exactly this reason — with the switch off, `request` refuses
// everything and `collect` reports UNKNOWN, so these would pass vacuously rather
// than testing anything.
#[cfg(feature = "async_audio_load_active")]
mod live_pipeline {
    use super::*;

    /// `RESULTS`/`QUEUE`/`IN_FLIGHT` are process-wide and cargo runs tests on
    /// parallel threads, so these must not interleave.
    static PIPELINE_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

    fn collect(path: &str) -> (u8, LoadResult) {
        let key = CString::new(path).unwrap();
        let mut out = LoadResult::failed(0xFF);
        // SAFETY: `key` is a valid NUL-terminated C string and `out` a live,
        // writable `LoadResult`.
        let status = unsafe { deluge_audio_load_collect(key.as_ptr(), &mut out) };
        (status, out)
    }

    fn request_load(path: &str, candidates: &[&str]) -> bool {
        let buffer = candidate_buffer(candidates);
        let key = CString::new(path).unwrap();
        // SAFETY: the buffer is `candidates.len() * DELUGE_MAX_PATH` bytes,
        // matching the count passed.
        unsafe {
            deluge_audio_load_request(
                key.as_ptr(),
                buffer.as_ptr().cast::<core::ffi::c_char>(),
                candidates.len() as u32,
            )
        }
    }

    /// A path the loader has never been asked about is `UNKNOWN`, not `PENDING`
    /// — `PENDING` would tell the caller to keep asking for something that will
    /// never arrive.
    #[test]
    fn an_unrequested_path_is_unknown() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        assert_eq!(collect("never/asked.wav").0, DELUGE_AUDIO_LOAD_UNKNOWN);
    }

    /// A queued-but-unparsed request reports `PENDING`, so the caller waits
    /// instead of re-requesting work already in the queue.
    #[test]
    fn a_queued_request_is_pending() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        assert!(request_load("queued/A.wav", &["queued/A.wav"]));
        assert_eq!(collect("queued/A.wav").0, DELUGE_AUDIO_LOAD_PENDING);
    }

    /// A load being parsed RIGHT NOW reports `PENDING`, not `UNKNOWN`.
    ///
    /// This branch had no test at all, because no test runs the loader task —
    /// the same degenerate-coverage shape that hid the `remove_at` bug.
    /// `mark_in_flight` is the seam the task uses, so driving it directly
    /// exercises the real branch rather than a copy of it.
    ///
    /// A false `UNKNOWN` here is not cosmetic: it tells the caller to re-request
    /// a load that is already running, on every tick, and each redundant request
    /// eventually orphans a result slot.
    #[test]
    fn an_in_flight_request_is_pending_not_unknown() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        block_on(mark_in_flight(Some(path_of("inflight/A.wav"))));
        assert_eq!(collect("inflight/A.wav").0, DELUGE_AUDIO_LOAD_PENDING);
        // A DIFFERENT path is still unknown — the branch matches on identity,
        // not on "something is in flight".
        assert_eq!(collect("inflight/B.wav").0, DELUGE_AUDIO_LOAD_UNKNOWN);
        block_on(mark_in_flight(None));
        assert_eq!(collect("inflight/A.wav").0, DELUGE_AUDIO_LOAD_UNKNOWN);
    }

    /// The window the task leaves open deliberately: a result is published while
    /// its path is still marked in flight. `PENDING` is the correct answer there
    /// — one extra tick, never a wrong one — and the result is collectable as
    /// soon as the mark clears.
    #[test]
    fn a_published_result_still_marked_in_flight_reports_pending() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        block_on(mark_in_flight(Some(path_of("window/A.wav"))));
        block_on(publish_result(
            path_of("window/A.wav"),
            LoadResult::failed(DELUGE_ERROR_NONE),
        ));
        assert_eq!(collect("window/A.wav").0, DELUGE_AUDIO_LOAD_PENDING);
        block_on(mark_in_flight(None));
        assert_eq!(collect("window/A.wav").0, DELUGE_AUDIO_LOAD_READY);
    }

    /// A collected result is removed: a second collect reports `UNKNOWN`.
    #[test]
    fn collecting_removes_the_result() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        block_on(publish_result(
            path_of("gone/A.wav"),
            LoadResult::failed(DELUGE_ERROR_FILE_CORRUPTED),
        ));
        let (status, result) = collect("gone/A.wav");
        assert_eq!(status, DELUGE_AUDIO_LOAD_READY);
        assert_eq!(result.error, DELUGE_ERROR_FILE_CORRUPTED);
        assert_eq!(collect("gone/A.wav").0, DELUGE_AUDIO_LOAD_UNKNOWN);
    }

    /// The `DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED` sentinel means the C side's
    /// buffer is not trustworthy at all. It must be REFUSED, not clamped to the
    /// candidate bound and read anyway — the header is explicit that the caller
    /// must not read `out`, and a silently truncated path can name a different
    /// real file.
    #[test]
    fn the_truncation_sentinel_is_refused_not_clamped() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let buffer = candidate_buffer(&["trunc/A.wav", "trunc/B.wav", "trunc/C.wav"]);
        let key = CString::new("trunc/A.wav").unwrap();
        // SAFETY: the buffer is three full slots; only the count is the sentinel.
        let accepted = unsafe {
            deluge_audio_load_request(
                key.as_ptr(),
                buffer.as_ptr().cast::<core::ffi::c_char>(),
                0xFFFF_FFFF,
            )
        };
        assert!(!accepted, "the sentinel must refuse the request");
        assert_eq!(
            collect("trunc/A.wav").0,
            DELUGE_AUDIO_LOAD_UNKNOWN,
            "a refused request must not have been queued"
        );
    }

    /// A request reads only the candidate slots it was given, never past the
    /// buffer the caller sized.
    #[test]
    fn a_request_reads_only_the_candidate_slots_it_was_given() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        assert!(request_load("bound/A.wav", &["bound/A.wav"]));
        assert_eq!(collect("bound/A.wav").0, DELUGE_AUDIO_LOAD_PENDING);
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
    let decoded = decode_candidates(&buffer).unwrap();
    assert_eq!(decoded.len(), 3);
    assert_eq!(decoded[0].as_str(), "A.wav");
    assert_eq!(decoded[1].as_str(), "ALT/A.wav");
    assert_eq!(decoded[2].as_str(), "A2.wav");
}

/// An unterminated slot is refused rather than read past — and the refusal must
/// hold when it is NOT the first slot, which is the case a single-slot buffer
/// cannot exercise. Slot 0 is a perfectly good path; slot 1 is unterminated, and
/// the whole buffer is rejected rather than the good prefix accepted.
#[test]
fn an_unterminated_candidate_slot_is_refused_even_after_a_good_one() {
    let mut buffer = candidate_buffer(&["A.wav", "B.wav"]);
    // Scrub slot 1's terminator, leaving slot 0 intact.
    for byte in buffer[DELUGE_MAX_PATH..].iter_mut() {
        *byte = 0xAA;
    }
    assert!(decode_candidates(&buffer).is_none());
    // Slot 0 alone still decodes, so the refusal above is about slot 1.
    assert_eq!(
        decode_candidates(&buffer[..DELUGE_MAX_PATH]).unwrap()[0].as_str(),
        "A.wav"
    );
}

// ── The admission rule, shared with the C++ side ────────────────────────────

/// One case from `path_admission_corpus.inc`.
struct AdmissionCase {
    accepted: bool,
    bytes: Vec<u8>,
    label: String,
}

/// Parse the corpus the C++ side `#include`s.
///
/// The file is the single source of truth for which paths the loader accepts:
/// C++ checks the rule before requesting (a path refused permanently must never
/// become a request that is retried forever), and this side applies it when the
/// request arrives. Reading the same file from both is what makes a drift between
/// them fail a test rather than resurface as a load that never completes.
///
/// The reader is deliberately simple, and the corpus documents the format it
/// understands: one macro call per line, `\xNN` the only escape.
fn admission_corpus() -> Vec<AdmissionCase> {
    const CORPUS: &str = include_str!("../../../deluge/storage/audio/path_admission_corpus.inc");
    const MACRO: &str = "DELUGE_PATH_ADMISSION_CASE(";

    fn unescape(literal: &str) -> Vec<u8> {
        let bytes = literal.as_bytes();
        let mut out = Vec::new();
        let mut i = 0;
        while i < bytes.len() {
            if bytes[i] == b'\\' {
                assert!(
                    i + 3 < bytes.len() && bytes[i + 1] == b'x',
                    "the corpus may only use \\xNN escapes: {literal}"
                );
                let hex = core::str::from_utf8(&bytes[i + 2..i + 4]).expect("ASCII hex");
                out.push(u8::from_str_radix(hex, 16).expect("two hex digits"));
                i += 4;
            } else {
                out.push(bytes[i]);
                i += 1;
            }
        }
        out
    }

    let mut cases = Vec::new();
    for line in CORPUS.lines() {
        let Some(rest) = line.trim().strip_prefix(MACRO) else {
            continue;
        };
        let rest = rest.strip_suffix(')').expect("a case line ends with ')'");
        let (accepted, rest) = rest.split_once(',').expect("accepted, then the bytes");
        let accepted = match accepted.trim() {
            "true" => true,
            "false" => false,
            other => panic!("case verdict is true or false, not {other}"),
        };
        let rest = rest
            .trim_start()
            .strip_prefix('"')
            .expect("the bytes are a string literal");
        let (literal, rest) = rest.split_once('"').expect("the literal is closed");
        let (repeat, label) = rest
            .trim_start()
            .strip_prefix(',')
            .expect("a repeat count follows the bytes")
            .split_once(',')
            .expect("a label follows the repeat count");
        let repeat: usize = repeat.trim().parse().expect("the repeat count is a number");
        let unit = unescape(literal);
        let mut bytes = Vec::with_capacity(unit.len() * repeat);
        for _ in 0..repeat {
            bytes.extend_from_slice(&unit);
        }
        cases.push(AdmissionCase {
            accepted,
            bytes,
            label: String::from(label.trim().trim_matches('"')),
        });
    }

    // A corpus that silently parsed to nothing would make every assertion below
    // vacuous -- exactly what a renamed macro or a reformatted file would do.
    assert!(
        cases.len() >= 15,
        "the shared corpus parsed to only {} case(s); the file's format changed under this reader",
        cases.len()
    );
    cases
}

/// Every case in the shared corpus gets the verdict it declares, from the two
/// entry points the refusal actually happens in.
///
/// `cstr_to_path` is the nominal path's check; `decode_candidates` is each
/// candidate slot's. A case too long to fit a slot AT ALL is unterminated there
/// rather than over-length, which is the same refusal by a different route --
/// both mean "this path never becomes a load", which is what the C++ side has to
/// agree with.
#[test]
fn the_admission_rule_matches_the_corpus_the_c_side_checks() {
    use std::ffi::CString;

    for case in admission_corpus() {
        let c_path = CString::new(case.bytes.clone()).expect("no interior NUL in the corpus");
        // SAFETY: a live, NUL-terminated CString.
        let accepted_as_path = unsafe { audio_loader::cstr_to_path(c_path.as_ptr()) }.is_some();
        assert_eq!(
            accepted_as_path, case.accepted,
            "cstr_to_path disagrees with the corpus on {}",
            case.label
        );

        let mut slot = vec![0xAAu8; DELUGE_MAX_PATH];
        let usable = case.bytes.len().min(DELUGE_MAX_PATH);
        slot[..usable].copy_from_slice(&case.bytes[..usable]);
        if case.bytes.len() < DELUGE_MAX_PATH {
            slot[case.bytes.len()] = 0;
        }
        let accepted_as_candidate = decode_candidates(&slot).is_some();
        assert_eq!(
            accepted_as_candidate, case.accepted,
            "decode_candidates disagrees with the corpus on {}",
            case.label
        );
    }
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
