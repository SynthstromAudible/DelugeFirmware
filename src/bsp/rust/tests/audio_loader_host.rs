//! Host unit tests for `audio_loader::load_once` — the async audio-file load
//! driver, exercised against an in-memory [`FakeOps`] double rather than the real
//! `deluge_audio_load_*` C ABI and a real card.
//!
//! See `src/audio_loader.rs`'s module doc ("Why the driver is generic over
//! `LoadOps`") for why the operations are injected: `host_app` (which links the
//! host-built C++ app closure) and this crate's integration tests do not
//! co-compile, so a test binary cannot call the real symbols, and defining fakes
//! with those names would collide with the real ones wherever the C++ app *is*
//! linked.
//!
//! `deluge-bsp-rust` is bin-only (no `[lib]` target — see `Cargo.toml`), so this
//! recompiles `src/audio_loader.rs` unmodified into this test binary via
//! `#[path]`, the same convention `tests/streaming_fill_host.rs` and
//! `tests/owner_host.rs` use. Only the `async_audio_load`-gated driver is needed
//! here — the `prod` submodule (the real `extern "C"` wiring, plus the efatfs
//! backing) is gated on device/`host_app` and stays uncompiled, so this test
//! never needs the C++ app linked.
//!
//! What these tests are actually pinning, in one line each: the build is adopted
//! by `begin` and must be released by exactly one of `finish`/`abandon` on every
//! path; the efatfs handle must be closed on every path; and a zero-length window
//! request must still be read and fed, or the parse loop never terminates.
#![cfg(feature = "async_audio_load")]
#![cfg(not(target_os = "none"))]

use core::ffi::c_void;
use std::cell::{Cell, RefCell};

#[path = "../src/audio_loader.rs"]
mod audio_loader;

use audio_loader::{LoadGeometry, LoadOps, LoadRequest, decode_candidates, load_once};

/// Deluge `Error` codes, by number — the same identities `audio_load_abi.cpp`
/// pins with `static_assert`s.
const ERROR_NONE: u8 = 0;
const ERROR_FILE_CORRUPTED: u8 = 5;
const ERROR_FILE_UNREADABLE: u8 = 6;
const ERROR_FILE_UNSUPPORTED: u8 = 7;

/// The stride `deluge_audio_load_candidates` writes its slots at
/// (`DELUGE_MAX_PATH`).
const MAX_PATH: usize = 256;

/// One call `FakeOps` observed, in order. Only the calls whose ORDER is part of
/// the contract are recorded: alternate-dir resolution must precede
/// `candidates`, and the read stream must be bound before anything can publish.
#[derive(Debug, PartialEq, Eq, Clone)]
enum Call {
    ResolveAlternateDir,
    Candidates,
    Open(String),
    SizeOf,
    /// The two path arguments `deluge_audio_load_begin` takes: the nominal path
    /// and the alternate location (empty when the regular path won).
    Begin {
        path: String,
        alternate: String,
    },
    BindReadStream(String),
    Read {
        offset: u32,
        len: usize,
    },
    ApplyGeometry,
    Finish(u8),
    Abandon,
    Close,
}

/// A non-null opaque stand-in for `DelugeSampleBuild*`. Never dereferenced — the
/// driver only threads it through.
const FAKE_BUILD: *mut c_void = 0x1000 as *mut c_void;

/// Counting double for [`LoadOps`].
///
/// `open_handles` is the LIVE count: incremented by `open`, decremented by
/// `close`, and asserted zero at the end of every test because the efatfs handle
/// table has only `MAX_HANDLES` (128) slots and a resident `Sample` pins one for
/// its whole lifetime — a leaked handle is silent capacity loss, not an error
/// anything reports.
///
/// `build_live` is the ownership check: `begin` sets it, `finish`/`abandon` clear
/// it, and both panic if it is already clear. Together with the `finished` /
/// `abandoned` counts that turns "exactly one of finish or abandon, on every
/// path" from a claim into an assertion.
struct FakeOps {
    /// The file's bytes, served from memory.
    bytes: Vec<u8>,
    /// What `candidates` proposes, in try order.
    candidates: Vec<String>,
    /// `candidates` reports the C side's buffer untrustworthy.
    candidates_untrustworthy: bool,
    /// Paths `open` refuses; every other path in `candidates` opens.
    unopenable: Vec<String>,
    /// What `bind_read_stream` returns.
    bind_error: u8,
    /// `apply_geometry` refuses (as it does for an invalid `raw_data_format`).
    refuse_geometry: bool,
    /// `size_of` fails.
    size_fails: bool,

    calls: RefCell<Vec<Call>>,
    finished: Cell<u32>,
    abandoned: Cell<u32>,
    open_handles: Cell<i32>,
    next_handle: Cell<u32>,
    build_live: Cell<bool>,
    begun: Cell<u32>,
    alternate_resolved: Cell<bool>,
    last_geometry: RefCell<Option<LoadGeometry>>,
}

impl FakeOps {
    fn new() -> Self {
        Self {
            bytes: Vec::new(),
            candidates: vec![String::from("A.wav")],
            candidates_untrustworthy: false,
            unopenable: Vec::new(),
            bind_error: ERROR_NONE,
            refuse_geometry: false,
            size_fails: false,
            calls: RefCell::new(Vec::new()),
            finished: Cell::new(0),
            abandoned: Cell::new(0),
            open_handles: Cell::new(0),
            next_handle: Cell::new(7),
            build_live: Cell::new(false),
            begun: Cell::new(0),
            alternate_resolved: Cell::new(false),
            last_geometry: RefCell::new(None),
        }
    }

    fn with_bytes(mut self, bytes: Vec<u8>) -> Self {
        self.bytes = bytes;
        self
    }

    fn with_open_failing(mut self) -> Self {
        self.unopenable = self.candidates.clone();
        self
    }

    fn with_candidates(mut self, candidates: &[&str]) -> Self {
        self.candidates = candidates.iter().map(|c| String::from(*c)).collect();
        self
    }

    fn with_untrustworthy_candidates(mut self) -> Self {
        self.candidates_untrustworthy = true;
        self
    }

    fn with_bind_failing(mut self) -> Self {
        self.bind_error = ERROR_FILE_UNREADABLE;
        self
    }

    fn with_geometry_refused(mut self) -> Self {
        self.refuse_geometry = true;
        self
    }

    fn with_size_failing(mut self) -> Self {
        self.size_fails = true;
        self
    }

    fn finished(&self) -> u32 {
        self.finished.get()
    }

    fn abandoned(&self) -> u32 {
        self.abandoned.get()
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

    /// The single `finish` error code, if `finish` was called exactly once.
    fn finish_error(&self) -> Option<u8> {
        let codes: Vec<u8> = self
            .calls()
            .into_iter()
            .filter_map(|c| match c {
                Call::Finish(code) => Some(code),
                _ => None,
            })
            .collect();
        match codes.as_slice() {
            [code] => Some(*code),
            _ => None,
        }
    }

    /// Assert the invariants that hold at the end of EVERY load, whatever its
    /// outcome: no handle left open, no build left adopted, and at most one of
    /// finish/abandon.
    fn assert_settled(&self) {
        assert_eq!(
            self.open_handles(),
            0,
            "efatfs handle leaked: {:?}",
            self.calls()
        );
        assert!(
            !self.build_live.get(),
            "build left adopted: {:?}",
            self.calls()
        );
        assert!(
            self.finished() + self.abandoned() <= 1,
            "build consumed more than once: {:?}",
            self.calls()
        );
        // Every build that was begun was consumed exactly once.
        assert_eq!(
            self.begun.get(),
            self.finished() + self.abandoned(),
            "begun builds and finish/abandon calls disagree: {:?}",
            self.calls()
        );
    }
}

impl LoadOps for FakeOps {
    async fn ensure_alternate_dir_resolved(&self) {
        self.record(Call::ResolveAlternateDir);
        self.alternate_resolved.set(true);
    }

    fn candidates(&self, _path: &str) -> Option<Vec<String>> {
        self.record(Call::Candidates);
        assert!(
            self.alternate_resolved.get(),
            "candidates asked for before the alternate dir was resolved: a collect-media song's \
             first sample would silently miss its alternate-dir candidates"
        );
        if self.candidates_untrustworthy {
            return None;
        }
        Some(self.candidates.clone())
    }

    async fn open(&self, path: &str) -> Option<u32> {
        self.record(Call::Open(String::from(path)));
        if self.unopenable.iter().any(|p| p == path) {
            return None;
        }
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
        Some(self.bytes.len() as u32)
    }

    fn begin(&self, path: &str, alternate: &str, size_bytes: u64) -> *mut c_void {
        self.record(Call::Begin {
            path: String::from(path),
            alternate: String::from(alternate),
        });
        assert_eq!(size_bytes, self.bytes.len() as u64);
        self.begun.set(self.begun.get() + 1);
        self.build_live.set(true);
        FAKE_BUILD
    }

    async fn bind_read_stream(&self, build: *mut c_void, path: &str) -> u8 {
        assert_eq!(build, FAKE_BUILD);
        assert!(self.build_live.get(), "bind on a consumed build");
        self.record(Call::BindReadStream(String::from(path)));
        self.bind_error
    }

    async fn read_at(&self, _handle: u32, offset: u32, buf: &mut [u8]) -> bool {
        self.record(Call::Read {
            offset,
            len: buf.len(),
        });
        // Mirrors `efatfs_fs::read_at`: an empty fill trivially succeeds,
        // whatever the offset. This is the case the driver MUST still feed.
        if buf.is_empty() {
            return true;
        }
        let start = offset as usize;
        let Some(end) = start.checked_add(buf.len()) else {
            return false;
        };
        if end > self.bytes.len() {
            return false; // A real read failure: the parser clamps inside the file.
        }
        buf.copy_from_slice(&self.bytes[start..end]);
        true
    }

    fn apply_geometry(&self, build: *mut c_void, geometry: &LoadGeometry) -> bool {
        assert_eq!(build, FAKE_BUILD);
        assert!(self.build_live.get(), "apply_geometry on a consumed build");
        self.record(Call::ApplyGeometry);
        *self.last_geometry.borrow_mut() = Some(*geometry);
        !self.refuse_geometry
    }

    fn finish(&self, build: *mut c_void, error: u8) -> bool {
        assert_eq!(build, FAKE_BUILD);
        assert!(self.build_live.get(), "finish on an already-consumed build");
        self.build_live.set(false);
        self.record(Call::Finish(error));
        self.finished.set(self.finished.get() + 1);
        error == ERROR_NONE
    }

    fn abandon(&self, build: *mut c_void) {
        assert_eq!(build, FAKE_BUILD);
        assert!(
            self.build_live.get(),
            "abandon on an already-consumed build"
        );
        self.build_live.set(false);
        self.record(Call::Abandon);
        self.abandoned.set(self.abandoned.get() + 1);
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

fn request(path: &str) -> LoadRequest {
    LoadRequest {
        path: String::from(path),
    }
}

// ── Ownership: exactly one of finish/abandon, and never a leaked handle ──────

/// Nothing opened means nothing was ever adopted: `begin` takes the resolved
/// size, so it cannot run before a candidate opens. There is therefore no build
/// to abandon here — asserting an `abandon` would be asserting a double-free of
/// an object that does not exist. What must hold is that no handle leaked and no
/// build was begun.
#[test]
fn a_failed_open_never_begins_a_build() {
    let ops = FakeOps::new().with_bytes(minimal_wav()).with_open_failing();
    let published = block_on(load_once(&ops, request("A.wav")));
    assert!(!published);
    assert_eq!(ops.finished(), 0);
    assert_eq!(ops.abandoned(), 0);
    assert_eq!(ops.open_handles(), 0);
    assert!(!ops.calls().iter().any(|c| matches!(c, Call::Begin { .. })));
    ops.assert_settled();
}

/// A build that will never carry a parse result must be ABANDONED — never
/// dropped on the floor, or the manager keeps a zero-geometry `Sample` holding a
/// reason it can never be evicted under. Binding the read stream is the first
/// thing that can fail after `begin`.
#[test]
fn a_failed_read_stream_bind_abandons_the_build() {
    let ops = FakeOps::new().with_bytes(minimal_wav()).with_bind_failing();
    let published = block_on(load_once(&ops, request("A.wav")));
    assert!(!published);
    assert_eq!(ops.finished(), 0);
    assert_eq!(ops.abandoned(), 1);
    assert_eq!(ops.open_handles(), 0);
    ops.assert_settled();
}

/// A refused geometry (`apply_geometry` returning false) leaves the build at zero
/// geometry with nothing written. Publishing it would be exactly the silent
/// wrong-render that refusal exists to prevent, so it must be abandoned, not
/// finished.
#[test]
fn a_refused_geometry_abandons_the_build() {
    let ops = FakeOps::new()
        .with_bytes(minimal_wav())
        .with_geometry_refused();
    let published = block_on(load_once(&ops, request("A.wav")));
    assert!(!published);
    assert_eq!(ops.finished(), 0);
    assert_eq!(ops.abandoned(), 1);
    assert_eq!(ops.open_handles(), 0);
    ops.assert_settled();
}

/// A read that fails mid-parse (a pulled card) abandons rather than publishing a
/// half-parsed build, and still closes the handle.
#[test]
fn a_truncated_file_fails_the_parse_and_releases_everything() {
    // The file ends inside its own `fmt ` payload, so the read for that window is
    // inside the size the parser was given and genuinely comes back short.
    let mut truncated = minimal_wav();
    truncated.truncate(24);
    let ops = FakeOps::new().with_bytes(truncated);
    let published = block_on(load_once(&ops, request("A.wav")));
    assert!(!published);
    assert_eq!(ops.open_handles(), 0);
    ops.assert_settled();
    assert_eq!(ops.finished() + ops.abandoned(), 1);
}

/// A `size_of` failure ends the load before `begin`, and still closes the handle
/// the (successful) open took.
#[test]
fn a_failed_size_closes_the_handle_without_beginning_a_build() {
    let ops = FakeOps::new().with_bytes(minimal_wav()).with_size_failing();
    let published = block_on(load_once(&ops, request("A.wav")));
    assert!(!published);
    assert_eq!(ops.open_handles(), 0);
    assert!(!ops.calls().iter().any(|c| matches!(c, Call::Begin { .. })));
    ops.assert_settled();
}

// ── The happy path ──────────────────────────────────────────────────────────

/// The happy path drives the parser to `Done`, applies the geometry and finishes
/// exactly once — after binding the Sample's own read stream, without which the
/// published sample would load cleanly and play silence.
#[test]
fn a_good_file_finishes_exactly_once() {
    let ops = FakeOps::new().with_bytes(minimal_wav());
    let published = block_on(load_once(&ops, request("A.wav")));
    assert!(published);
    assert_eq!(ops.finished(), 1);
    assert_eq!(ops.abandoned(), 0);
    assert_eq!(ops.open_handles(), 0);
    assert_eq!(ops.finish_error(), Some(ERROR_NONE));
    ops.assert_settled();

    let calls = ops.calls();
    let bind = calls
        .iter()
        .position(|c| matches!(c, Call::BindReadStream(_)))
        .expect("the Sample's own read stream must be bound before it is published");
    let finish = calls
        .iter()
        .position(|c| matches!(c, Call::Finish(_)))
        .expect("finish");
    assert!(bind < finish);
    assert_eq!(
        calls[bind],
        Call::BindReadStream(String::from("A.wav")),
        "the read stream binds to the path the bytes were actually resolved at"
    );
}

/// The geometry handed across the ABI is the parsed header, field for field. A
/// wrong or missed field here loads without any error and renders wrong.
#[test]
fn the_parsed_geometry_reaches_apply_geometry() {
    let ops = FakeOps::new().with_bytes(minimal_wav());
    assert!(block_on(load_once(&ops, request("A.wav"))));
    let geometry = ops.last_geometry.borrow().expect("geometry applied");
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

/// Every window the parser asks for stays inside the size it was given
/// (`start + len <= size`), which is exactly what makes
/// [`LoadOps::read_at`]'s full-fill-or-`false` contract usable here: a short read
/// can only be a real read failure, never an end of file that a byte count would
/// have had to distinguish. `read_at` cannot report both, so if the parser could
/// ask past the end this driver would have no way to tell the two apart.
#[test]
fn every_window_stays_inside_the_file() {
    let bytes = minimal_wav();
    let ops = FakeOps::new().with_bytes(bytes.clone());
    assert!(block_on(load_once(&ops, request("A.wav"))));
    assert!(
        ops.reads()
            .iter()
            .all(|(offset, len)| *offset as usize + *len <= bytes.len()),
        "the parser must never name a byte past the size it was given; reads were {:?}",
        ops.reads()
    );
    // And the audio payload is stepped over by arithmetic, never read: the walk
    // stops at the `data` chunk HEADER (offset 36, 8 bytes), it does not read the
    // 8 audio bytes behind it.
    assert_eq!(ops.reads(), vec![(0, 12), (12, 8), (20, 16), (36, 8)]);
}

/// A zero-length window request must still be READ and FED. The parser resolves
/// the end of the file inside `feed` and never terminates without a `feed` call,
/// so a driver that treated `len == 0` as "nothing to do" and skipped it would
/// spin in the parse loop forever — this test would hang rather than fail.
///
/// A zero-byte file is the case that produces one: `pending` clamps the container
/// header request to what remains, which is nothing. In production
/// `deluge_audio_load_begin` refuses a zero-byte file (`FILE_CORRUPTED`) before
/// the parser is ever built, so this is defence in depth on the driver's own
/// loop rather than a reachable card state — which is precisely why it needs a
/// test: nothing else would notice if the loop stopped feeding.
#[test]
fn a_zero_length_window_is_still_read_and_fed() {
    let ops = FakeOps::new().with_bytes(Vec::new());
    let published = block_on(load_once(&ops, request("A.wav")));
    assert!(!published);
    assert_eq!(
        ops.reads(),
        vec![(0, 0)],
        "the clamped-to-zero window must be read, then fed"
    );
    // Fed, so the parse reached a verdict and the build was consumed exactly once
    // rather than being left adopted by a loop that never ended.
    ops.assert_settled();
    assert_eq!(ops.finished() + ops.abandoned(), 1);
}

// ── Parse failures keep the C++ side's error identity ───────────────────────

/// A file that is not audio at all is reported as `FILE_UNSUPPORTED` through
/// `finish` — the same code the synchronous path produced, which is what
/// `sample_browser.cpp` reads as "not a wavetable, try it as a sample". A parse
/// RESULT exists, so this goes through `finish`, not `abandon`.
#[test]
fn a_non_audio_file_finishes_with_file_unsupported() {
    let ops = FakeOps::new().with_bytes(b"NOTAUDIOATALLREALLY!".to_vec());
    let published = block_on(load_once(&ops, request("A.wav")));
    assert!(!published);
    assert_eq!(ops.finished(), 1);
    assert_eq!(ops.abandoned(), 0);
    assert_eq!(ops.finish_error(), Some(ERROR_FILE_UNSUPPORTED));
    ops.assert_settled();
}

/// A RIFF/WAVE container with no `data` chunk is malformed, not unsupported:
/// `FILE_CORRUPTED`.
#[test]
fn a_wav_without_audio_data_finishes_with_file_corrupted() {
    let mut wav = minimal_wav();
    wav.truncate(36); // Drop the whole `data` chunk, keeping `fmt `.
    let riff_size = (wav.len() - 8) as u32;
    wav[4..8].copy_from_slice(&riff_size.to_le_bytes());
    let ops = FakeOps::new().with_bytes(wav);
    let published = block_on(load_once(&ops, request("A.wav")));
    assert!(!published);
    assert_eq!(ops.finish_error(), Some(ERROR_FILE_CORRUPTED));
    ops.assert_settled();
}

// ── Candidate policy ────────────────────────────────────────────────────────

/// Discovery precedes candidates: the alternate-dir status must be resolved (I/O)
/// before the C side is asked which paths to try, or a collect-media song's first
/// sample resolves to the regular path only and silently fails to load where the
/// synchronous path finds it.
#[test]
fn the_alternate_dir_is_resolved_before_candidates_are_asked_for() {
    let ops = FakeOps::new().with_bytes(minimal_wav());
    assert!(block_on(load_once(&ops, request("A.wav"))));
    let calls = ops.calls();
    let resolve = calls
        .iter()
        .position(|c| *c == Call::ResolveAlternateDir)
        .expect("resolve");
    let candidates = calls
        .iter()
        .position(|c| *c == Call::Candidates)
        .expect("candidates");
    assert!(resolve < candidates, "calls were {calls:?}");
}

/// Candidates are tried in order, and the FIRST one that opens wins. The winning
/// path is reported to `begin` as the alternate location when it is not the
/// nominal path — that is exactly `usingAlternateLocation` — and it is the path
/// the Sample's own read stream binds to.
#[test]
fn the_first_candidate_that_opens_becomes_the_alternate_location() {
    let mut ops = FakeOps::new()
        .with_bytes(minimal_wav())
        .with_candidates(&["SAMPLES/A.wav", "SAMPLES/SONG/A.wav"]);
    ops.unopenable = vec![String::from("SAMPLES/A.wav")];
    let published = block_on(load_once(&ops, request("SAMPLES/A.wav")));
    assert!(published);

    let calls = ops.calls();
    assert!(calls.contains(&Call::Begin {
        path: String::from("SAMPLES/A.wav"),
        alternate: String::from("SAMPLES/SONG/A.wav"),
    }));
    assert!(calls.contains(&Call::BindReadStream(String::from("SAMPLES/SONG/A.wav"))));
    // The refused candidate was tried first and took no handle with it.
    assert_eq!(
        calls
            .iter()
            .filter(|c| matches!(c, Call::Open(_)))
            .cloned()
            .collect::<Vec<_>>(),
        vec![
            Call::Open(String::from("SAMPLES/A.wav")),
            Call::Open(String::from("SAMPLES/SONG/A.wav")),
        ]
    );
    ops.assert_settled();
}

/// The regular path winning means NO alternate location — an empty string, which
/// is what `deluge_audio_load_begin` documents for "resolved at `file_path`
/// directly" (and never NULL).
#[test]
fn the_regular_path_winning_reports_no_alternate_location() {
    let ops = FakeOps::new()
        .with_bytes(minimal_wav())
        .with_candidates(&["SAMPLES/A.wav"]);
    assert!(block_on(load_once(&ops, request("SAMPLES/A.wav"))));
    assert!(ops.calls().contains(&Call::Begin {
        path: String::from("SAMPLES/A.wav"),
        alternate: String::new(),
    }));
}

/// An untrustworthy candidate buffer fails the load outright rather than opening
/// whatever a silent truncation would have left behind — which could be a
/// DIFFERENT real file sitting at the truncated path.
#[test]
fn an_untrustworthy_candidate_list_opens_nothing() {
    let ops = FakeOps::new()
        .with_bytes(minimal_wav())
        .with_untrustworthy_candidates();
    let published = block_on(load_once(&ops, request("A.wav")));
    assert!(!published);
    assert!(!ops.calls().iter().any(|c| matches!(c, Call::Open(_))));
    ops.assert_settled();
}

// ── The candidate decoder's truncation contract ─────────────────────────────

/// Build the buffer `deluge_audio_load_candidates` would have written: one
/// NUL-terminated path per `DELUGE_MAX_PATH` slot, the rest of the buffer left as
/// the caller had it.
fn candidate_buffer(paths: &[&str], capacity: usize) -> Vec<u8> {
    // 0xAA, not zero: a slot the C side never wrote must not accidentally decode
    // as an empty string.
    let mut buffer = vec![0xAAu8; capacity * MAX_PATH];
    for (slot, path) in paths.iter().enumerate() {
        let bytes = path.as_bytes();
        let base = slot * MAX_PATH;
        buffer[base..base + bytes.len()].copy_from_slice(bytes);
        buffer[base + bytes.len()] = 0;
    }
    buffer
}

/// The count `deluge_audio_load_candidates` returns is a TOTAL and can exceed the
/// capacity it was given — only the first `capacity` slots were written. Reading
/// `0..count` would decode slots the C side never touched.
#[test]
fn a_count_over_capacity_decodes_only_the_slots_that_were_written() {
    let buffer = candidate_buffer(&["A.wav", "B.wav", "C.wav"], 3);
    let decoded = decode_candidates(&buffer, 5).expect("a real count, not the sentinel");
    assert_eq!(decoded, vec!["A.wav", "B.wav", "C.wav"]);
}

/// The `DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED` sentinel means `out` is not
/// trustworthy at all and must not be read.
#[test]
fn the_truncation_sentinel_yields_no_candidates() {
    let buffer = candidate_buffer(&["A.wav"], 3);
    assert!(decode_candidates(&buffer, 0xFFFF_FFFF).is_none());
}

/// A count below capacity decodes exactly that many, leaving the untouched slots
/// alone.
#[test]
fn a_short_count_decodes_only_that_many() {
    let buffer = candidate_buffer(&["A.wav"], 3);
    assert_eq!(decode_candidates(&buffer, 1).unwrap(), vec!["A.wav"]);
    assert!(decode_candidates(&buffer, 0).unwrap().is_empty());
}
