//! Corpus differential: `deluge_audio_format::parse` + `wavetable::accept_as_wavetable` against the
//! legacy C++ `parseSampleHeader`/`parseWaveTableHeader` it replaces, run over real WAV/AIFF files.
//!
//! This is the gate that makes the rewrite trustworthy, so per
//! `docs/superpowers/specs/2026-08-26-rust-audio-header-parser-design.md` §6 it treats the C++ as a
//! REFERENCE, not an oracle: on disagreement, the RIFF/AIFF spec decides which side is right, and the
//! ruling is recorded in that spec's §6 (not encoded silently here). Two fields are the sole
//! exception — `cycle_size`/`explicitly_specifies_self` are Serum/Deluge convention with no governing
//! spec, so for THOSE the C++ *is* the oracle and any disagreement is a Rust defect.
//!
//! A `std` test (this crate's library stays `no_std`/no-`alloc`; nothing here links into it).
//! Gated on `DELUGE_AUDIO_CORPUS` so CI without a corpus checked out stays green:
//!
//! ```text
//! DELUGE_AUDIO_CORPUS="$HOME/Deluge Backup"      cargo test -p deluge_audio_format --test corpus_differential -- --nocapture
//! DELUGE_AUDIO_CORPUS="$HOME/deluge-aiff-corpus"  cargo test -p deluge_audio_format --test corpus_differential -- --nocapture
//! DELUGE_AUDIO_CORPUS="$HOME/Deluge Backup,$HOME/deluge-aiff-corpus" cargo test -p deluge_audio_format --test corpus_differential -- --nocapture
//! ```
//!
//! The C++ side of the comparison is `deluge_audio_header_dump` (`src/bsp/host/
//! host_audio_header_dump_main.cpp`, driving `harness/audio_header_dump.h`), a host-only CLI that
//! reads file paths on stdin and prints one JSON line per file. Build it first:
//!
//! ```text
//! cmake --build sim/build --target deluge_audio_header_dump
//! ```
//!
//! This test finds it at `<repo root>/sim/build/deluge_audio_header_dump` (or `DELUGE_AUDIO_HEADER_DUMP_BIN`).

use deluge_audio_format::{parse, wavetable, AudioHeader, Container, ParseError};
use std::collections::HashMap;
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

// ============================================================================================
// Minimal schema-specific JSON parsing. Not a general JSON library: `deluge_audio_header_dump`'s
// output is a fixed, flat shape (see `harness/audio_header_dump.cpp`), and adding a real JSON crate
// as a dependency for one test file's fixed schema isn't worth it in a workspace that otherwise
// depends on nothing.
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

/// Recursively collect every `*.wav`/`*.aif`/`*.aiff` file under `root` (case-insensitive
/// extension), sorted for a deterministic run order.
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

fn cpp_dumper_binary() -> PathBuf {
    if let Ok(p) = std::env::var("DELUGE_AUDIO_HEADER_DUMP_BIN") {
        return PathBuf::from(p);
    }
    // CARGO_MANIFEST_DIR is crates/deluge_audio_format; the repo root is two levels up.
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    manifest_dir
        .join("../../sim/build/deluge_audio_header_dump")
        .canonicalize()
        .unwrap_or_else(|_| manifest_dir.join("../../sim/build/deluge_audio_header_dump"))
}

/// Run the C++ dumper once over every path in `paths` and return each file's parsed JSON, keyed by
/// its position in `paths` (the dumper emits exactly one line per stdin line, in order — see
/// `deluge_audio_header_dump_driver()` in `host_audio_header_dump_main.cpp`). One subprocess for the
/// WHOLE corpus rather than one per file: `deluge_audio_header_dump` boots the app once (~ms), so
/// batching keeps a 2000+ file run to a couple of seconds instead of paying that boot cost per file.
fn run_cpp_dumper(paths: &[PathBuf]) -> Vec<Json> {
    let bin = cpp_dumper_binary();
    assert!(
        bin.exists(),
        "C++ dumper not found at {bin:?} (or $DELUGE_AUDIO_HEADER_DUMP_BIN) — build it first:\n  \
         cmake --build sim/build --target deluge_audio_header_dump"
    );

    let mut child = Command::new(&bin)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()
        .unwrap_or_else(|e| panic!("failed to spawn {bin:?}: {e}"));

    let mut stdin = child.stdin.take().expect("piped stdin");
    let paths_owned: Vec<PathBuf> = paths.to_vec();
    // Write on a separate thread: with 2000+ paths the combined stdin/stdout pipe traffic can
    // exceed the OS pipe buffer in both directions at once, so writing and reading on the same
    // thread sequentially (write everything, then read) can deadlock once both pipes fill.
    let writer = std::thread::spawn(move || {
        for p in &paths_owned {
            writeln!(stdin, "{}", p.to_string_lossy()).expect("write to dumper stdin");
        }
        // stdin is dropped (closed) here, signalling EOF to the child.
    });

    let stdout = child.stdout.take().expect("piped stdout");
    let mut lines = Vec::with_capacity(paths.len());
    for line in BufReader::new(stdout).lines() {
        lines.push(line.expect("read dumper stdout"));
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
    /// `Some(reason)` marks a KNOWN, adjudicated disagreement (see the module doc and
    /// `docs/superpowers/specs/2026-08-26-rust-audio-header-parser-design.md` §6): recorded, but
    /// does not fail the test. `None` is an unadjudicated finding that DOES fail the test.
    adjudicated: Option<&'static str>,
}

/// `ParseError`'s coarse outcome shape, for comparing against the C++'s single `Error` enum without
/// requiring the exact `Reason` to line up (the C++ doesn't have that granularity at all — see
/// `HeaderFields`/`walkChunks` in `audio_file_format.cpp`, which return one of a handful of `Error`
/// values regardless of which specific check failed).
fn ours_outcome_category(e: &ParseError) -> &'static str {
    match e {
        ParseError::NeedAtLeast(_) => "corrupted",
        ParseError::Malformed(_) => "corrupted",
        ParseError::NotAudio => "unsupported",
        ParseError::Unsupported(_) => "unsupported",
    }
}

/// Error values `audio_file_format.cpp` actually returns from `parseSampleHeader`/
/// `parseWaveTableHeader` (`definitions_cxx.hpp`'s `enum class Error`, by ordinal).
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
            adjudicated: None,
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
            adjudicated: None,
        });
    }
}

/// Adjudication 1 (task-9 brief): the C++ computes `midiNote - fineTune * 0.01` where the bare
/// `0.01` literal is `double`, so despite both operands being explicitly cast to `float` the whole
/// expression evaluates in double precision and only narrows to `float` at the assignment; the Rust
/// stays in `f32` throughout (matching the explicit casts, which is what the C++ source visibly
/// intends). Verified exhaustively over all 32768 (note, fine_tune) pairs: 285 differ from the exact
/// double-precision computation by less than 1e-4 semitone. Adjudicated in the RUST's favour — this
/// tolerance exists to absorb that known double-vs-float promotion artefact, not to paper over a real
/// bug, so it is deliberately far tighter than the actual worst-case divergence would need to catch a
/// genuinely wrong computation (a wrong midi note or fine-tune byte produces errors of a whole
/// semitone or more, nowhere close to this boundary).
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
                    adjudicated: None,
                });
            }
        }
        (ours, _) => out.push(Disagreement {
            path: path.to_path_buf(),
            field: "sample.midi_note",
            ours: format!("{ours:?}"),
            theirs: format!("{theirs:?}"),
            adjudicated: None,
        }),
    }
}

/// Adjudication 2 (task-9 brief): `audio_file_format.cpp:394-395`'s AIFF `MARK` pstring skip,
/// `(stringLength + 1) & ~1`, rounds only the marker NAME length up to even; the AIFF spec pads the
/// WHOLE pstring record (length byte + name) to an even total. The two formulas disagree whenever the
/// name length is even (including zero) — the common case, e.g. this crate's own AIFF corpus
/// generator writes zero-length marker names — and every marker after the first one in such a file
/// gets misread by the C++ (verified on a real file: marker 2 resolves as `id=512 pos=466179`
/// instead of `id=2 pos=1821`). The Rust implementation (`lib.rs`'s `MARK` arm) uses the corrected
/// skip, `((1 + count + 1) & ~1) - 1`. This is adjudicated in the RUST's favour; a divergence here on
/// an AIFF file is EXPECTED, not a Rust defect, and must not be "fixed" by matching the C++ bug.
const AIFF_LOOP_MARKER_CPP_BUG: &str = "aiff_loop_marker_cpp_pstring_off_by_one (design doc §6)";

fn cmp_loop_points(out: &mut Vec<Disagreement>, path: &Path, ours: &AudioHeader, theirs: &Json) {
    let is_aiff = ours.container == Container::Aiff;
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
                adjudicated: if is_aiff {
                    Some(AIFF_LOOP_MARKER_CPP_BUG)
                } else {
                    None
                },
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
            // Adjudication 4 (task-9 brief): the C++ IS the oracle for these two fields (Serum/Deluge
            // convention, no governing spec) — deliberately no `adjudicated` tag, so ANY mismatch here
            // fails the test as a real Rust defect.
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
                    adjudicated: None,
                });
            }
        }
        (Ok(h), false) => out.push(Disagreement {
            path: path.to_path_buf(),
            field: "sample.outcome",
            ours: format!("Ok({h:?})"),
            theirs: format!("err={:?}", sample.get("error")),
            adjudicated: None,
        }),
        (Err(e), true) => out.push(Disagreement {
            path: path.to_path_buf(),
            field: "sample.outcome",
            ours: format!("Err({e:?})"),
            theirs: "ok".to_string(),
            adjudicated: None,
        }),
    }
}

fn compare_wavetable(
    path: &Path,
    ours: &Result<AudioHeader, ParseError>,
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
                    adjudicated: None,
                }),
            }
        }
        Err(e) => {
            if theirs_ok {
                out.push(Disagreement {
                    path: path.to_path_buf(),
                    field: "wavetable.outcome",
                    ours: format!("insist={insist} parse() failed: {e:?}"),
                    theirs: "ok".to_string(),
                    adjudicated: None,
                });
            }
            // If theirs also rejects, that's consistent enough without matching WHY: a whole-file
            // `parse()` failure and a WaveTable-mode-specific rejection aren't directly comparable
            // (parseWaveTableHeader can stop scanning at the data chunk, before whatever later chunk
            // caused parse() to fail as a Sample).
        }
    }
    let _ = key;
}

fn compare_one_file(
    path: &Path,
    ours: &Result<AudioHeader, ParseError>,
    theirs: &Json,
) -> Vec<Disagreement> {
    let mut out = Vec::new();
    compare_sample(path, ours, theirs, &mut out);
    compare_wavetable(path, ours, false, theirs, &mut out);
    compare_wavetable(path, ours, true, theirs, &mut out);
    out
}

// ============================================================================================
// The test.
// ============================================================================================

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

    let dumps = run_cpp_dumper(&paths);

    let mut checked = 0usize;
    let mut expected_counts: HashMap<&'static str, usize> = HashMap::new();
    let mut expected_examples: HashMap<&'static str, Vec<String>> = HashMap::new();
    let mut unexpected: Vec<Disagreement> = Vec::new();

    for (path, theirs) in paths.iter().zip(dumps.iter()) {
        checked += 1;
        assert!(
            !theirs.get("read_error").as_bool(),
            "C++ dumper could not read {path:?} off the host filesystem"
        );

        let bytes = std::fs::read(path).unwrap_or_else(|e| panic!("read {path:?}: {e}"));
        let ours = parse(&bytes);

        for d in compare_one_file(path, &ours, theirs) {
            match d.adjudicated {
                Some(reason) => {
                    *expected_counts.entry(reason).or_insert(0) += 1;
                    let examples = expected_examples.entry(reason).or_default();
                    if examples.len() < 5 {
                        examples.push(format!(
                            "{} [{}]: ours={} theirs={}",
                            path.display(),
                            d.field,
                            d.ours,
                            d.theirs
                        ));
                    }
                }
                None => unexpected.push(d),
            }
        }
    }

    eprintln!("checked {checked} files");
    eprintln!("adjudicated (expected) disagreement categories:");
    for (reason, count) in &expected_counts {
        eprintln!("  {count:5} x {reason}");
        for ex in &expected_examples[reason] {
            eprintln!("        e.g. {ex}");
        }
    }

    assert!(
        checked > 20,
        "expected a real corpus, walked only {checked} files"
    );
    assert!(
        unexpected.is_empty(),
        "{} unadjudicated disagreements:\n{unexpected:#?}",
        unexpected.len()
    );
}
