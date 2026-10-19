//! Host tests for `audio_loader`: the parse driver over an in-memory [`LoadOps`], the result
//! table, and the request/collect C ABI.
//!
//! They pin that the efatfs handle is closed on every path; a verdict keeps the synchronous path's
//! error identity; a zero-length window is still read and fed; an uncollected result is evicted,
//! not leaked; and nothing parsed from one card survives its ejection, including a load in flight
//! across it and a publish into a table the flush could not clear.

use std::cell::{Cell, RefCell};
use std::ffi::CString;

use deluge_bsp_rust::app_loop;
use deluge_bsp_rust::channel::audio_loader;

use deluge_bsp_rust::storage::card;

use audio_loader::{
    CandidateList, DELUGE_ERROR_FILE_CORRUPTED, DELUGE_ERROR_FILE_UNREADABLE,
    DELUGE_ERROR_FILE_UNSUPPORTED, DELUGE_ERROR_NONE, DELUGE_ERROR_TOO_MANY_OPEN_STREAMS,
    FORGET_SLOTS, ForgetMarks, LoadGeometry, LoadOps, LoadPath, LoadRequest, LoadResult,
    OpenVerdict, RESULT_SLOTS, ResultTable, decode_candidates, load_io_once, path_hash,
    publish_into, take_from,
};

// The live request/collect pipeline; see `live_pipeline` below.
use audio_loader::{
    deluge_audio_load_collect, deluge_audio_load_forget, deluge_audio_load_request,
    forget_marks_for_test, mark_in_flight, publish_result,
};
use card::deluge_card_flush;
use deluge_bsp_rust::channel::{
    DELUGE_CHANNEL_PENDING, DELUGE_CHANNEL_READY, DELUGE_CHANNEL_UNKNOWN, DELUGE_PATH_MAX,
};
use deluge_bsp_rust::storage::card::generation as current_generation;

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
/// `open_handles` is the live count, asserted zero at the end of every test: a leaked efatfs handle
/// is silent capacity loss.
struct FakeOps {
    /// The file's bytes, served from memory, keyed by path.
    files: Vec<(String, Vec<u8>)>,
    /// `size_of` fails.
    size_fails: bool,
    /// Paths whose `open` reports a full efatfs handle table rather than a miss.
    table_full_for: Vec<String>,
    /// `size_of` fails only for these paths (by the bytes their handle refers to).
    size_fails_for: Vec<String>,
    /// `read_at` fails from this call onwards (a card pulled mid-parse).
    fail_read_after: Option<u32>,

    calls: RefCell<Vec<Call>>,
    open_handles: Cell<i32>,
    next_handle: Cell<u32>,
    reads_done: Cell<u32>,
    /// Live handles: `handle -> the bytes that handle refers to`.
    ///
    /// Every operation resolves its handle and panics if it was never issued or has been closed,
    /// so a driver that sized or read through the wrong handle cannot be served the right bytes by
    /// accident.
    live: RefCell<Vec<(u32, String, Vec<u8>)>>,
}

impl FakeOps {
    fn new() -> Self {
        Self {
            files: Vec::new(),
            size_fails: false,
            table_full_for: Vec::new(),
            size_fails_for: Vec::new(),
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

    /// Makes `open` of `path` report `OpenVerdict::TableFull` rather than a miss. The file need not
    /// exist.
    fn with_table_full_for(mut self, path: &str) -> Self {
        self.table_full_for.push(String::from(path));
        self
    }

    /// Makes `path` open but fail to size, which `tryOpenCandidate` treats as a candidate miss.
    fn with_size_failing_for(mut self, path: &str) -> Self {
        self.size_fails_for.push(String::from(path));
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

    /// Asserts no handle is left open.
    fn assert_settled(&self) {
        assert_eq!(
            self.open_handles(),
            0,
            "efatfs handle leaked: {:?}",
            self.calls()
        );
    }

    /// The bytes behind a live handle. Panics if `handle` was never issued or is closed.
    fn bytes_for(&self, handle: u32) -> Vec<u8> {
        self.entry_for(handle).1
    }

    /// The path and bytes behind a live handle. Panics as `bytes_for` does.
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
        if self.size_fails || self.size_fails_for.contains(&path) {
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
        // An empty fill trivially succeeds at any offset, as in production; this is the case the
        // driver must still feed. The handle is resolved first, so a bad handle is still caught.
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
            .position(|(live_handle, _, _)| *live_handle == handle)
            .unwrap_or_else(|| panic!("close of handle {handle}, which is not open"));
        live.remove(index);
        self.open_handles.set(self.open_handles.get() - 1);
    }

    /// A location naming the handle, so a test can tell which file's location a result carries.
    async fn location(&self, handle: u32) -> Option<audio_loader::FileLocation> {
        let _ = self.entry_for(handle); // a location is asked only of an open handle
        let mut location = audio_loader::FileLocation::NONE;
        location.context[0] = 1;
        location.mount_generation = handle;
        location.valid = 1;
        Some(location)
    }
}

/// Smallest RIFF/WAVE that parses: the 44-byte canonical header (12-byte RIFF container, 24-byte
/// `fmt ` chunk, 8-byte `data` chunk header), 16-bit mono 44100, then 4 frames of audio.
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

/// Runs a future to completion on the test thread.
fn block_on<F: core::future::Future>(future: F) -> F::Output {
    embassy_futures::block_on(future)
}

fn path_of(path: &str) -> LoadPath {
    LoadPath::try_from(path).expect("test path fits DELUGE_PATH_MAX")
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
        candidates_key: KEY,
    }
}

/// The candidates key the tests request and collect with unless a test is about keys.
const KEY: u32 = 0x5EED;

/// The alternate path a result reports, as a `String`.
fn alternate_of(result: &LoadResult) -> String {
    let end = result
        .alternate_path
        .iter()
        .position(|b| *b == 0)
        .expect("NUL-terminated");
    String::from_utf8(result.alternate_path[..end].to_vec()).expect("UTF-8")
}

// The happy path

/// A good file parses to a geometry, a size, and no error — and closes its handle.
#[test]
fn a_good_file_parses_to_a_geometry() {
    let ops = FakeOps::new().with_file("A.wav", minimal_wav());
    let result = block_on(load_io_once(&ops, &request("A.wav", &["A.wav"])));
    assert_eq!(result.error, DELUGE_ERROR_NONE);
    assert_eq!(result.size_bytes, 52);
    ops.assert_settled();
}

/// A good file's result says where the file is, taken while its handle was still open, so the
/// collector can open its read stream without looking up the path again.
#[test]
fn a_good_file_reports_where_it_is() {
    let ops = FakeOps::new()
        .with_file("ALT/A.wav", minimal_wav())
        .with_file("A.wav", minimal_wav());
    let result = block_on(load_io_once(&ops, &request("A.wav", &["X.wav", "A.wav"])));
    assert_eq!(result.error, DELUGE_ERROR_NONE);
    assert_eq!(result.location.valid, 1);
    // X.wav is missing, so A.wav takes the fake's first handle, 7, which its location names.
    assert_eq!(result.location.mount_generation, 7);
    ops.assert_settled();
}

/// A failed load says nowhere: there is no file to open.
#[test]
fn a_failed_load_reports_no_location() {
    let ops = FakeOps::new()
        .with_file("A.wav", minimal_wav())
        .with_read_failing_after(1);
    let result = block_on(load_io_once(&ops, &request("A.wav", &["A.wav"])));
    assert_eq!(result.error, DELUGE_ERROR_FILE_UNREADABLE);
    assert_eq!(result.location.valid, 0);
    ops.assert_settled();
}

/// The geometry crossing the ABI is the parsed header, field for field; a missed field loads
/// without error and renders wrong.
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
    // The parser's "the file said nothing" default (the Serum convention).
    assert_eq!(geometry.wavetable_cycle_size, 2048);
}

/// Two loads through one `FakeOps` each report their own file's size, via their own handle, so a
/// driver that cached a handle or size between loads fails.
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

// The handle is released on every path

/// Nothing opens: no handle was taken, and the verdict is `FILE_UNREADABLE` (always a result,
/// never silence).
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

/// A read that fails mid-parse (a card pulled) still closes the handle and produces a verdict.
#[test]
fn a_failed_read_mid_parse_closes_the_handle() {
    let ops = FakeOps::new()
        .with_file("A.wav", minimal_wav())
        .with_read_failing_after(1);
    let result = block_on(load_io_once(&ops, &request("A.wav", &["A.wav"])));
    assert_eq!(result.error, DELUGE_ERROR_FILE_UNREADABLE);
    ops.assert_settled();
}

// Parse verdicts keep the synchronous path's error identity

/// Not audio: `FILE_UNSUPPORTED`, which `sample_browser.cpp` reads as "not a wavetable, try it
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

// Candidate order and the alternate location

/// The first candidate that opens wins; a winner not in the last slot is reported as the
/// alternate location, the path the collector binds its read stream to. The list is shaped as
/// `proposeLoadCandidates` shapes it: alternates first, the regular path last.
#[test]
fn the_first_candidate_that_opens_becomes_the_alternate_location() {
    let ops = FakeOps::new().with_file("SAMPLES/SONG/A.wav", minimal_wav());
    let result = block_on(load_io_once(
        &ops,
        &request("SAMPLES/A.wav", &["SAMPLES/SONG/A.wav", "SAMPLES/A.wav"]),
    ));
    assert_eq!(result.error, DELUGE_ERROR_NONE);
    assert_eq!(alternate_of(&result), "SAMPLES/SONG/A.wav");
    ops.assert_settled();
}

/// Candidates are tried in the order given and the first that opens wins; the C++ side picks which
/// copy of a duplicated sample loads by that order. All three files exist, so "first" is
/// distinguishable from "last" or "any"; removing candidates walks the winner down the list.
#[test]
fn the_first_candidate_that_opens_wins_when_more_than_one_would() {
    // Flattened alternate, bare-name alternate, regular path last.
    let candidates = [
        "SAMPLES/ALT/KIT_A.wav",
        "SAMPLES/ALT/A.wav",
        "SAMPLES/KIT/A.wav",
    ];

    let all_three = FakeOps::new()
        .with_file(candidates[0], minimal_wav())
        .with_file(candidates[1], minimal_wav())
        .with_file(candidates[2], minimal_wav());
    let result = block_on(load_io_once(
        &all_three,
        &request("SAMPLES/KIT/A.wav", &candidates),
    ));
    assert_eq!(result.error, DELUGE_ERROR_NONE);
    assert_eq!(alternate_of(&result), candidates[0]);
    all_three.assert_settled();

    // Drop the first: the second alternate wins, still an alternate.
    let without_flattened = FakeOps::new()
        .with_file(candidates[1], minimal_wav())
        .with_file(candidates[2], minimal_wav());
    let result = block_on(load_io_once(
        &without_flattened,
        &request("SAMPLES/KIT/A.wav", &candidates),
    ));
    assert_eq!(result.error, DELUGE_ERROR_NONE);
    assert_eq!(alternate_of(&result), candidates[1]);
    without_flattened.assert_settled();

    // Drop both alternates: the regular path wins and reports no alternate location.
    let regular_only = FakeOps::new().with_file(candidates[2], minimal_wav());
    let result = block_on(load_io_once(
        &regular_only,
        &request("SAMPLES/KIT/A.wav", &candidates),
    ));
    assert_eq!(result.error, DELUGE_ERROR_NONE);
    assert_eq!(alternate_of(&result), "");
    regular_only.assert_settled();
}

/// The regular path winning reports an empty alternate path, which `sample_build::begin` reads as
/// "resolved at the nominal path".
#[test]
fn the_regular_path_winning_reports_no_alternate_location() {
    let ops = FakeOps::new().with_file("SAMPLES/A.wav", minimal_wav());
    let result = block_on(load_io_once(
        &ops,
        &request("SAMPLES/A.wav", &["SAMPLES/A.wav"]),
    ));
    assert_eq!(alternate_of(&result), "");
}

/// When the alternate directory is the file's own directory, the bare-name alternate candidate
/// equals the nominal path.
///
/// The winner must be classified by slot, not by string equality: the slot-1 hit is an alternate.
/// Reporting it as regular would leave `Sample::loadedFromAlternatePath` empty, and
/// `save_song_ui.cpp` would skip copying the sample into the saved song folder.
#[test]
fn a_bare_name_candidate_equal_to_the_nominal_path_is_still_an_alternate() {
    // `proposeLoadCandidates` for `SAMPLES/ALT/A.wav` with alternate directory `SAMPLES/ALT`:
    // flattened form, bare name in the alternate dir, regular path (the last two identical).
    let candidates = [
        "SAMPLES/ALT/ALT_A.wav",
        "SAMPLES/ALT/A.wav",
        "SAMPLES/ALT/A.wav",
    ];
    let ops = FakeOps::new().with_file("SAMPLES/ALT/A.wav", minimal_wav());
    let result = block_on(load_io_once(
        &ops,
        &request("SAMPLES/ALT/A.wav", &candidates),
    ));
    assert_eq!(result.error, DELUGE_ERROR_NONE);
    assert_eq!(
        alternate_of(&result),
        "SAMPLES/ALT/A.wav",
        "the slot-1 hit is an alternate-dir hit even though it stringifies to the nominal path"
    );
    ops.assert_settled();
}

// A full handle table is not a candidate miss

/// A full efatfs handle table must not read as "this candidate is not here".
///
/// The table fills up under browser preview, this loader's own workload. As a miss it would become
/// `FILE_UNREADABLE`, and the collector would probe the alternate directory and could load a
/// same-named file from there. It must be `TOO_MANY_OPEN_STREAMS`, which the collector answers by
/// reclaiming unleased files.
#[test]
fn a_full_handle_table_is_reported_as_too_many_open_streams() {
    let ops = FakeOps::new()
        .with_file("SAMPLES/ALT/A.wav", minimal_wav())
        .with_table_full_for("SAMPLES/A.wav");
    let result = block_on(load_io_once(
        &ops,
        // The alternate misses; the regular path then hits the full table.
        &request("SAMPLES/A.wav", &["SAMPLES/OTHER/A.wav", "SAMPLES/A.wav"]),
    ));
    assert_eq!(
        result.error, DELUGE_ERROR_TOO_MANY_OPEN_STREAMS,
        "a full handle table must keep its own identity, not masquerade as a candidate miss"
    );
    ops.assert_settled();
}

/// A full table stops the walk: every remaining candidate would hit it too, and continuing would
/// end on `FILE_UNREADABLE`.
#[test]
fn a_full_handle_table_stops_the_candidate_walk() {
    let ops = FakeOps::new()
        .with_file("SAMPLES/ALT/A.wav", minimal_wav())
        .with_table_full_for("SAMPLES/ALT/A.wav");
    let result = block_on(load_io_once(
        &ops,
        &request("SAMPLES/A.wav", &["SAMPLES/ALT/A.wav", "SAMPLES/A.wav"]),
    ));
    assert_eq!(result.error, DELUGE_ERROR_TOO_MANY_OPEN_STREAMS);
    assert_eq!(
        ops.calls(),
        vec![Call::Open(String::from("SAMPLES/ALT/A.wav"))],
        "the walk must stop at the full table, not go on to try the regular path"
    );
    ops.assert_settled();
}

// An unsizeable candidate is a miss, not the end of the load

/// A candidate that opens but cannot be sized is a candidate miss, as in `tryOpenCandidate`; it
/// must not shadow a good later candidate.
#[test]
fn an_unsizeable_candidate_yields_to_the_next_one() {
    let ops = FakeOps::new()
        .with_file("SAMPLES/ALT/A.wav", minimal_wav())
        .with_file("SAMPLES/A.wav", minimal_wav())
        .with_size_failing_for("SAMPLES/ALT/A.wav");
    let result = block_on(load_io_once(
        &ops,
        &request("SAMPLES/A.wav", &["SAMPLES/ALT/A.wav", "SAMPLES/A.wav"]),
    ));
    assert_eq!(
        result.error, DELUGE_ERROR_NONE,
        "the sizeable regular path should have answered the load"
    );
    assert_eq!(alternate_of(&result), "");
    // The unsizeable candidate's handle was closed before the next was opened.
    assert_eq!(
        ops.calls(),
        vec![
            Call::Open(String::from("SAMPLES/ALT/A.wav")),
            Call::SizeOf,
            Call::Close,
            Call::Open(String::from("SAMPLES/A.wav")),
            Call::SizeOf,
            Call::Read { offset: 0, len: 12 },
            Call::Read { offset: 12, len: 8 },
            Call::Read {
                offset: 20,
                len: 16
            },
            Call::Read { offset: 36, len: 8 },
            Call::Close,
        ]
    );
    ops.assert_settled();
}

// The window protocol

/// Every window stays inside the size the parser was given (`start + len <= size`), which is what
/// lets [`LoadOps::read_at`] treat a short read as a real failure.
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
    // The audio payload is skipped by arithmetic: the walk stops at the `data` chunk header
    // (offset 36, 8 bytes).
    assert_eq!(ops.reads(), vec![(0, 12), (12, 8), (20, 16), (36, 8)]);
}

/// A zero-length window must still be read and fed: the parser resolves end of file inside
/// `feed`, so a driver that skipped it would loop forever (this test would hang).
///
/// A zero-byte file produces one. Production refuses zero-byte files before building a `Sample`,
/// so this guards the driver's own loop.
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

// The result table, driven through a local `ResultTable` rather than the `RESULTS` static.

/// The card generation the ordering tests publish and take under. Nonzero, so a publish that
/// dropped its stamp (leaving 0) cannot still satisfy every take.
const CARD: u32 = 3;

fn table_with(paths: &[&str]) -> ResultTable {
    let mut table = ResultTable::new();
    for path in paths {
        publish_into(
            &mut table,
            path_of(path),
            LoadResult::failed(DELUGE_ERROR_FILE_CORRUPTED),
            CARD,
        );
    }
    table
}

/// Takes under [`CARD`]. The generation tests below pass the generation explicitly.
fn take(table: &mut ResultTable, path: &str) -> Option<LoadResult> {
    take_from(table, path, 0, CARD)
}

/// A published result is taken exactly once; returning it twice could build a `Sample` twice.
#[test]
fn a_result_is_taken_exactly_once() {
    let mut table = table_with(&["once/A.wav"]);
    let taken = take(&mut table, "once/A.wav").expect("published");
    assert_eq!(taken.error, DELUGE_ERROR_FILE_CORRUPTED);
    assert!(take(&mut table, "once/A.wav").is_none());
}

/// A path that was never published is simply absent.
#[test]
fn an_unpublished_path_is_absent() {
    let mut table = table_with(&["other/A.wav"]);
    assert!(take(&mut table, "never/asked.wav").is_none());
}

/// Re-publishing a key replaces its entry rather than adding a second, and the newest parse is
/// taken. Exercised mid-table, where the replacement goes through `remove_at`.
#[test]
fn republishing_a_key_replaces_its_entry_in_place() {
    let mut table = table_with(&["dup/A", "dup/B", "dup/C"]);
    publish_into(
        &mut table,
        path_of("dup/B"),
        LoadResult::failed(DELUGE_ERROR_FILE_UNSUPPORTED),
        CARD,
    );
    assert_eq!(table.len(), 3, "replace must not add an entry");
    assert_eq!(
        take(&mut table, "dup/B").unwrap().error,
        DELUGE_ERROR_FILE_UNSUPPORTED,
        "latest wins"
    );
    assert!(take(&mut table, "dup/B").is_none());
    // The neighbours are untouched.
    assert!(take(&mut table, "dup/A").is_some());
    assert!(take(&mut table, "dup/C").is_some());
}

/// An uncollected result is evicted, oldest first, once the table fills. Safe because
/// `LoadResult` owns nothing; the collector sees `UNKNOWN` and re-requests.
#[test]
fn an_uncollected_result_is_evicted_rather_than_leaked() {
    let keys: Vec<String> = (0..=RESULT_SLOTS).map(|i| format!("evict/{i}")).collect();
    let mut table = table_with(&keys.iter().map(String::as_str).collect::<Vec<_>>());
    assert_eq!(table.len(), RESULT_SLOTS);
    // The oldest was evicted...
    assert!(take(&mut table, "evict/0").is_none());
    // ...and every newer one survived, in its own slot.
    for index in 1..=RESULT_SLOTS {
        assert!(
            take(&mut table, &format!("evict/{index}")).is_some(),
            "slot {index} should have survived"
        );
    }
}

/// Taking from the middle leaves the remaining entries in their original order, which
/// oldest-first eviction reads. Regression test: `remove_at` once rotated the survivors, so the
/// next eviction discarded the wrong result.
#[test]
fn taking_from_the_middle_keeps_the_eviction_order() {
    let keys: Vec<String> = (0..RESULT_SLOTS).map(|i| format!("ord/{i}")).collect();
    let mut table = table_with(&keys.iter().map(String::as_str).collect::<Vec<_>>());

    let middle = RESULT_SLOTS / 2;
    assert!(take(&mut table, &format!("ord/{middle}")).is_some());

    // One free slot after the take, so the second of these evicts the oldest.
    publish_into(
        &mut table,
        path_of("ord/x"),
        LoadResult::failed(DELUGE_ERROR_NONE),
        CARD,
    );
    publish_into(
        &mut table,
        path_of("ord/y"),
        LoadResult::failed(DELUGE_ERROR_NONE),
        CARD,
    );

    assert!(
        take(&mut table, "ord/0").is_none(),
        "entry 0 was the oldest and must be the one evicted"
    );
    let newest = RESULT_SLOTS - 1;
    assert!(
        take(&mut table, &format!("ord/{newest}")).is_some(),
        "the newest original entry was never the oldest, so it must survive"
    );
    assert!(take(&mut table, "ord/x").is_some());
    assert!(take(&mut table, "ord/y").is_some());
}

// Candidates keys

/// A result whose candidates key is `key`.
fn keyed(error: u8, key: u32) -> LoadResult {
    let mut result = LoadResult::failed(error);
    result.candidates_key = key;
    result
}

/// Answers for one path under two candidate lists are both kept, and each is taken only by its own key. A collector
/// presenting another key finds nothing and leaves the answer where it is.
#[test]
fn answers_for_one_path_under_different_candidates_are_kept_apart() {
    let mut table = ResultTable::new();
    publish_into(
        &mut table,
        path_of("keys/A.wav"),
        keyed(DELUGE_ERROR_FILE_UNREADABLE, 1),
        CARD,
    );
    publish_into(
        &mut table,
        path_of("keys/A.wav"),
        keyed(DELUGE_ERROR_NONE, 2),
        CARD,
    );
    assert_eq!(
        table.len(),
        2,
        "a second key's answer must not replace the first's"
    );
    assert!(
        take_from(&mut table, "keys/A.wav", 3, CARD).is_none(),
        "a key nobody asked with takes nothing"
    );
    assert_eq!(
        table.len(),
        2,
        "a take for another key leaves both answers in place"
    );
    let second = take_from(&mut table, "keys/A.wav", 2, CARD).expect("its own answer");
    assert_eq!(second.error, DELUGE_ERROR_NONE);
    assert_eq!(second.candidates_key, 2);
    let first = take_from(&mut table, "keys/A.wav", 1, CARD).expect("its own answer");
    assert_eq!(first.error, DELUGE_ERROR_FILE_UNREADABLE);
    assert!(table.is_empty());
}

/// Re-publishing under the same path and key still replaces the entry.
#[test]
fn republishing_under_the_same_key_replaces_only_that_answer() {
    let mut table = ResultTable::new();
    publish_into(
        &mut table,
        path_of("keys/B.wav"),
        keyed(DELUGE_ERROR_FILE_UNREADABLE, 1),
        CARD,
    );
    publish_into(
        &mut table,
        path_of("keys/B.wav"),
        keyed(DELUGE_ERROR_FILE_CORRUPTED, 2),
        CARD,
    );
    publish_into(
        &mut table,
        path_of("keys/B.wav"),
        keyed(DELUGE_ERROR_NONE, 1),
        CARD,
    );
    assert_eq!(table.len(), 2);
    assert_eq!(
        take_from(&mut table, "keys/B.wav", 1, CARD).unwrap().error,
        DELUGE_ERROR_NONE
    );
    assert_eq!(
        take_from(&mut table, "keys/B.wav", 2, CARD).unwrap().error,
        DELUGE_ERROR_FILE_CORRUPTED
    );
}

/// Every result the driver returns, found or not, carries its request's candidates key.
#[test]
fn every_result_carries_its_requests_candidates_key() {
    let ops = FakeOps::new().with_file("keyed/found.wav", minimal_wav());
    let mut found = request("keyed/found.wav", &["keyed/found.wav"]);
    found.candidates_key = 0xABCD_0001;
    let result = block_on(load_io_once(&ops, &found));
    assert_eq!(result.error, DELUGE_ERROR_NONE);
    assert_eq!(result.candidates_key, 0xABCD_0001);

    let mut missing = request("keyed/missing.wav", &["keyed/missing.wav"]);
    missing.candidates_key = 0xABCD_0002;
    let result = block_on(load_io_once(&ops, &missing));
    assert_eq!(result.error, DELUGE_ERROR_FILE_UNREADABLE);
    assert_eq!(result.candidates_key, 0xABCD_0002);
    ops.assert_settled();
}

// The generation stamp on the resident entry
//
// A stale entry can stay resident: `flush_tables`' clears are `try_lock`ed, and a publish can pass
// its own pre-insert check and land after the flush (no happens-before edge from the bump). Under
// `host_app` the loader is a plain `std::thread`, so both happen. `take_from` comparing the entry's
// stamp with the taker's generation is what makes them harmless; these tests pin that.

/// An entry stamped by the previous card is not returned, and is removed from the table rather
/// than left occupying a slot.
#[test]
fn an_entry_from_a_previous_card_is_dropped_not_returned() {
    let mut table = table_with(&["gen/A.wav"]);
    assert!(
        take_from(&mut table, "gen/A.wav", 0, CARD + 1).is_none(),
        "a result parsed from the previous card must not be collectable"
    );
    assert!(
        table.is_empty(),
        "the stale entry must be dropped from the table, not merely withheld"
    );
    assert!(
        take_from(&mut table, "gen/A.wav", 0, CARD).is_none(),
        "and it must not be recoverable by asking under its own old generation"
    );
}

/// The taker's current generation decides, covering the case `publish_result`'s pre-insert check
/// cannot: after a flush bumped to `CARD + 1` without clearing the table, a load that sampled
/// `CARD` publishes anyway. That entry must be uncollectable, and its `CARD + 1` neighbour must
/// not be.
#[test]
fn a_result_stamped_before_a_flush_cannot_be_taken_after_it() {
    let mut table = ResultTable::new();

    // The late publish: stamped with the pre-flush generation, inserted after it.
    publish_into(
        &mut table,
        path_of("gen/late.wav"),
        LoadResult::failed(DELUGE_ERROR_NONE),
        CARD,
    );
    // A load that genuinely began on the new card.
    publish_into(
        &mut table,
        path_of("gen/fresh.wav"),
        LoadResult::failed(DELUGE_ERROR_FILE_CORRUPTED),
        CARD + 1,
    );
    assert_eq!(
        table.len(),
        2,
        "the late publish must actually have landed, or this test proves nothing about \
         what happens once it has"
    );

    assert!(
        take_from(&mut table, "gen/late.wav", 0, CARD + 1).is_none(),
        "a result stamped before the flush must not be collectable after it"
    );
    // Positive control: a check that refused everything after a flush would pass the assertion
    // above while wedging the loader.
    assert_eq!(
        take_from(&mut table, "gen/fresh.wav", 0, CARD + 1)
            .expect("a load that began on the current card must be collectable")
            .error,
        DELUGE_ERROR_FILE_CORRUPTED
    );
    assert!(table.is_empty());
}

// Forget marks

/// A mark is taken once: the answer it discards is the one owed when it was made.
#[test]
fn a_forget_mark_is_taken_once() {
    let mut marks = ForgetMarks::new();
    marks.mark("forget/A.wav", |_| true);
    marks.mark("forget/A.wav", |_| true);
    assert_eq!(marks.len(), 1, "marking a path twice holds one mark");
    assert!(!marks.take("forget/B.wav"), "another path has no mark");
    assert!(marks.take("forget/A.wav"));
    assert!(!marks.take("forget/A.wav"), "a taken mark is gone");
    assert!(marks.is_empty());
}

/// A full table drops a mark whose path is no longer outstanding in preference to an older one that still is, and
/// falls back to the oldest only when every mark is outstanding.
#[test]
fn a_full_forget_table_keeps_live_marks_over_stale_ones() {
    let paths: Vec<String> = (0..=FORGET_SLOTS)
        .map(|i| format!("forget/full/{i}.wav"))
        .collect();
    let fill = |marks: &mut ForgetMarks, owed: &dyn Fn(u32) -> bool| {
        for path in &paths {
            marks.mark(path, owed);
        }
    };

    // The oldest mark is live and the second is stale: the stale one goes.
    let mut marks = ForgetMarks::new();
    for path in &paths[..FORGET_SLOTS] {
        marks.mark(path, |_| true);
    }
    assert_eq!(marks.len(), FORGET_SLOTS);
    let stale = path_hash(&paths[1]);
    marks.mark(&paths[FORGET_SLOTS], |hash| hash != stale);
    assert_eq!(marks.len(), FORGET_SLOTS);
    assert!(marks.take(&paths[0]), "a live mark must survive eviction");
    assert!(
        !marks.take(&paths[1]),
        "the stale mark must be the one dropped"
    );
    assert!(
        marks.take(&paths[FORGET_SLOTS]),
        "the newest mark must be kept"
    );

    // Every mark live: the oldest goes.
    let mut marks = ForgetMarks::new();
    fill(&mut marks, &|_| true);
    assert_eq!(marks.len(), FORGET_SLOTS);
    assert!(
        !marks.take(&paths[0]),
        "with all live, the oldest is dropped"
    );
    assert!(marks.take(&paths[FORGET_SLOTS]));
    assert!(marks.take(&paths[1]));
}

// The live pipeline: request, in flight, collect, through the real C ABI over the process-wide
// statics.
mod live_pipeline {
    use super::*;

    /// The statics are process-wide and tests run on parallel threads, so tests take this lock.
    static PIPELINE_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

    /// What `collect` seeds `out` with. `0xFF` is not a real error code (the largest is 34).
    const UNTOUCHED: u8 = 0xFF;

    /// Collects with the candidates key [`publish`] stamps.
    fn collect(path: &str) -> (u8, LoadResult) {
        collect_keyed(path, 0)
    }

    fn collect_keyed(path: &str, candidates_key: u32) -> (u8, LoadResult) {
        let key = CString::new(path).unwrap();
        let mut out = LoadResult::failed(UNTOUCHED);
        // SAFETY: `key` is a valid NUL-terminated C string and `out` a live,
        // writable `LoadResult`.
        let status = unsafe { deluge_audio_load_collect(key.as_ptr(), candidates_key, &mut out) };
        (status, out)
    }

    /// Asserts `out` came back exactly as [`collect`] seeded it: `audio_load.h` writes `out` only
    /// on `READY`, so a caller may pass an uninitialised slot.
    ///
    /// Catches a `probe` branch that writes a placeholder, or a write hoisted above the take,
    /// either of which leaves the status right. Every field is checked, since a partial write is
    /// the likelier accident.
    fn assert_out_untouched(result: &LoadResult) {
        assert_eq!(
            result.error, UNTOUCHED,
            "`out` must not be written unless the status is READY"
        );
        assert_eq!(
            result.geometry,
            LoadGeometry::default(),
            "`out.geometry` must not be written unless the status is READY"
        );
        assert_eq!(
            result.size_bytes, 0,
            "`out.size_bytes` must not be written unless the status is READY"
        );
        assert!(
            result.alternate_path.iter().all(|&byte| byte == 0),
            "`out.alternate_path` must not be written unless the status is READY"
        );
        assert_eq!(
            result.candidates_key, 0,
            "`out.candidates_key` must not be written unless the status is READY"
        );
    }

    /// Publishes as the loader task would for a load that began under the current card generation.
    fn publish(path: &str, result: LoadResult) {
        block_on(publish_result(path_of(path), result, current_generation()));
    }

    fn request_load(path: &str, candidates: &[&str]) -> bool {
        let buffer = candidate_buffer(candidates);
        let key = CString::new(path).unwrap();
        // SAFETY: the buffer is `candidates.len() * DELUGE_PATH_MAX` bytes,
        // matching the count passed.
        unsafe {
            deluge_audio_load_request(
                key.as_ptr(),
                buffer.as_ptr().cast::<core::ffi::c_char>(),
                candidates.len() as u32,
                KEY,
            )
        }
    }

    fn forget(path: &str) {
        let key = CString::new(path).unwrap();
        // SAFETY: `key` is a valid NUL-terminated C string.
        unsafe { deluge_audio_load_forget(key.as_ptr()) };
    }

    /// Two live loads of one path with different candidate lists: the one whose request is out waits for it, the other
    /// waits too (the path is in flight), then finds nothing for its own candidates and asks; the answer stays for the
    /// load whose candidates found it.
    #[test]
    fn a_collect_for_other_candidates_leaves_the_answer_for_its_own() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        const FEWER: u32 = 0x0F;
        const MORE: u32 = 0x3F;
        block_on(mark_in_flight(Some(path_of("pair/A.wav"))));
        assert_eq!(
            collect_keyed("pair/A.wav", MORE).0,
            DELUGE_CHANNEL_PENDING,
            "any request out for the path is waited for"
        );
        publish("pair/A.wav", keyed(DELUGE_ERROR_FILE_UNREADABLE, FEWER));
        block_on(mark_in_flight(None));

        let (status, out) = collect_keyed("pair/A.wav", MORE);
        assert_eq!(
            status, DELUGE_CHANNEL_UNKNOWN,
            "a miss for fewer candidates is not this collector's answer"
        );
        assert_out_untouched(&out);

        let (status, out) = collect_keyed("pair/A.wav", FEWER);
        assert_eq!(
            status, DELUGE_CHANNEL_READY,
            "the answer must still be there for the load that asked with its candidates"
        );
        assert_eq!(out.error, DELUGE_ERROR_FILE_UNREADABLE);
        assert_eq!(out.candidates_key, FEWER);
    }

    /// A request dropped while in flight: its answer is discarded when it is published, so the next collect of the
    /// path is `UNKNOWN` and its caller asks with its own candidates.
    #[test]
    fn a_forgotten_in_flight_request_publishes_nothing() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        block_on(mark_in_flight(Some(path_of("forget/inflight.wav"))));
        forget("forget/inflight.wav");
        publish(
            "forget/inflight.wav",
            LoadResult::failed(DELUGE_ERROR_FILE_UNREADABLE),
        );
        block_on(mark_in_flight(None));
        let (status, out) = collect("forget/inflight.wav");
        assert_eq!(
            status, DELUGE_CHANNEL_UNKNOWN,
            "a dropped request's answer must not reach the next collect of its path"
        );
        assert_out_untouched(&out);

        // Positive control: the mark is spent, so the next request's answer is collected.
        publish("forget/inflight.wav", LoadResult::failed(DELUGE_ERROR_NONE));
        assert_eq!(collect("forget/inflight.wav").0, DELUGE_CHANNEL_READY);
    }

    /// A request dropped once its answer was already published: the answer is discarded at the collect.
    #[test]
    fn a_forgotten_published_answer_is_discarded_at_the_collect() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        publish(
            "forget/published.wav",
            LoadResult::failed(DELUGE_ERROR_FILE_UNREADABLE),
        );
        forget("forget/published.wav");
        let (status, out) = collect("forget/published.wav");
        assert_eq!(status, DELUGE_CHANNEL_UNKNOWN);
        assert_out_untouched(&out);
        assert_eq!(collect("forget/published.wav").0, DELUGE_CHANNEL_UNKNOWN);

        publish(
            "forget/published.wav",
            LoadResult::failed(DELUGE_ERROR_NONE),
        );
        assert_eq!(
            collect("forget/published.wav").0,
            DELUGE_CHANNEL_READY,
            "the mark must be spent by the answer it discarded"
        );
    }

    /// Forgetting a path the loader owes nothing leaves no mark, so a later answer is not lost.
    #[test]
    fn forgetting_a_path_with_nothing_outstanding_changes_nothing() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let before = forget_marks_for_test();
        forget("forget/nothing.wav");
        assert_eq!(forget_marks_for_test(), before);
        publish("forget/nothing.wav", LoadResult::failed(DELUGE_ERROR_NONE));
        assert_eq!(collect("forget/nothing.wav").0, DELUGE_CHANNEL_READY);
    }

    /// A new request for a path clears a mark left for it, so the mark cannot discard the new request's answer.
    #[test]
    fn a_new_request_clears_a_spent_mark() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        block_on(mark_in_flight(Some(path_of("forget/respent.wav"))));
        forget("forget/respent.wav");
        // The owed answer never arrives (its slot was cleared), so the mark outlives it.
        block_on(mark_in_flight(None));
        let marked = forget_marks_for_test();
        assert!(marked >= 1, "the in-flight request must have been marked");
        assert!(request_load("forget/respent.wav", &["forget/respent.wav"]));
        assert_eq!(
            forget_marks_for_test(),
            marked - 1,
            "the request must clear the path's mark"
        );
        // Drain the queue, so this test leaves it as it found it.
        deluge_card_flush();
    }

    /// A path never asked about is `UNKNOWN`; `PENDING` would make the caller wait forever.
    #[test]
    fn an_unrequested_path_is_unknown() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let (status, out) = collect("never/asked.wav");
        assert_eq!(status, DELUGE_CHANNEL_UNKNOWN);
        assert_out_untouched(&out);
    }

    #[test]
    fn a_queued_request_is_pending() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        assert!(request_load("queued/A.wav", &["queued/A.wav"]));
        let (status, out) = collect("queued/A.wav");
        assert_eq!(status, DELUGE_CHANNEL_PENDING);
        assert_out_untouched(&out);
    }

    /// A load being parsed now reports `PENDING`, not `UNKNOWN` (which would make the caller
    /// re-request it every tick). No test runs the task, so `mark_in_flight` is driven directly.
    #[test]
    fn an_in_flight_request_is_pending_not_unknown() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        block_on(mark_in_flight(Some(path_of("inflight/A.wav"))));
        assert_eq!(collect("inflight/A.wav").0, DELUGE_CHANNEL_PENDING);
        // Another path is still unknown: the branch matches on the path.
        assert_eq!(collect("inflight/B.wav").0, DELUGE_CHANNEL_UNKNOWN);
        block_on(mark_in_flight(None));
        assert_eq!(collect("inflight/A.wav").0, DELUGE_CHANNEL_UNKNOWN);
    }

    /// Clearing the in-flight mark wakes the app (`DELUGE_WAKE_STORAGE`); marking it does not.
    #[test]
    fn finishing_a_load_wakes_the_app() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        app_loop::reset_reasons_for_test();
        block_on(mark_in_flight(Some(path_of("wake/A.wav"))));
        assert_eq!(
            app_loop::reasons_for_test() & app_loop::WAKE_STORAGE,
            0,
            "marking a load in flight must not wake the app early"
        );
        block_on(mark_in_flight(None));
        assert_ne!(
            app_loop::reasons_for_test() & app_loop::WAKE_STORAGE,
            0,
            "clearing the in-flight mark must wake the app"
        );
    }

    /// A card flush wakes the app (`DELUGE_WAKE_STORAGE`) too: queued and in-flight work for the
    /// old card is gone, and a job waiting on one of those drops must not sleep through it.
    #[test]
    fn a_flush_wakes_the_app() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        app_loop::reset_reasons_for_test();
        deluge_card_flush();
        assert_ne!(
            app_loop::reasons_for_test() & app_loop::WAKE_STORAGE,
            0,
            "a card flush must wake the app"
        );
    }

    /// A result published while its path is still marked in flight reports `PENDING` (one extra
    /// tick), and is collectable once the mark clears.
    #[test]
    fn a_published_result_still_marked_in_flight_reports_pending() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        block_on(mark_in_flight(Some(path_of("window/A.wav"))));
        publish("window/A.wav", LoadResult::failed(DELUGE_ERROR_NONE));
        assert_eq!(collect("window/A.wav").0, DELUGE_CHANNEL_PENDING);
        block_on(mark_in_flight(None));
        assert_eq!(collect("window/A.wav").0, DELUGE_CHANNEL_READY);
    }

    /// A collected result is removed: a second collect reports `UNKNOWN`.
    #[test]
    fn collecting_removes_the_result() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        publish(
            "gone/A.wav",
            LoadResult::failed(DELUGE_ERROR_FILE_CORRUPTED),
        );
        let (status, result) = collect("gone/A.wav");
        assert_eq!(status, DELUGE_CHANNEL_READY);
        assert_eq!(result.error, DELUGE_ERROR_FILE_CORRUPTED);
        assert_eq!(collect("gone/A.wav").0, DELUGE_CHANNEL_UNKNOWN);
    }

    /// The `DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED` sentinel means the buffer must not be read: the
    /// request is refused, not clamped, since a truncated path can name a different real file.
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
                KEY,
            )
        };
        assert!(!accepted, "the sentinel must refuse the request");
        assert_eq!(
            collect("trunc/A.wav").0,
            DELUGE_CHANNEL_UNKNOWN,
            "a refused request must not have been queued"
        );
    }

    /// A request reads only the candidate slots it was given, never past the
    /// buffer the caller sized.
    #[test]
    fn a_request_reads_only_the_candidate_slots_it_was_given() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        assert!(request_load("bound/A.wav", &["bound/A.wav"]));
        assert_eq!(collect("bound/A.wav").0, DELUGE_CHANNEL_PENDING);
    }

    // Ejection
    //
    // A result records nothing about its card, so one parsed before an ejection and collected after
    // the next insertion would apply the old card's geometry to the new card's file.
    // `deluge_card_flush` must cover both what was already published and what is still in flight.

    /// A result published before the ejection is `UNKNOWN` afterwards, so the collector re-requests
    /// against the card now in the slot.
    #[test]
    fn a_flush_discards_a_published_result() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        publish("eject/published.wav", LoadResult::failed(DELUGE_ERROR_NONE));
        assert_eq!(
            collect("eject/published.wav").0,
            DELUGE_CHANNEL_READY,
            "the result must be collectable before the ejection, or this test proves nothing"
        );

        publish("eject/published.wav", LoadResult::failed(DELUGE_ERROR_NONE));
        deluge_card_flush();
        let (status, out) = collect("eject/published.wav");
        assert_eq!(status, DELUGE_CHANNEL_UNKNOWN);
        assert_out_untouched(&out);
    }

    /// A load in flight when the card goes publishes afterwards and must not become collectable.
    ///
    /// The stamp must be the generation the load began under (sampled in `take_next_request`);
    /// stamping at publish time would read the post-flush value and let the stale result through.
    /// The test captures the generation before the flush to reproduce that.
    #[test]
    fn a_flush_drops_a_result_from_a_load_that_was_already_in_flight() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        // The loader picks the request up: `take_next_request` samples the generation here.
        block_on(mark_in_flight(Some(path_of("eject/inflight.wav"))));
        let began_under = current_generation();

        // The card is pulled mid-parse.
        deluge_card_flush();

        // ...and the parse finishes anyway. Its result must not become collectable.
        block_on(publish_result(
            path_of("eject/inflight.wav"),
            LoadResult::failed(DELUGE_ERROR_NONE),
            began_under,
        ));
        let (status, out) = collect("eject/inflight.wav");
        assert_eq!(
            status, DELUGE_CHANNEL_UNKNOWN,
            "a load that began on the previous card must not publish onto this one"
        );
        assert_out_untouched(&out);

        // Positive control: a check that dropped every publish after a flush would pass the
        // assertion above while wedging the loader for good.
        publish("eject/inflight.wav", LoadResult::failed(DELUGE_ERROR_NONE));
        assert_eq!(
            collect("eject/inflight.wav").0,
            DELUGE_CHANNEL_READY,
            "a load that began AFTER the flush must publish normally"
        );
    }

    /// A queued request does not survive the ejection: it named a path on a card that has gone.
    #[test]
    fn a_flush_discards_a_queued_request() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        assert!(request_load("eject/queued.wav", &["eject/queued.wav"]));
        assert_eq!(collect("eject/queued.wav").0, DELUGE_CHANNEL_PENDING);
        deluge_card_flush();
        assert_eq!(
            collect("eject/queued.wav").0,
            DELUGE_CHANNEL_UNKNOWN,
            "a flushed request must report UNKNOWN, which is what makes the collector ask again"
        );
    }

    /// The in-flight mark goes too; left behind, it would report `PENDING` for a load nothing will
    /// publish.
    #[test]
    fn a_flush_clears_the_in_flight_mark() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        block_on(mark_in_flight(Some(path_of("eject/mark.wav"))));
        assert_eq!(collect("eject/mark.wav").0, DELUGE_CHANNEL_PENDING);
        deluge_card_flush();
        assert_eq!(collect("eject/mark.wav").0, DELUGE_CHANNEL_UNKNOWN);
    }

    /// The loader keeps working after an ejection: a card swapped mid-session must still load
    /// samples.
    #[test]
    fn the_loader_still_accepts_work_after_a_flush() {
        let _guard = PIPELINE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        deluge_card_flush();
        assert!(request_load("eject/after.wav", &["eject/after.wav"]));
        assert_eq!(collect("eject/after.wav").0, DELUGE_CHANNEL_PENDING);
        // Drain it, so this test leaves the shared queue as it found it.
        deluge_card_flush();
    }
}

// The candidate buffer decoder

/// Builds the buffer `sample_build::candidates` would write: one NUL-terminated path per
/// `DELUGE_PATH_MAX` slot, the rest left as the caller had it.
fn candidate_buffer(paths: &[&str]) -> Vec<u8> {
    // Not zero: a slot the C side never wrote must not decode as an empty string.
    let mut buffer = vec![0xAAu8; paths.len() * DELUGE_PATH_MAX];
    for (slot, path) in paths.iter().enumerate() {
        let bytes = path.as_bytes();
        let base = slot * DELUGE_PATH_MAX;
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

/// An unterminated slot rejects the whole buffer, even when it is not the first: slot 0 is good,
/// slot 1 unterminated.
#[test]
fn an_unterminated_candidate_slot_is_refused_even_after_a_good_one() {
    let mut buffer = candidate_buffer(&["A.wav", "B.wav"]);
    // Scrub slot 1's terminator, leaving slot 0 intact.
    for byte in buffer[DELUGE_PATH_MAX..].iter_mut() {
        *byte = 0xAA;
    }
    assert!(decode_candidates(&buffer).is_none());
    // Slot 0 alone still decodes, so the refusal above is about slot 1.
    assert_eq!(
        decode_candidates(&buffer[..DELUGE_PATH_MAX]).unwrap()[0].as_str(),
        "A.wav"
    );
}

// The admission rule, shared with the C++ side

/// One case from `path_admission_corpus.inc`.
struct AdmissionCase {
    accepted: bool,
    bytes: Vec<u8>,
    label: String,
}

/// Parses the corpus the C++ side `#include`s.
///
/// It is the single source of truth for which paths the loader accepts: C++ checks it before
/// requesting and this side when the request arrives, so drift fails a test instead of producing
/// a load that is retried forever. Format: one macro call per line, `\xNN` the only escape.
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

    // An empty parse (a renamed macro, a reformatted file) would make every assertion vacuous.
    assert!(
        cases.len() >= 15,
        "the shared corpus parsed to only {} case(s); the file's format changed under this reader",
        cases.len()
    );
    cases
}

/// Every corpus case gets its declared verdict from both refusal points: `cstr_to_path` for the
/// nominal path and `decode_candidates` for each candidate slot. A case too long for a slot is
/// refused there as unterminated rather than over-length; either way it never becomes a load.
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

        let mut slot = vec![0xAAu8; DELUGE_PATH_MAX];
        let usable = case.bytes.len().min(DELUGE_PATH_MAX);
        slot[..usable].copy_from_slice(&case.bytes[..usable]);
        if case.bytes.len() < DELUGE_PATH_MAX {
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
