//! Corpus differential: this crate decoding directly against the C++ entry points
//! (`parseSampleHeader`/`parseWaveTableHeader`) over real WAV/AIFF files.
//!
//! Both sides use this crate's decoder, so the test catches defects between them rather than in
//! the decode itself. Each entry point is compared against the walk it runs:
//!
//! * **Sample**: [`parse`] (prefix mode, [`WalkGoal::WholeHeader`]) against the C++ size-aware
//!   windowed walk. Covers the C++ adapter's window buffering and short-tail handling, and the
//!   marshalling across the C ABI.
//! * **WaveTable**: a [`WalkGoal::AudioOnly`] walk off the byte slice against the same walk
//!   through the C ABI, plus wavetable acceptance at both `insist` settings. The goals must match;
//!   see `decode_as_wavetable_walk`.
//!
//! Gated on `DELUGE_AUDIO_CORPUS`, a comma-separated list of roots walked recursively for
//! `.wav`/`.aif`/`.aiff` files; unset, the test is skipped:
//!
//! ```text
//! DELUGE_AUDIO_CORPUS="$HOME/Deluge Backup,$HOME/deluge-aiff-corpus" \
//!     cargo test -p deluge_audio_format --test corpus_differential -- --nocapture
//! ```
//!
//! Once set, finding no audio files fails the test rather than passing with nothing checked.
//!
//! The C++ side is `golden_vt_render`'s `audio_header_dump` scenario
//! (`tests/integration/scenarios/audio_header_dump*.cpp`), which reads paths on stdin and prints
//! one `AUDIO-HEADER <json>` line per file. `./dbt test` builds it as the `golden-vt-render`
//! product; this test runs `<repo root>/build/cargo-harness/release/golden_vt_render`, or
//! `GOLDEN_VT_RENDER_BIN`.
//!
//! The JSON number parser does not handle `nan`/`inf`; `midi_note` is always finite, so the
//! corpus cannot produce them.

use deluge_audio_format::{
    parse, wavetable, AudioHeader, Container, FeedResult, HeaderError, HeaderParser, ParseError,
    WalkGoal,
};
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

// ============================================================================================
// Minimal JSON parsing for the dumper's fixed, flat output; not worth a dependency.
// ============================================================================================

#[derive(Debug, Clone)]
enum Json {
    Null,
    Bool(bool),
    Number(f64),
    Str(String),
    Object(Vec<(String, Json)>),
}

impl Json {
    fn get(&self, key: &str) -> &Json {
        match self {
            Json::Object(pairs) => pairs
                .iter()
                .find(|(k, _)| k == key)
                .map(|(_, v)| v)
                .unwrap_or(&Json::Null),
            _ => &Json::Null,
        }
    }
    fn as_bool(&self) -> bool {
        matches!(self, Json::Bool(true))
    }
    fn as_f64(&self) -> Option<f64> {
        match self {
            Json::Number(n) => Some(*n),
            _ => None,
        }
    }
    fn as_u32(&self) -> Option<u32> {
        self.as_f64().map(|n| n as u32)
    }
    fn as_str(&self) -> Option<&str> {
        match self {
            Json::Str(s) => Some(s.as_str()),
            _ => None,
        }
    }
    fn is_null(&self) -> bool {
        matches!(self, Json::Null)
    }
}

struct JsonParser<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> JsonParser<'a> {
    fn new(s: &'a str) -> Self {
        JsonParser {
            bytes: s.as_bytes(),
            pos: 0,
        }
    }

    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.pos).copied()
    }

    fn skip_ws(&mut self) {
        while matches!(self.peek(), Some(b' ' | b'\t' | b'\n' | b'\r')) {
            self.pos += 1;
        }
    }

    fn expect(&mut self, c: u8) {
        assert_eq!(
            self.peek(),
            Some(c),
            "expected {:?} at byte {} of {:?}",
            c as char,
            self.pos,
            self.rest()
        );
        self.pos += 1;
    }

    fn rest(&self) -> &str {
        std::str::from_utf8(&self.bytes[self.pos..]).unwrap_or("<invalid utf8>")
    }

    fn parse_value(&mut self) -> Json {
        self.skip_ws();
        match self.peek() {
            Some(b'{') => self.parse_object(),
            Some(b'"') => Json::Str(self.parse_string()),
            Some(b't') => {
                self.pos += 4; // "true"
                Json::Bool(true)
            }
            Some(b'f') => {
                self.pos += 5; // "false"
                Json::Bool(false)
            }
            Some(b'n') => {
                self.pos += 4; // "null"
                Json::Null
            }
            Some(c) if c == b'-' || c.is_ascii_digit() => self.parse_number(),
            other => panic!("unexpected JSON byte {other:?} at {}", self.rest()),
        }
    }

    fn parse_object(&mut self) -> Json {
        self.expect(b'{');
        let mut pairs = Vec::new();
        self.skip_ws();
        if self.peek() == Some(b'}') {
            self.pos += 1;
            return Json::Object(pairs);
        }
        loop {
            self.skip_ws();
            let key = self.parse_string();
            self.skip_ws();
            self.expect(b':');
            let value = self.parse_value();
            pairs.push((key, value));
            self.skip_ws();
            match self.peek() {
                Some(b',') => {
                    self.pos += 1;
                }
                Some(b'}') => {
                    self.pos += 1;
                    break;
                }
                other => panic!("expected ',' or '}}' at {other:?}: {}", self.rest()),
            }
        }
        Json::Object(pairs)
    }

    fn parse_string(&mut self) -> String {
        self.expect(b'"');
        let mut out = String::new();
        loop {
            match self.peek() {
                Some(b'"') => {
                    self.pos += 1;
                    break;
                }
                Some(b'\\') => {
                    self.pos += 1;
                    match self.peek() {
                        Some(b'"') => out.push('"'),
                        Some(b'\\') => out.push('\\'),
                        Some(b'/') => out.push('/'),
                        Some(b'n') => out.push('\n'),
                        Some(b'r') => out.push('\r'),
                        Some(b't') => out.push('\t'),
                        Some(b'u') => {
                            let hex = std::str::from_utf8(&self.bytes[self.pos + 1..self.pos + 5])
                                .unwrap();
                            let code = u32::from_str_radix(hex, 16).unwrap();
                            out.push(char::from_u32(code).unwrap_or('\u{FFFD}'));
                            self.pos += 4;
                        }
                        other => panic!("bad escape {other:?}"),
                    }
                    self.pos += 1;
                }
                Some(_) => {
                    // Fast path: consume a run of plain bytes at once via UTF-8 decoding.
                    let start = self.pos;
                    while !matches!(self.peek(), Some(b'"') | Some(b'\\') | None) {
                        self.pos += 1;
                    }
                    out.push_str(
                        std::str::from_utf8(&self.bytes[start..self.pos])
                            .expect("valid utf8 in JSON string"),
                    );
                }
                None => panic!("unterminated JSON string"),
            }
        }
        out
    }

    fn parse_number(&mut self) -> Json {
        let start = self.pos;
        if self.peek() == Some(b'-') {
            self.pos += 1;
        }
        while matches!(self.peek(), Some(c) if c.is_ascii_digit() || c == b'.' || c == b'e' || c == b'E' || c == b'+' || c == b'-')
        {
            self.pos += 1;
        }
        let s = std::str::from_utf8(&self.bytes[start..self.pos]).unwrap();
        Json::Number(
            s.parse()
                .unwrap_or_else(|_| panic!("bad number literal {s:?}")),
        )
    }
}

fn parse_json_line(line: &str) -> Json {
    let mut p = JsonParser::new(line);
    let v = p.parse_value();
    p.skip_ws();
    v
}

// ============================================================================================
// Corpus walking.
// ============================================================================================

/// Recursively collects every `*.wav`/`*.aif`/`*.aiff` file under `root` (case-insensitive),
/// sorted for a deterministic run order.
fn walk_audio_files(root: &str) -> Vec<PathBuf> {
    let mut out = Vec::new();
    let mut stack = vec![PathBuf::from(root)];
    while let Some(dir) = stack.pop() {
        let entries = match std::fs::read_dir(&dir) {
            Ok(e) => e,
            Err(_) => continue,
        };
        for entry in entries.flatten() {
            let path = entry.path();
            let file_type = match entry.file_type() {
                Ok(t) => t,
                Err(_) => continue,
            };
            if file_type.is_dir() {
                stack.push(path);
            } else if let Some(ext) = path.extension().and_then(|e| e.to_str()) {
                let ext = ext.to_ascii_lowercase();
                if ext == "wav" || ext == "aif" || ext == "aiff" {
                    out.push(path);
                }
            }
        }
    }
    out.sort();
    out
}

// ============================================================================================
// The C++ dumper subprocess.
// ============================================================================================

/// Prefix on each of the scenario's per-file JSON lines.
const DUMP_PREFIX: &str = "AUDIO-HEADER ";

fn cpp_dumper_binary() -> PathBuf {
    if let Ok(p) = std::env::var("GOLDEN_VT_RENDER_BIN") {
        return PathBuf::from(p);
    }
    // CARGO_MANIFEST_DIR is crates/deluge_audio_format; the repo root is two levels up.
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let bin = "../../build/cargo-harness/release/golden_vt_render";
    manifest_dir
        .join(bin)
        .canonicalize()
        .unwrap_or_else(|_| manifest_dir.join(bin))
}

/// Runs the C++ dumper once over `paths` and returns each file's JSON, in `paths` order (the
/// scenario emits one `AUDIO-HEADER` line per stdin line, in order). One subprocess for the whole
/// corpus, so the app boots once.
fn run_cpp_dumper(paths: &[PathBuf]) -> Vec<Json> {
    let bin = cpp_dumper_binary();
    assert!(
        bin.exists(),
        "golden_vt_render not found at {bin:?} (or $GOLDEN_VT_RENDER_BIN) — build it first:\n  \
         ./dbt test golden-synth (or any test that builds the golden-vt-render product)"
    );

    let mut child = Command::new(&bin)
        .env("GOLDEN_SCENARIO", "audio_header_dump")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()
        .unwrap_or_else(|e| panic!("failed to spawn {bin:?}: {e}"));

    let mut stdin = child.stdin.take().expect("piped stdin");
    let paths_owned: Vec<PathBuf> = paths.to_vec();
    // Write on a separate thread: with many paths both pipes can fill at once, and writing then
    // reading on one thread would deadlock.
    let writer = std::thread::spawn(move || {
        for p in &paths_owned {
            writeln!(stdin, "{}", p.to_string_lossy()).expect("write to dumper stdin");
        }
        // Dropping stdin here signals EOF to the child.
    });

    let stdout = child.stdout.take().expect("piped stdout");
    let mut lines = Vec::with_capacity(paths.len());
    for line in BufReader::new(stdout).lines() {
        let line = line.expect("read dumper stdout");
        if let Some(json) = line.strip_prefix(DUMP_PREFIX) {
            lines.push(json.to_string());
        }
    }

    writer.join().expect("dumper stdin writer thread panicked");
    let status = child.wait().expect("wait on dumper");
    assert!(status.success(), "{bin:?} exited with {status}");
    assert_eq!(
        lines.len(),
        paths.len(),
        "dumper produced {} lines for {} input paths",
        lines.len(),
        paths.len()
    );

    lines.iter().map(|l| parse_json_line(l)).collect()
}

// ============================================================================================
// Field-by-field comparison.
// ============================================================================================

#[derive(Debug)]
struct Disagreement {
    path: PathBuf,
    field: &'static str,
    ours: String,
    theirs: String,
}

/// Buckets a [`ParseError`] the way the C++ side's `Error` values do, since the [`Reason`] does not
/// cross the C ABI.
///
/// [`Reason`]: deluge_audio_format::Reason
fn ours_outcome_category(e: &ParseError) -> &'static str {
    match e {
        ParseError::NeedAtLeast(_) => "corrupted",
        ParseError::Malformed(_) => "corrupted",
        ParseError::NotAudio => "unsupported",
        ParseError::Unsupported(_) => "unsupported",
    }
}

/// Buckets the `Error` ordinals (`definitions_cxx.hpp`) that `parseSampleHeader`/
/// `parseWaveTableHeader` return.
fn cpp_outcome_category(code: i64) -> &'static str {
    match code {
        5 => "corrupted",   // FILE_CORRUPTED
        7 => "unsupported", // FILE_UNSUPPORTED
        _ => "other",
    }
}

fn cmp_u32(
    out: &mut Vec<Disagreement>,
    path: &Path,
    field: &'static str,
    ours: u32,
    theirs: &Json,
) {
    let t = theirs.as_u32();
    if t != Some(ours) {
        out.push(Disagreement {
            path: path.to_path_buf(),
            field,
            ours: ours.to_string(),
            theirs: format!("{theirs:?}"),
        });
    }
}

fn cmp_bool(
    out: &mut Vec<Disagreement>,
    path: &Path,
    field: &'static str,
    ours: bool,
    theirs: &Json,
) {
    if theirs.as_bool() != ours {
        out.push(Disagreement {
            path: path.to_path_buf(),
            field,
            ours: ours.to_string(),
            theirs: format!("{theirs:?}"),
        });
    }
}

/// Compares the container. Neither C++ output struct carries it, so the dumper reports it from the
/// file's 12-byte magic as a top-level `"container"` field (`"wav"`/`"aiff"`/`"unknown"`).
fn cmp_container(out: &mut Vec<Disagreement>, path: &Path, ours: Container, theirs: &Json) {
    let ours_tag = match ours {
        Container::Wav => "wav",
        Container::Aiff => "aiff",
    };
    if theirs.get("container").as_str() != Some(ours_tag) {
        out.push(Disagreement {
            path: path.to_path_buf(),
            field: "container",
            ours: ours_tag.to_string(),
            theirs: format!("{:?}", theirs.get("container")),
        });
    }
}

/// Tolerance on `midi_note`, in semitones: absorbs `f32`-vs-`f64` rounding of
/// `note - fine_tune * 0.01` (under 1e-4 for every input pair), while a wrong note or fine-tune
/// byte is off by at least a cent.
const MIDI_NOTE_TOLERANCE: f64 = 1e-4;

fn cmp_midi_note(out: &mut Vec<Disagreement>, path: &Path, ours: Option<f32>, theirs: &Json) {
    match (ours, theirs.is_null()) {
        (None, true) => {}
        (Some(v), false) => {
            let t = theirs
                .as_f64()
                .expect("midi_note should be a number when non-null");
            if (v as f64 - t).abs() >= MIDI_NOTE_TOLERANCE {
                out.push(Disagreement {
                    path: path.to_path_buf(),
                    field: "sample.midi_note",
                    ours: format!("{v}"),
                    theirs: format!("{t}"),
                });
            }
        }
        (ours, _) => out.push(Disagreement {
            path: path.to_path_buf(),
            field: "sample.midi_note",
            ours: format!("{ours:?}"),
            theirs: format!("{theirs:?}"),
        }),
    }
}

fn cmp_loop_points(out: &mut Vec<Disagreement>, path: &Path, ours: &AudioHeader, theirs: &Json) {
    for (field, ours_v, key) in [
        (
            "sample.file_loop_start_samples",
            ours.file_loop_start_samples,
            "file_loop_start_samples",
        ),
        (
            "sample.file_loop_end_samples",
            ours.file_loop_end_samples,
            "file_loop_end_samples",
        ),
    ] {
        let theirs_v = theirs.get(key).as_u32();
        if theirs_v != Some(ours_v) {
            out.push(Disagreement {
                path: path.to_path_buf(),
                field,
                ours: ours_v.to_string(),
                theirs: format!("{theirs_v:?}"),
            });
        }
    }
}

fn compare_sample(
    path: &Path,
    ours: &Result<AudioHeader, ParseError>,
    theirs: &Json,
    out: &mut Vec<Disagreement>,
) {
    let sample = theirs.get("sample");
    let theirs_ok = sample.get("ok").as_bool();
    match (ours, theirs_ok) {
        (Ok(h), true) => {
            cmp_container(out, path, h.container, theirs);
            cmp_u32(
                out,
                path,
                "sample.num_channels",
                h.num_channels as u32,
                sample.get("num_channels"),
            );
            cmp_u32(
                out,
                path,
                "sample.byte_depth",
                h.byte_depth as u32,
                sample.get("byte_depth"),
            );
            cmp_u32(
                out,
                path,
                "sample.raw_data_format",
                h.raw_data_format as u32,
                sample.get("raw_data_format"),
            );
            cmp_u32(
                out,
                path,
                "sample.sample_rate",
                h.sample_rate,
                sample.get("sample_rate"),
            );
            cmp_u32(
                out,
                path,
                "sample.audio_data_start_pos_bytes",
                h.audio_data_start_pos_bytes,
                sample.get("audio_data_start_pos_bytes"),
            );
            cmp_u32(
                out,
                path,
                "sample.audio_data_length_bytes",
                h.audio_data_length_bytes,
                sample.get("audio_data_length_bytes"),
            );
            cmp_loop_points(out, path, h, sample);
            cmp_midi_note(out, path, h.midi_note, sample.get("midi_note"));
            // Serum/Deluge convention with no spec: any mismatch is a defect.
            cmp_u32(
                out,
                path,
                "sample.wave_table_cycle_size",
                h.wavetable.cycle_size,
                sample.get("wave_table_cycle_size"),
            );
            cmp_bool(
                out,
                path,
                "sample.file_explicitly_specifies_self_as_wave_table",
                h.wavetable.explicitly_specifies_self,
                sample.get("file_explicitly_specifies_self_as_wave_table"),
            );
        }
        (Err(e), false) => {
            let ours_cat = ours_outcome_category(e);
            let theirs_code = sample.get("error").as_f64().unwrap_or(-1.0) as i64;
            let theirs_cat = cpp_outcome_category(theirs_code);
            if ours_cat != theirs_cat {
                out.push(Disagreement {
                    path: path.to_path_buf(),
                    field: "sample.outcome_category",
                    ours: format!("{e:?} ({ours_cat})"),
                    theirs: format!("error={theirs_code} ({theirs_cat})"),
                });
            }
        }
        (Ok(h), false) => out.push(Disagreement {
            path: path.to_path_buf(),
            field: "sample.outcome",
            ours: format!("Ok({h:?})"),
            theirs: format!("err={:?}", sample.get("error")),
        }),
        (Err(e), true) => out.push(Disagreement {
            path: path.to_path_buf(),
            field: "sample.outcome",
            ours: format!("Err({e:?})"),
            theirs: "ok".to_string(),
        }),
    }
}

/// Decodes `bytes` as `parseWaveTableHeader` does: a size-aware [`WalkGoal::AudioOnly`] walk,
/// stopping at the audio data chunk.
///
/// Comparing against [`parse`] instead would be wrong: a whole-header walk reads `smpl`/`inst` and
/// rejects unplayable sample rates, so it disagrees on valid wavetables such as a 192 kHz one.
fn decode_as_wavetable_walk(bytes: &[u8]) -> Result<AudioHeader, HeaderError> {
    let mut parser = HeaderParser::new_with_goal(bytes.len() as u32, WalkGoal::AudioOnly);
    loop {
        if parser.located_audio_data() {
            return parser.finish_here();
        }
        let Some((start, len)) = parser.pending() else {
            unreachable!("the walk only ever ends inside `feed`")
        };
        let from = (start as usize).min(bytes.len());
        let to = from.saturating_add(len as usize).min(bytes.len());
        match parser.feed(&bytes[from..to], start) {
            Ok(FeedResult::Done(header)) => return Ok(header),
            Ok(FeedResult::Need { .. }) => {}
            Err(error) => return Err(error),
        }
    }
}

fn compare_wavetable(
    path: &Path,
    ours: &Result<AudioHeader, HeaderError>,
    insist: bool,
    theirs: &Json,
    out: &mut Vec<Disagreement>,
) {
    let key: &'static str = if insist {
        "wavetable_insist_true"
    } else {
        "wavetable_insist_false"
    };
    let wt = theirs.get(key);
    let theirs_ok = wt.get("ok").as_bool();

    match ours {
        Ok(h) => {
            let ours_result = wavetable::accept_as_wavetable(h, insist);
            match (&ours_result, theirs_ok) {
                (Ok(use_), true) => {
                    cmp_u32(
                        out,
                        path,
                        "wavetable.cycle_size",
                        use_.cycle_size,
                        wt.get("cycle_size"),
                    );
                    cmp_u32(
                        out,
                        path,
                        "wavetable.num_channels",
                        h.num_channels as u32,
                        wt.get("num_channels"),
                    );
                    cmp_u32(
                        out,
                        path,
                        "wavetable.byte_depth",
                        h.byte_depth as u32,
                        wt.get("byte_depth"),
                    );
                    cmp_u32(
                        out,
                        path,
                        "wavetable.raw_data_format",
                        h.raw_data_format as u32,
                        wt.get("raw_data_format"),
                    );
                    cmp_u32(
                        out,
                        path,
                        "wavetable.audio_data_start_pos_bytes",
                        h.audio_data_start_pos_bytes,
                        wt.get("audio_data_start_pos_bytes"),
                    );
                    cmp_u32(
                        out,
                        path,
                        "wavetable.audio_data_length_bytes",
                        h.audio_data_length_bytes,
                        wt.get("audio_data_length_bytes"),
                    );
                }
                (Err(_), false) => {} // both reject; the specific reject *reason* isn't cross-checked.
                _ => out.push(Disagreement {
                    path: path.to_path_buf(),
                    field: "wavetable.outcome",
                    ours: format!("insist={insist} {ours_result:?}"),
                    theirs: format!("ok={theirs_ok}"),
                }),
            }
        }
        Err(e) => {
            if theirs_ok {
                out.push(Disagreement {
                    path: path.to_path_buf(),
                    field: "wavetable.outcome",
                    ours: format!("insist={insist} the walk failed: {e:?}"),
                    theirs: "ok".to_string(),
                });
            }
            // Both rejecting is agreement enough: the reason does not cross the C ABI.
        }
    }
    let _ = key;
}

fn compare_one_file(
    path: &Path,
    ours: &Result<AudioHeader, ParseError>,
    bytes: &[u8],
    theirs: &Json,
) -> Vec<Disagreement> {
    let mut out = Vec::new();
    compare_sample(path, ours, theirs, &mut out);
    // Each entry point against the walk it runs: Sample against `parse`, WaveTable against an
    // audio-only walk.
    let ours_wavetable = decode_as_wavetable_walk(bytes);
    compare_wavetable(path, &ours_wavetable, false, theirs, &mut out);
    compare_wavetable(path, &ours_wavetable, true, theirs, &mut out);
    out
}

// ============================================================================================
// The test.
// ============================================================================================

// ============================================================================================
// Synthetic fixtures: shapes a corpus may lack, written to a temp directory and swept through the
// same dumper and comparison.
// ============================================================================================

/// A RIFF/WAVE file: `fmt ` + `data`, with the audio zero-filled.
fn synth_wav(channels: u16, rate: u32, bits: u16, audio_bytes: u32) -> Vec<u8> {
    let mut body = Vec::new();
    body.extend_from_slice(b"fmt ");
    body.extend_from_slice(&16u32.to_le_bytes());
    body.extend_from_slice(&1u16.to_le_bytes()); // PCM
    body.extend_from_slice(&channels.to_le_bytes());
    body.extend_from_slice(&rate.to_le_bytes());
    let block_align = channels * (bits / 8);
    body.extend_from_slice(&(rate * u32::from(block_align)).to_le_bytes());
    body.extend_from_slice(&block_align.to_le_bytes());
    body.extend_from_slice(&bits.to_le_bytes());
    body.extend_from_slice(b"data");
    body.extend_from_slice(&audio_bytes.to_le_bytes());
    body.resize(body.len() + audio_bytes as usize, 0);

    let mut file = Vec::new();
    file.extend_from_slice(b"RIFF");
    file.extend_from_slice(&((4 + body.len()) as u32).to_le_bytes());
    file.extend_from_slice(b"WAVE");
    file.extend_from_slice(&body);
    file
}

/// A `smpl` chunk with no loops: 36 bytes, less than the parser's 52-byte `smpl` window.
fn synth_smpl_no_loops() -> Vec<u8> {
    let mut chunk = Vec::new();
    chunk.extend_from_slice(b"smpl");
    chunk.extend_from_slice(&36u32.to_le_bytes());
    for word in [0u32; 9] {
        chunk.extend_from_slice(&word.to_le_bytes());
    }
    chunk
}

/// Splices `chunk` in just before the `data` chunk of a [`synth_wav`] file and fixes the RIFF size.
fn synth_wav_with_chunk_before_data(base: Vec<u8>, chunk: &[u8]) -> Vec<u8> {
    let data_at = base
        .windows(4)
        .position(|w| w == b"data")
        .expect("the base file has a data chunk");
    let mut file = Vec::with_capacity(base.len() + chunk.len());
    file.extend_from_slice(&base[..data_at]);
    file.extend_from_slice(chunk);
    file.extend_from_slice(&base[data_at..]);
    let riff_size = (file.len() - 8) as u32;
    file[4..8].copy_from_slice(&riff_size.to_le_bytes());
    file
}

/// Writes the synthetic fixtures into `dir` and returns their paths.
fn write_synthetic_fixtures(dir: &Path) -> Vec<PathBuf> {
    let mut written = Vec::new();
    let mut put = |name: &str, bytes: Vec<u8>| {
        let path = dir.join(name);
        std::fs::write(&path, &bytes).unwrap_or_else(|e| panic!("write {path:?}: {e}"));
        written.push(path);
    };

    // 192 kHz is unplayable as a sample but irrelevant to a wavetable; mono whole 2048-sample
    // cycles make it a wavetable by length alone.
    put(
        "wt_192k_mono_2048.wav",
        synth_wav(1, 192_000, 16, 2048 * 2 * 4),
    );
    // The same rate where it is judged: a stereo non-wavetable both sides must reject alike.
    put("sample_192k_stereo.wav", synth_wav(2, 192_000, 16, 4096));
    // A `smpl` shorter than its read window, right before `data`: an over-read would reach the
    // audio.
    put(
        "wt_smpl_no_loops_before_data.wav",
        synth_wav_with_chunk_before_data(
            synth_wav(1, 44_100, 16, 2048 * 2),
            &synth_smpl_no_loops(),
        ),
    );
    // Ordinary files, covering the common case.
    put("sample_44k1_stereo.wav", synth_wav(2, 44_100, 16, 4096));
    put("wt_44k1_mono_2048.wav", synth_wav(1, 44_100, 16, 2048 * 2));
    written
}

#[test]
fn matches_the_cpp_parser_over_the_corpus() {
    let Ok(roots_env) = std::env::var("DELUGE_AUDIO_CORPUS") else {
        eprintln!("DELUGE_AUDIO_CORPUS unset — skipping");
        return;
    };

    let mut paths = Vec::new();
    for root in roots_env.split(',') {
        let root = root.trim();
        if root.is_empty() {
            continue;
        }
        let found = walk_audio_files(root);
        eprintln!("{root}: {} files", found.len());
        paths.extend(found);
    }
    assert!(
        !paths.is_empty(),
        "no audio files found under {roots_env:?}"
    );

    // In their own directory so a corpus root is never written to.
    let fixtures_dir = std::env::temp_dir().join("deluge-audio-differential-fixtures");
    std::fs::create_dir_all(&fixtures_dir)
        .unwrap_or_else(|e| panic!("create {fixtures_dir:?}: {e}"));
    let synthetic = write_synthetic_fixtures(&fixtures_dir);
    eprintln!(
        "{}: {} synthetic fixtures",
        fixtures_dir.display(),
        synthetic.len()
    );
    paths.extend(synthetic);

    let dumps = run_cpp_dumper(&paths);

    let mut checked = 0usize;
    let mut disagreements: Vec<Disagreement> = Vec::new();

    for (path, theirs) in paths.iter().zip(dumps.iter()) {
        checked += 1;
        assert!(
            !theirs.get("read_error").as_bool(),
            "C++ dumper could not read {path:?} off the host filesystem"
        );

        let bytes = std::fs::read(path).unwrap_or_else(|e| panic!("read {path:?}: {e}"));
        let ours = parse(&bytes);

        disagreements.extend(compare_one_file(path, &ours, &bytes, theirs));
    }

    eprintln!("checked {checked} files");

    assert!(
        checked > 20,
        "expected a real corpus, walked only {checked} files"
    );
    let report: Vec<String> = disagreements
        .iter()
        .map(|d| {
            format!(
                "{} [{}]: ours={} theirs={}",
                d.path.display(),
                d.field,
                d.ours,
                d.theirs
            )
        })
        .collect();
    assert!(
        report.is_empty(),
        "{} disagreements:\n{}",
        report.len(),
        report.join("\n")
    );
}
