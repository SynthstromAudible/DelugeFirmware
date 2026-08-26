//! The property that makes this parser usable on the device: driven through
//! [`HeaderParser`], a header decode reads a handful of bytes, never the file.
//!
//! A prefix-only protocol cannot do that. Every chunk's payload sits between the
//! caller and the next chunk header, so "give me the first N bytes" walks the
//! caller straight through the audio — measured over `~/Deluge Backup`, the
//! required prefix was **100% of the file at the median, p90 and maximum**, up
//! to a 183 MB single allocation for one file in the icoustic golden fixture.
//! The device has 8 MB of SDRAM. Hence the windowed API, and hence this gate.
//!
//! Two halves:
//! * always-run synthetic files, including one with a 512 MB audio payload that
//!   is never materialised — proof the payload is stepped over by arithmetic
//!   rather than read;
//! * a corpus sweep gated on `DELUGE_AUDIO_CORPUS` (same convention as
//!   `corpus_differential`), which reads each real file through nothing but the
//!   windows the parser asks for and additionally asserts the streamed answer is
//!   identical to `parse`'s whole-file answer.
//!
//! ```text
//! DELUGE_AUDIO_CORPUS="$HOME/Deluge Backup,$HOME/deluge-aiff-corpus" \
//!     cargo test -p deluge_audio_format --test windowed_streaming -- --nocapture
//! ```

use deluge_audio_format::{parse, AudioHeader, FeedResult, HeaderParser, ParseError};
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

/// What a run of the state machine cost its caller.
#[derive(Debug, Default, Clone, Copy)]
struct Cost {
    /// Largest single window the parser asked for. This is the caller's peak
    /// buffer: it never has to hold two windows at once.
    max_window: u32,
    /// Total bytes handed to the parser across the whole decode.
    bytes_read: u64,
    /// How many times `feed` was called — a walk that failed to advance would
    /// show up here as a runaway.
    feeds: u32,
}

/// Drive a decode to completion, answering every request from `read`.
///
/// `read(start, len)` must return the bytes that exist at `start`, up to `len` —
/// fewer (or none) when the file ends first. That shortfall is the only way the
/// parser learns where the file stops.
fn drive(mut read: impl FnMut(u32, u32) -> Vec<u8>) -> (Result<AudioHeader, ParseError>, Cost) {
    let mut parser = HeaderParser::new();
    let mut cost = Cost::default();
    loop {
        let Some((start, len)) = parser.pending() else {
            unreachable!("a finished parse returns Done/Err from feed before pending() is asked");
        };
        cost.max_window = cost.max_window.max(len);
        let window = read(start, len);
        cost.bytes_read += window.len() as u64;
        cost.feeds += 1;
        assert!(
            cost.feeds < 100_000,
            "runaway walk: {} feeds without finishing",
            cost.feeds
        );
        match parser.feed(&window, start) {
            Ok(FeedResult::Done(header)) => return (Ok(header), cost),
            Ok(FeedResult::Need { .. }) => {}
            Err(error) => return (Err(error), cost),
        }
    }
}

/// Read windows out of an in-memory file image.
fn from_bytes(bytes: &[u8]) -> impl FnMut(u32, u32) -> Vec<u8> + '_ {
    move |start, len| {
        let from = (start as usize).min(bytes.len());
        let to = from.saturating_add(len as usize).min(bytes.len());
        bytes[from..to].to_vec()
    }
}

// ---------------------------------------------------------------------------
// Synthetic files — always run.
// ---------------------------------------------------------------------------

fn le32(v: u32) -> [u8; 4] {
    v.to_le_bytes()
}

/// `fmt ` + `data` + `smpl`, with the data chunk's payload declared as
/// `audio_len` bytes but only `present` of them actually stored. The `smpl`
/// chunk sits AFTER the audio — the layout 35% of the real corpus uses, and the
/// one a prefix-only protocol can only reach by materialising everything in
/// between.
fn wav_with_trailing_smpl(audio_len: u32, present: u32) -> Vec<u8> {
    let mut chunks: Vec<u8> = Vec::new();
    chunks.extend_from_slice(b"fmt ");
    chunks.extend_from_slice(&le32(16));
    chunks.extend_from_slice(&1u16.to_le_bytes()); // PCM
    chunks.extend_from_slice(&1u16.to_le_bytes()); // mono
    chunks.extend_from_slice(&le32(44100));
    chunks.extend_from_slice(&le32(88200));
    chunks.extend_from_slice(&2u16.to_le_bytes());
    chunks.extend_from_slice(&16u16.to_le_bytes());

    chunks.extend_from_slice(b"data");
    chunks.extend_from_slice(&le32(audio_len));
    chunks.resize(chunks.len() + present as usize, 0);

    let mut smpl: Vec<u8> = Vec::new();
    smpl.extend_from_slice(&le32(0)); // manufacturer
    smpl.extend_from_slice(&le32(0)); // product
    smpl.extend_from_slice(&le32(0)); // sample period
    smpl.extend_from_slice(&le32(60)); // MIDI unity note
    smpl.extend_from_slice(&le32(0)); // pitch fraction
    smpl.extend_from_slice(&le32(0)); // SMPTE format
    smpl.extend_from_slice(&le32(0)); // SMPTE offset
    smpl.extend_from_slice(&le32(1)); // one loop
    smpl.extend_from_slice(&le32(0)); // sampler data
    smpl.extend_from_slice(&le32(0)); // loop id
    smpl.extend_from_slice(&le32(0)); // loop type
    smpl.extend_from_slice(&le32(1234)); // loop start
    smpl.extend_from_slice(&le32(5678)); // loop end
    smpl.extend_from_slice(&le32(0)); // fraction
    smpl.extend_from_slice(&le32(0)); // play count

    let mut file: Vec<u8> = Vec::new();
    file.extend_from_slice(b"RIFF");
    file.extend_from_slice(&le32(4 + chunks.len() as u32 + 8 + smpl.len() as u32));
    file.extend_from_slice(b"WAVE");
    file.extend_from_slice(&chunks);
    file.extend_from_slice(b"smpl");
    file.extend_from_slice(&le32(smpl.len() as u32));
    file.extend_from_slice(&smpl);
    file
}

#[test]
fn a_half_gigabyte_of_audio_is_never_read() {
    // The whole point, stated as a test: a 512 MB audio payload that exists only
    // as a declared length. Nothing materialises it, and the parser still walks
    // past it to the `smpl` chunk on the far side.
    const AUDIO_LEN: u32 = 512 * 1024 * 1024;
    let head = wav_with_trailing_smpl(AUDIO_LEN, 0);
    let data_payload_start = 44u32; // 12 top + 8+16 fmt + 8 data header
    let tail_start = data_payload_start + AUDIO_LEN;

    // A file that is a head, a hole, and a tail — the hole reads as zeros and is
    // never allocated. If the parser ever asked for a window inside it, the
    // assertion below would fire.
    let (result, cost) = drive(|start, len| {
        assert!(
            start < data_payload_start || start >= tail_start,
            "asked for {len} bytes at {start}, inside the audio payload"
        );
        let mut window = Vec::new();
        for offset in start..start.saturating_add(len) {
            if offset < data_payload_start {
                match head.get(offset as usize) {
                    Some(&byte) => window.push(byte),
                    None => break,
                }
            } else if offset >= tail_start {
                match head.get((offset - AUDIO_LEN) as usize) {
                    Some(&byte) => window.push(byte),
                    None => break,
                }
            } else {
                window.push(0);
            }
        }
        window
    });

    let header = result.expect("a 512 MB file's header decodes from a handful of bytes");
    assert_eq!(header.audio_data_start_pos_bytes, data_payload_start);
    assert_eq!(header.audio_data_length_bytes, AUDIO_LEN);
    // Reached across the hole: the trailing metadata a prefix-only protocol
    // could only see by buffering half a gigabyte first.
    assert_eq!(header.file_loop_start_samples, 1234);
    assert_eq!(header.file_loop_end_samples, 5678);
    assert_eq!(header.midi_note, Some(60.0));

    assert!(
        cost.max_window <= deluge_audio_format::MAX_WINDOW_BYTES,
        "asked for a {}-byte window",
        cost.max_window
    );
    assert!(
        cost.bytes_read < 1024,
        "read {} bytes to decode a 512 MB file's header",
        cost.bytes_read
    );
}

#[test]
fn a_truncated_file_ends_the_walk_at_its_real_end() {
    // The audio chunk declares 4096 bytes but the file holds 100 and stops. A
    // streaming caller's short window says so, and the walk ends there rather
    // than asking for bytes that will never exist.
    let file = wav_with_trailing_smpl(4096, 100);
    let real_end = 44 + 100;
    let (result, cost) = drive(from_bytes(&file[..real_end]));
    // Everything the header itself said is still known; only the trailing
    // metadata, which genuinely isn't in the file, is missing.
    let header = result.expect("a truncated payload is not a corrupt header");
    assert_eq!(header.audio_data_length_bytes, 4096);
    assert_eq!(header.file_loop_start_samples, 0);
    assert!(cost.max_window <= deluge_audio_format::MAX_WINDOW_BYTES);
}

#[test]
fn an_oversized_window_is_consumed_in_one_feed() {
    // A caller holding a whole SD cluster should not have to hand it back one
    // field at a time. Feeding a superset window must satisfy every request it
    // covers before the parser asks again.
    let file = wav_with_trailing_smpl(64, 64);
    let mut parser = HeaderParser::new();
    let mut feeds = 0;
    let header = loop {
        let Some((start, _len)) = parser.pending() else {
            unreachable!()
        };
        feeds += 1;
        let from = (start as usize).min(file.len());
        match parser.feed(&file[from..], start) {
            Ok(FeedResult::Done(header)) => break header,
            Ok(FeedResult::Need { .. }) => {}
            Err(error) => panic!("{error:?}"),
        }
    };
    assert_eq!(header.file_loop_end_samples, 5678);
    // Two feeds, not one: the first consumes the entire window, chunk after
    // chunk, and the second is the caller saying "there is nothing at the offset
    // after the last chunk" — the end-of-file signal, which no window can carry.
    assert_eq!(
        feeds, 2,
        "a window covering the file should need one feed plus the EOF answer"
    );
}

#[test]
fn a_window_that_never_reaches_the_request_terminates() {
    // Contract violation, deliberately: answer every request with bytes from
    // somewhere else entirely. The parser must still return — a caller bug must
    // not become a device hang.
    let file = wav_with_trailing_smpl(64, 64);
    let (_result, cost) = drive(|_start, _len| file[..4].to_vec());
    assert!(cost.feeds < 100, "{} feeds before giving up", cost.feeds);
}

// ---------------------------------------------------------------------------
// Corpus sweep — gated on DELUGE_AUDIO_CORPUS.
// ---------------------------------------------------------------------------

fn walk_audio_files(root: &str, out: &mut Vec<PathBuf>) {
    fn recurse(dir: &Path, out: &mut Vec<PathBuf>) {
        let Ok(entries) = std::fs::read_dir(dir) else {
            return;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                recurse(&path, out);
            } else if let Some(ext) = path.extension().and_then(|e| e.to_str()) {
                let ext = ext.to_ascii_lowercase();
                if matches!(ext.as_str(), "wav" | "aif" | "aiff") {
                    out.push(path);
                }
            }
        }
    }
    recurse(Path::new(root), out);
}

#[test]
fn windows_stay_bounded_over_the_corpus() {
    let Ok(roots) = std::env::var("DELUGE_AUDIO_CORPUS") else {
        eprintln!("DELUGE_AUDIO_CORPUS unset - skipping the corpus sweep");
        return;
    };

    let mut paths = Vec::new();
    for root in roots.split(',') {
        walk_audio_files(root.trim(), &mut paths);
    }
    paths.sort();
    assert!(!paths.is_empty(), "no audio files under {roots}");

    let mut worst_window = (0u32, PathBuf::new());
    let mut worst_read = (0u64, PathBuf::new());
    let mut decoded = 0usize;

    for path in &paths {
        let Ok(mut file) = File::open(path) else {
            continue;
        };
        let whole = std::fs::read(path).expect("corpus file readable");

        // Read ONLY what the parser asks for, straight off the file.
        let (streamed, cost) = drive(|start, len| {
            let mut window = vec![0u8; len as usize];
            if file.seek(SeekFrom::Start(u64::from(start))).is_err() {
                return Vec::new();
            }
            let mut filled = 0;
            while filled < window.len() {
                match file.read(&mut window[filled..]) {
                    Ok(0) | Err(_) => break,
                    Ok(n) => filled += n,
                }
            }
            window.truncate(filled);
            window
        });

        assert!(
            cost.max_window <= deluge_audio_format::MAX_WINDOW_BYTES,
            "{}: asked for a {}-byte window, over the {}-byte bound",
            path.display(),
            cost.max_window,
            deluge_audio_format::MAX_WINDOW_BYTES
        );

        // The streamed decode must agree with the whole-file one, field for
        // field. Files the whole-file parser rejects are expected to be rejected
        // by the streaming one too, but the two protocols legitimately differ on
        // what a short file means, so only successes are compared.
        if let Ok(expected) = parse(&whole) {
            assert_eq!(
                streamed.as_ref().ok(),
                Some(&expected),
                "{}: streamed decode differs from the whole-file decode",
                path.display()
            );
            decoded += 1;
        }

        if cost.max_window > worst_window.0 {
            worst_window = (cost.max_window, path.clone());
        }
        if cost.bytes_read > worst_read.0 {
            worst_read = (cost.bytes_read, path.clone());
        }
    }

    eprintln!("swept {} files, {decoded} decoded", paths.len());
    eprintln!(
        "largest window: {} bytes (bound {}) - {}",
        worst_window.0,
        deluge_audio_format::MAX_WINDOW_BYTES,
        worst_window.1.display()
    );
    eprintln!(
        "most bytes read for one header: {} - {}",
        worst_read.0,
        worst_read.1.display()
    );
    assert!(decoded > 0, "no corpus file decoded");
}
