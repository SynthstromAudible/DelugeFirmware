//! Card manifests: a scenario's memory card written as a short list of operations, built into a directory that
//! `memory_card::install` then copies onto the card.
//!
//! One operation a line; `#` starts a comment. Fields are separated by whitespace, so no path holds a space or `=` (a
//! word with `=` is a `key=value` field). Card paths (`<path>`, `<dst>`, `<dir>` written to) are relative to the
//! card's root; source paths (`from=`) are relative to the repository. Numbers are decimal, or hexadecimal after `0x`.
//!
//! | Operation | Writes |
//! | --- | --- |
//! | `dir <path>` | an empty directory, with its parents |
//! | `wav <path> ch=<n> bits=<n> bytes=<n> fill=<byte>` | a PCM WAV header (44.1 kHz) and `bytes` data bytes, each `fill` |
//! | `sine <path> ch=<n> frames=<n> hz=<f>` | a 16-bit sine at `hz`, amplitude 16,000, the same on every channel |
//! | `partials <path> frames=<n> <hz>:<amp>,…` | a 16-bit mono sum of sines, scaled by 30,000 (amplitudes sum to ≤ 1) |
//! | `raw <path> bytes=<n> fill=<byte>` | `bytes` bytes, each `fill`, with no header |
//! | `copy <dst> from=<file>` | a copy of `<file>` |
//! | `copyall <dir> from=<dir> ext=<ext>` | a copy of each file in `from` whose extension is exactly `ext` |
//! | `base <dir>` | a copy of the repository tree `<dir>`, empty directories included; only as the first operation |
//! | `text <path> <<MARK` | the lines after it up to the line `MARK`, as written, each ending in a newline |
//! | `delete <path>` | nothing: removes the file or directory tree at `<path>`, which must exist |
//!
//! A `text` heredoc's lines are taken as they stand: a `#` in them is text, and a tab stays a tab. A manifest that
//! starts from `base` states only how its card differs from that tree.
//!
//! A fill tells apart files that agree on every geometry field when the C++ side fingerprints the bytes it reads
//! back; a sine is for the legs whose pitch detection must name a note, which a constant fill never does.

use std::collections::BTreeMap;
use std::fmt;
use std::path::{Component, Path, PathBuf};

/// One line of a manifest.
#[derive(Debug, Clone, PartialEq)]
pub enum Op {
    Dir(PathBuf),
    Wav {
        path: PathBuf,
        channels: u16,
        bits: u16,
        data_bytes: u32,
        fill: u8,
    },
    Sine {
        path: PathBuf,
        channels: u16,
        frames: u32,
        hz: f64,
    },
    Partials {
        path: PathBuf,
        frames: u32,
        partials: Vec<(f64, f64)>,
    },
    Raw {
        path: PathBuf,
        bytes: u32,
        fill: u8,
    },
    Copy {
        to: PathBuf,
        from: PathBuf,
    },
    CopyAll {
        to: PathBuf,
        from: PathBuf,
        extension: String,
    },
    Base(PathBuf),
    Text {
        path: PathBuf,
        bytes: Vec<u8>,
    },
    Delete(PathBuf),
}

/// A manifest line that does not parse.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error {
    pub file: PathBuf,
    pub line: usize,
    pub message: String,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}:{}: {}", self.file.display(), self.line, self.message)
    }
}

impl std::error::Error for Error {}

/// Parses manifest `text`; `file` names it in errors.
pub fn parse(file: &Path, text: &str) -> Result<Vec<Op>, Error> {
    let mut ops = Vec::new();
    let mut lines = text.lines().enumerate();
    while let Some((index, raw)) = lines.next() {
        let fail = |message: String| Error {
            file: file.to_path_buf(),
            line: index + 1,
            message,
        };
        let content = raw.split_once('#').map_or(raw, |(before, _)| before);
        let mut words = content.split_whitespace();
        let Some(name) = words.next() else { continue };
        let words: Vec<&str> = words.collect();
        let op = if name == "text" {
            parse_text(&words, &mut lines).map_err(fail)?
        } else {
            parse_op(name, &words).map_err(fail)?
        };
        if matches!(op, Op::Base(_)) && !ops.is_empty() {
            return Err(fail("`base` must be the first operation".into()));
        }
        check_wav_size(name, &op).map_err(fail)?;
        ops.push(op);
    }
    Ok(ops)
}

/// `text <path> <<MARK`: the lines `lines` yields up to the line `MARK`, each with its newline.
fn parse_text<'a>(
    words: &[&str],
    lines: &mut impl Iterator<Item = (usize, &'a str)>,
) -> Result<Op, String> {
    let usage = || "expected `text <path> <<MARK`".to_string();
    let [path, open] = words else {
        return Err(usage());
    };
    let mark = open
        .strip_prefix("<<")
        .filter(|m| !m.is_empty())
        .ok_or_else(usage)?;
    let mut bytes = Vec::new();
    for (_, line) in lines.by_ref() {
        if line == mark {
            return Ok(Op::Text {
                path: relative(path)?,
                bytes,
            });
        }
        bytes.extend_from_slice(line.as_bytes());
        bytes.push(b'\n');
    }
    Err(format!("`text` never reaches its closing `{mark}` line"))
}

/// Fails when a WAV op's header fields would overflow: the block align (a `u16`), or the data size or the RIFF size
/// (`36 +` the data size, both `u32`).
fn check_wav_size(name: &str, op: &Op) -> Result<(), String> {
    let (channels, bits, data_bytes) = match *op {
        Op::Wav {
            channels,
            bits,
            data_bytes,
            ..
        } => (channels, bits, Some(data_bytes)),
        Op::Sine {
            channels, frames, ..
        } => (
            channels,
            16,
            frames
                .checked_mul(u32::from(channels))
                .and_then(|n| n.checked_mul(2)),
        ),
        Op::Partials { frames, .. } => (1, 16, frames.checked_mul(2)),
        _ => return Ok(()),
    };
    channels
        .checked_mul(bits / 8)
        .ok_or_else(|| format!("`{name}` makes a block align larger than 65535"))?;
    data_bytes
        .and_then(|n| n.checked_add(36))
        .map(drop)
        .ok_or_else(|| format!("`{name}` makes a WAV larger than 4 GiB"))
}

fn parse_op(name: &str, words: &[&str]) -> Result<Op, String> {
    let mut fields = Fields::new(words)?;
    let op = match name {
        "dir" => {
            let [path] = fields.positional("dir <path>")?;
            Op::Dir(relative(path)?)
        }
        "wav" => {
            let [path] = fields.positional("wav <path> ch= bits= bytes= fill=")?;
            Op::Wav {
                path: relative(path)?,
                channels: fields.take("ch")?,
                bits: fields.take("bits")?,
                data_bytes: fields.take("bytes")?,
                fill: fields.take("fill")?,
            }
        }
        "sine" => {
            let [path] = fields.positional("sine <path> ch= frames= hz=")?;
            Op::Sine {
                path: relative(path)?,
                channels: fields.take("ch")?,
                frames: fields.take("frames")?,
                hz: fields.take("hz")?,
            }
        }
        "partials" => {
            let [path, list] = fields.positional("partials <path> frames= <hz>:<amp>,…")?;
            let partials = list
                .split(',')
                .map(|pair| {
                    let parsed = pair
                        .split_once(':')
                        .and_then(|(hz, amp)| Some((f64::parse(hz)?, f64::parse(amp)?)));
                    parsed.ok_or_else(|| format!("`{pair}` is not <hz>:<amplitude>"))
                })
                .collect::<Result<Vec<_>, _>>()?;
            Op::Partials {
                path: relative(path)?,
                frames: fields.take("frames")?,
                partials,
            }
        }
        "raw" => {
            let [path] = fields.positional("raw <path> bytes= fill=")?;
            Op::Raw {
                path: relative(path)?,
                bytes: fields.take("bytes")?,
                fill: fields.take("fill")?,
            }
        }
        "copy" => {
            let [to] = fields.positional("copy <dst> from=<file>")?;
            Op::Copy {
                to: relative(to)?,
                from: relative(fields.take_str("from")?)?,
            }
        }
        "copyall" => {
            let [to] = fields.positional("copyall <dir> from=<dir> ext=<ext>")?;
            Op::CopyAll {
                to: relative(to)?,
                from: relative(fields.take_str("from")?)?,
                extension: fields.take_str("ext")?.to_string(),
            }
        }
        "base" => {
            let [dir] = fields.positional("base <dir>")?;
            Op::Base(relative(dir)?)
        }
        "delete" => {
            let [path] = fields.positional("delete <path>")?;
            Op::Delete(relative(path)?)
        }
        other => return Err(format!("unknown operation `{other}`")),
    };
    fields.finish()?;
    Ok(op)
}

/// A line's words after the operation: positional ones, and `key=value` ones taken by key.
struct Fields<'a> {
    positional: Vec<&'a str>,
    named: BTreeMap<&'a str, &'a str>,
}

impl<'a> Fields<'a> {
    fn new(words: &[&'a str]) -> Result<Self, String> {
        let mut positional = Vec::new();
        let mut named = BTreeMap::new();
        for word in words {
            match word.split_once('=') {
                Some((key, value)) => {
                    if named.insert(key, value).is_some() {
                        return Err(format!("`{key}=` given twice"));
                    }
                }
                None => positional.push(*word),
            }
        }
        Ok(Self { positional, named })
    }

    fn positional<const N: usize>(&self, usage: &str) -> Result<[&'a str; N], String> {
        <[&str; N]>::try_from(self.positional.as_slice()).map_err(|_| format!("expected `{usage}`"))
    }

    fn take_str(&mut self, key: &str) -> Result<&'a str, String> {
        self.named
            .remove(key)
            .ok_or_else(|| format!("missing `{key}=`"))
    }

    fn take<T: Value>(&mut self, key: &str) -> Result<T, String> {
        let text = self.take_str(key)?;
        T::parse(text).ok_or_else(|| format!("`{key}={text}` is not a valid {}", T::WHAT))
    }

    fn finish(self) -> Result<(), String> {
        match self.named.keys().next() {
            Some(key) => Err(format!("unknown field `{key}=`")),
            None => Ok(()),
        }
    }
}

/// A field's value type.
trait Value: Sized {
    const WHAT: &'static str;
    fn parse(text: &str) -> Option<Self>;
}

fn integer(text: &str) -> Option<u64> {
    match text.strip_prefix("0x") {
        Some(hex) => u64::from_str_radix(hex, 16).ok(),
        None => text.parse().ok(),
    }
}

macro_rules! integer_value {
    ($type:ty, $what:literal) => {
        impl Value for $type {
            const WHAT: &'static str = $what;
            fn parse(text: &str) -> Option<Self> {
                integer(text).and_then(|value| Self::try_from(value).ok())
            }
        }
    };
}

integer_value!(u8, "byte");
integer_value!(u16, "16-bit count");
integer_value!(u32, "32-bit count");

impl Value for f64 {
    const WHAT: &'static str = "number";
    fn parse(text: &str) -> Option<Self> {
        text.parse::<f64>().ok().filter(|value| value.is_finite())
    }
}

/// `text` as a path that cannot leave the directory it is joined to.
fn relative(text: &str) -> Result<PathBuf, String> {
    let path = PathBuf::from(text);
    if path
        .components()
        .all(|c| matches!(c, Component::Normal(_) | Component::CurDir))
    {
        Ok(path)
    } else {
        Err(format!("`{text}` must be a relative path without `..`"))
    }
}

/// `e` with `path` named in its message.
fn named(path: &Path, e: std::io::Error) -> std::io::Error {
    std::io::Error::new(e.kind(), format!("{}: {e}", path.display()))
}

/// Copies the tree at `from` into `to`: every directory, empty ones too, and every file.
fn copy_tree(from: &Path, to: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(to).map_err(|e| named(to, e))?;
    for entry in std::fs::read_dir(from).map_err(|e| named(from, e))? {
        let entry = entry.map_err(|e| named(from, e))?;
        let (source, target) = (entry.path(), to.join(entry.file_name()));
        if entry.file_type().map_err(|e| named(&source, e))?.is_dir() {
            copy_tree(&source, &target)?;
        } else {
            std::fs::copy(&source, &target)
                .map(drop)
                .map_err(|e| named(&source, e))?;
        }
    }
    Ok(())
}

/// Builds `ops` into the directory `into`, creating it; source paths are joined to `repo_root`.
pub fn build(repo_root: &Path, ops: &[Op], into: &Path) -> std::io::Result<()> {
    let write = |path: &Path, bytes: &[u8]| -> std::io::Result<()> {
        let target = into.join(path);
        if let Some(parent) = target.parent() {
            std::fs::create_dir_all(parent).map_err(|e| named(parent, e))?;
        }
        std::fs::write(&target, bytes).map_err(|e| named(&target, e))
    };
    let copy = |from: &Path, to: &Path| -> std::io::Result<()> {
        if let Some(parent) = to.parent() {
            std::fs::create_dir_all(parent).map_err(|e| named(parent, e))?;
        }
        std::fs::copy(from, to)
            .map(drop)
            .map_err(|e| named(from, e))
    };
    std::fs::create_dir_all(into).map_err(|e| named(into, e))?;
    for op in ops {
        match op {
            Op::Dir(path) => {
                std::fs::create_dir_all(into.join(path)).map_err(|e| named(&into.join(path), e))?
            }
            Op::Wav {
                path,
                channels,
                bits,
                data_bytes,
                fill,
            } => write(path, &test_wav(*channels, *bits, *data_bytes, *fill))?,
            Op::Sine {
                path,
                channels,
                frames,
                hz,
            } => write(path, &sine_wav(*channels, *frames, *hz))?,
            Op::Partials {
                path,
                frames,
                partials,
            } => write(path, &partials_wav(*frames, partials))?,
            Op::Raw { path, bytes, fill } => write(path, &vec![*fill; *bytes as usize])?,
            Op::Base(from) => copy_tree(&repo_root.join(from), into)?,
            Op::Text { path, bytes } => write(path, bytes)?,
            Op::Delete(path) => {
                let target = into.join(path);
                let meta = std::fs::symlink_metadata(&target).map_err(|e| named(&target, e))?;
                if meta.is_dir() {
                    std::fs::remove_dir_all(&target)
                } else {
                    std::fs::remove_file(&target)
                }
                .map_err(|e| named(&target, e))?
            }
            Op::Copy { to, from } => copy(&repo_root.join(from), &into.join(to))?,
            Op::CopyAll {
                to,
                from,
                extension,
            } => {
                let source = repo_root.join(from);
                let mut files: Vec<PathBuf> = std::fs::read_dir(&source)
                    .map_err(|e| named(&source, e))?
                    .map(|entry| entry.map(|e| e.path()))
                    .collect::<std::io::Result<_>>()
                    .map_err(|e| named(&source, e))?;
                files.retain(|path| {
                    path.is_file() && path.extension().is_some_and(|e| e == extension.as_str())
                });
                files.sort();
                std::fs::create_dir_all(into.join(to)).map_err(|e| named(&into.join(to), e))?;
                for file in files {
                    copy(
                        &file,
                        &into
                            .join(to)
                            .join(file.file_name().expect("a listed file has a name")),
                    )?;
                }
            }
        }
    }
    Ok(())
}

/// Reads, parses and builds the manifest at `manifest` into `into`; the error names the file and, for a parse
/// error, the line.
pub fn build_file(repo_root: &Path, manifest: &Path, into: &Path) -> Result<(), String> {
    let text =
        std::fs::read_to_string(manifest).map_err(|e| format!("{}: {e}", manifest.display()))?;
    let ops = parse(manifest, &text).map_err(|e| e.to_string())?;
    build(repo_root, &ops, into).map_err(|e| format!("{}: {e}", manifest.display()))
}

const SAMPLE_RATE: u32 = 44_100;

/// The 44-byte header of a 44.1 kHz PCM WAV.
fn wav_header(channels: u16, bits: u16, data_bytes: u32) -> Vec<u8> {
    let block_align = channels * (bits / 8);
    let mut bytes = Vec::with_capacity(44 + data_bytes as usize);
    bytes.extend_from_slice(b"RIFF");
    bytes.extend_from_slice(&(36 + data_bytes).to_le_bytes());
    bytes.extend_from_slice(b"WAVE");
    bytes.extend_from_slice(b"fmt ");
    bytes.extend_from_slice(&16u32.to_le_bytes());
    bytes.extend_from_slice(&1u16.to_le_bytes()); // PCM
    bytes.extend_from_slice(&channels.to_le_bytes());
    bytes.extend_from_slice(&SAMPLE_RATE.to_le_bytes());
    bytes.extend_from_slice(&(SAMPLE_RATE * u32::from(block_align)).to_le_bytes());
    bytes.extend_from_slice(&block_align.to_le_bytes());
    bytes.extend_from_slice(&bits.to_le_bytes());
    bytes.extend_from_slice(b"data");
    bytes.extend_from_slice(&data_bytes.to_le_bytes());
    bytes
}

/// A WAV of `data_bytes` data bytes, each `fill`.
fn test_wav(channels: u16, bits: u16, data_bytes: u32, fill: u8) -> Vec<u8> {
    let mut bytes = wav_header(channels, bits, data_bytes);
    bytes.resize(44 + data_bytes as usize, fill);
    bytes
}

/// A 16-bit WAV of a sine at `hz`, `frames` frames long, the same on every channel.
fn sine_wav(channels: u16, frames: u32, hz: f64) -> Vec<u8> {
    let mut bytes = wav_header(channels, 16, frames * u32::from(channels) * 2);
    for n in 0..frames {
        let phase = f64::from(n) * hz * std::f64::consts::TAU / f64::from(SAMPLE_RATE);
        let value = (phase.sin() * 16_000.0) as i16;
        for _ in 0..channels {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
    }
    bytes
}

/// A 16-bit mono WAV of `frames` frames summing `partials`, each a (frequency in Hz, amplitude) pair.
fn partials_wav(frames: u32, partials: &[(f64, f64)]) -> Vec<u8> {
    let mut bytes = wav_header(1, 16, frames * 2);
    for n in 0..frames {
        let t = f64::from(n) / f64::from(SAMPLE_RATE);
        let sum: f64 = partials
            .iter()
            .map(|&(hz, amplitude)| amplitude * (t * hz * std::f64::consts::TAU).sin())
            .sum();
        let value = (sum * 30_000.0) as i16;
        bytes.extend_from_slice(&value.to_le_bytes());
    }
    bytes
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse_ok(text: &str) -> Vec<Op> {
        parse(Path::new("t.card"), text).unwrap()
    }

    fn parse_err(text: &str) -> String {
        parse(Path::new("t.card"), text).unwrap_err().to_string()
    }

    /// A fresh directory under the temp dir for one test.
    fn scratch(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("card-manifest-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn parses_every_operation() {
        let ops = parse_ok(
            "dir SONGS\n\
             wav SAMPLES/A.WAV ch=2 bits=24 bytes=3072 fill=0x55\n\
             sine SAMPLES/S.WAV ch=1 frames=22050 hz=86.1328125\n\
             partials SAMPLES/P.WAV frames=44100 196:0.5,392:0.25\n\
             raw SAMPLES/G.WAV bytes=512 fill=0x7E\n\
             copy SAMPLES/C.wav from=tests/x/c.wav\n\
             copyall SAMPLES/WTP from=tests/x ext=WAV\n",
        );
        assert_eq!(
            ops,
            vec![
                Op::Dir(PathBuf::from("SONGS")),
                Op::Wav {
                    path: PathBuf::from("SAMPLES/A.WAV"),
                    channels: 2,
                    bits: 24,
                    data_bytes: 3072,
                    fill: 0x55
                },
                Op::Sine {
                    path: PathBuf::from("SAMPLES/S.WAV"),
                    channels: 1,
                    frames: 22_050,
                    hz: 44_100.0 / 512.0
                },
                Op::Partials {
                    path: PathBuf::from("SAMPLES/P.WAV"),
                    frames: 44_100,
                    partials: vec![(196.0, 0.5), (392.0, 0.25)]
                },
                Op::Raw {
                    path: PathBuf::from("SAMPLES/G.WAV"),
                    bytes: 512,
                    fill: 0x7E
                },
                Op::Copy {
                    to: PathBuf::from("SAMPLES/C.wav"),
                    from: PathBuf::from("tests/x/c.wav")
                },
                Op::CopyAll {
                    to: PathBuf::from("SAMPLES/WTP"),
                    from: PathBuf::from("tests/x"),
                    extension: "WAV".into()
                },
            ]
        );
    }

    #[test]
    fn comments_and_blank_lines_are_skipped() {
        assert_eq!(
            parse_ok("# a card\n\n   \ndir SONGS   # trailing\n"),
            vec![Op::Dir(PathBuf::from("SONGS"))]
        );
    }

    #[test]
    fn errors_name_the_file_and_line() {
        assert_eq!(
            parse_err("dir A\nfrob B\n"),
            "t.card:2: unknown operation `frob`"
        );
    }

    #[test]
    fn a_missing_field_is_an_error() {
        assert_eq!(
            parse_err("wav A.WAV ch=1 bits=16 bytes=4\n"),
            "t.card:1: missing `fill=`"
        );
    }

    #[test]
    fn an_unknown_or_repeated_field_is_an_error() {
        assert_eq!(
            parse_err("raw A bytes=1 fill=0 size=2\n"),
            "t.card:1: unknown field `size=`"
        );
        assert_eq!(
            parse_err("raw A bytes=1 bytes=2 fill=0\n"),
            "t.card:1: `bytes=` given twice"
        );
    }

    #[test]
    fn a_value_out_of_range_is_an_error() {
        assert_eq!(
            parse_err("raw A bytes=1 fill=0x100\n"),
            "t.card:1: `fill=0x100` is not a valid byte"
        );
        assert_eq!(
            parse_err("sine A ch=1 frames=4 hz=inf\n"),
            "t.card:1: `hz=inf` is not a valid number"
        );
    }

    #[test]
    fn the_wrong_number_of_paths_is_an_error() {
        assert_eq!(parse_err("dir\n"), "t.card:1: expected `dir <path>`");
        assert_eq!(parse_err("dir A B\n"), "t.card:1: expected `dir <path>`");
    }

    #[test]
    fn paths_must_stay_inside_their_root() {
        assert_eq!(
            parse_err("dir ../x\n"),
            "t.card:1: `../x` must be a relative path without `..`"
        );
        assert_eq!(
            parse_err("dir /x\n"),
            "t.card:1: `/x` must be a relative path without `..`"
        );
        assert_eq!(
            parse_err("copy A from=../b\n"),
            "t.card:1: `../b` must be a relative path without `..`"
        );
    }

    #[test]
    fn a_wav_too_large_for_its_header_is_a_parse_error() {
        assert_eq!(
            parse_err("wav A ch=1 bits=16 bytes=4294967295 fill=0\n"),
            "t.card:1: `wav` makes a WAV larger than 4 GiB"
        );
        assert_eq!(
            parse_err("sine A ch=2 frames=1100000000 hz=1\n"),
            "t.card:1: `sine` makes a WAV larger than 4 GiB"
        );
        assert_eq!(
            parse_err("partials A frames=2147483648 100:0.5\n"),
            "t.card:1: `partials` makes a WAV larger than 4 GiB"
        );
        assert_eq!(
            parse_err("wav A ch=65535 bits=16 bytes=4 fill=0\n"),
            "t.card:1: `wav` makes a block align larger than 65535"
        );
    }

    #[test]
    fn test_wav_is_a_header_and_a_fill() {
        let bytes = test_wav(2, 24, 6, 0xE2);
        assert_eq!(bytes.len(), 50);
        assert_eq!(&bytes[0..4], b"RIFF");
        assert_eq!(u32::from_le_bytes(bytes[4..8].try_into().unwrap()), 42);
        assert_eq!(u16::from_le_bytes(bytes[22..24].try_into().unwrap()), 2); // channels
        assert_eq!(
            u32::from_le_bytes(bytes[28..32].try_into().unwrap()),
            44_100 * 6
        ); // byte rate
        assert_eq!(u16::from_le_bytes(bytes[32..34].try_into().unwrap()), 6); // block align
        assert_eq!(u16::from_le_bytes(bytes[34..36].try_into().unwrap()), 24); // bits
        assert_eq!(u32::from_le_bytes(bytes[40..44].try_into().unwrap()), 6); // data bytes
        assert!(bytes[44..].iter().all(|&b| b == 0xE2));
    }

    #[test]
    fn sine_wav_repeats_each_frame_on_every_channel() {
        // A quarter cycle per frame: frame 1 is the crest, 16,000.
        let bytes = sine_wav(2, 4, 11_025.0);
        assert_eq!(bytes.len(), 44 + 4 * 2 * 2);
        assert_eq!(
            &bytes[48..52],
            &[16_000i16.to_le_bytes(), 16_000i16.to_le_bytes()].concat()[..]
        );
    }

    #[test]
    fn partials_wav_is_mono_16_bit_scaled_by_30000() {
        let bytes = partials_wav(2, &[(11_025.0, 0.5)]);
        assert_eq!(bytes.len(), 44 + 2 * 2);
        assert_eq!(u16::from_le_bytes(bytes[22..24].try_into().unwrap()), 1);
        let crest = i16::from_le_bytes(bytes[46..48].try_into().unwrap());
        assert!((14_999..=15_000).contains(&crest), "{crest}");
    }

    #[test]
    fn builds_every_operation_into_a_tree() {
        let repo = scratch("repo");
        std::fs::create_dir_all(repo.join("src")).unwrap();
        for (name, text) in [
            ("B.WAV", "b"),
            ("A.WAV", "a"),
            ("c.wav", "c"),
            ("d.txt", "d"),
        ] {
            std::fs::write(repo.join("src").join(name), text).unwrap();
        }
        let card = scratch("card");
        let ops = parse_ok(
            "dir KITS\n\
             wav SAMPLES/W.WAV ch=1 bits=16 bytes=4 fill=0x11\n\
             raw SAMPLES/E.WAV bytes=0 fill=0\n\
             copy SAMPLES/ONE.WAV from=src/c.wav\n\
             copyall SAMPLES/ALL from=src ext=WAV\n",
        );
        build(&repo, &ops, &card).unwrap();
        let read = |p: &str| std::fs::read(card.join(p)).unwrap();
        assert!(card.join("KITS").is_dir());
        assert_eq!(read("SAMPLES/W.WAV"), test_wav(1, 16, 4, 0x11));
        assert!(read("SAMPLES/E.WAV").is_empty());
        assert_eq!(read("SAMPLES/ONE.WAV"), b"c");
        let mut all: Vec<_> = std::fs::read_dir(card.join("SAMPLES/ALL"))
            .unwrap()
            .map(|e| e.unwrap().file_name())
            .collect();
        all.sort();
        assert_eq!(all, ["A.WAV", "B.WAV"]);
        let _ = std::fs::remove_dir_all(&repo);
        let _ = std::fs::remove_dir_all(&card);
    }

    #[test]
    fn a_missing_source_names_it() {
        let card = scratch("missing");
        let err = build(
            Path::new("/nonexistent-repo"),
            &parse_ok("copy A from=b/c\n"),
            &card,
        )
        .unwrap_err();
        assert!(err.to_string().contains("/nonexistent-repo/b/c"), "{err}");
        let _ = std::fs::remove_dir_all(&card);
    }

    #[test]
    fn parses_base_text_and_delete() {
        let ops = parse_ok(
            "base tests/fixtures/card\n\
             delete SAMPLES\n\
             text SAMPLES <<EOF\n\
             not a folder\n\
             \t# kept, tab and all\n\
             EOF\n\
             dir KITS\n",
        );
        assert_eq!(
            ops,
            vec![
                Op::Base(PathBuf::from("tests/fixtures/card")),
                Op::Delete(PathBuf::from("SAMPLES")),
                Op::Text {
                    path: PathBuf::from("SAMPLES"),
                    bytes: b"not a folder\n\t# kept, tab and all\n".to_vec()
                },
                Op::Dir(PathBuf::from("KITS")),
            ]
        );
    }

    #[test]
    fn base_comes_first_or_not_at_all() {
        assert_eq!(
            parse_err("dir A\nbase tests/fixtures/card\n"),
            "t.card:2: `base` must be the first operation"
        );
    }

    #[test]
    fn a_text_heredoc_must_end() {
        assert_eq!(
            parse_err("text A <<EOF\nline\n"),
            "t.card:1: `text` never reaches its closing `EOF` line"
        );
        assert_eq!(
            parse_err("text A\n"),
            "t.card:1: expected `text <path> <<MARK`"
        );
        assert_eq!(
            parse_err("text A <<\n"),
            "t.card:1: expected `text <path> <<MARK`"
        );
    }

    #[test]
    fn an_empty_heredoc_writes_an_empty_file() {
        assert_eq!(
            parse_ok("text A <<END\nEND\n"),
            vec![Op::Text {
                path: PathBuf::from("A"),
                bytes: Vec::new()
            }]
        );
    }

    #[test]
    fn builds_a_base_tree_with_a_path_replaced() {
        let repo = scratch("base-repo");
        for (path, text) in [
            ("card/KITS/.keep", ""),
            ("card/SAMPLES/T.WAV", "t"),
            ("card/SONGS/.keep", ""),
        ] {
            let file = repo.join(path);
            std::fs::create_dir_all(file.parent().unwrap()).unwrap();
            std::fs::write(file, text).unwrap();
        }
        std::fs::create_dir_all(repo.join("card/EMPTY")).unwrap();
        let card = scratch("base-card");
        let ops = parse_ok("base card\ndelete SAMPLES\ntext SAMPLES <<EOF\nnot a folder\nEOF\n");
        build(&repo, &ops, &card).unwrap();
        assert_eq!(
            std::fs::read(card.join("SAMPLES")).unwrap(),
            b"not a folder\n"
        );
        assert_eq!(std::fs::read(card.join("KITS/.keep")).unwrap(), b"");
        assert!(card.join("SONGS/.keep").is_file());
        assert!(card.join("EMPTY").is_dir());
        let _ = std::fs::remove_dir_all(&repo);
        let _ = std::fs::remove_dir_all(&card);
    }

    #[test]
    fn deleting_a_missing_path_names_it() {
        let card = scratch("delete-missing");
        let err = build(
            Path::new("/nonexistent-repo"),
            &parse_ok("delete GONE\n"),
            &card,
        )
        .unwrap_err();
        assert!(err.to_string().contains("GONE"), "{err}");
        let _ = std::fs::remove_dir_all(&card);
    }

    #[test]
    fn every_committed_manifest_parses_and_builds() {
        let repo = Path::new(env!("CARGO_MANIFEST_DIR"))
            .ancestors()
            .nth(3)
            .expect("src/bsp/rust is 3 below the repo root");
        let mut manifests: Vec<PathBuf> = ["tests/integration/scenarios", "tests/fixtures"]
            .into_iter()
            .flat_map(|dir| std::fs::read_dir(repo.join(dir)).unwrap())
            .map(|entry| entry.unwrap().path())
            .filter(|path| path.extension().is_some_and(|e| e == "card"))
            .collect();
        manifests.sort();
        assert!(
            !manifests.is_empty(),
            "no .card file under tests/integration/scenarios or tests/fixtures"
        );
        for manifest in manifests {
            let card = scratch(&format!(
                "committed-{}",
                manifest.file_stem().unwrap().to_string_lossy()
            ));
            build_file(repo, &manifest, &card).unwrap_or_else(|e| panic!("{e}"));
            let _ = std::fs::remove_dir_all(&card);
        }
    }
}
