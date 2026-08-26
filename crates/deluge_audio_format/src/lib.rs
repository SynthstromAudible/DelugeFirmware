//! Audio-file header decoding: WAV/RIFF and AIFF/FORM.
//!
//! A pure function over a byte prefix. No I/O, no allocation, no async, and no
//! panic on any input — the caller owns all reading, and
//! [`ParseError::NeedAtLeast`] asks it for more bytes. That shape is the point:
//! it lets an async task drive the parse, which the pull-based C++ parser this
//! replaces could not, and it makes the decoder trivially fuzzable.
//!
//! Wavetable *acceptance* deliberately lives elsewhere ([`wavetable`]): this
//! module reports what a file says, not whether some caller may use it.

#![no_std]
#![forbid(unsafe_code)]

#[cfg(test)]
extern crate std;

/// Which container the file uses. Decides endianness for everything after the
/// 12-byte top header: RIFF is little-endian, AIFF big-endian.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Container {
    Wav = 0,
    Aiff = 1,
}

/// How the audio bytes are encoded relative to what the engine wants. The
/// `EndiannessWrong*` variants record that the samples need byte-swapping —
/// AIFF PCM is big-endian, so it always lands on one of them.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum RawDataFormat {
    Native = 0,
    Float = 1,
    Unsigned8 = 2,
    EndiannessWrong16 = 3,
    EndiannessWrong24 = 4,
    EndiannessWrong32 = 5,
}

/// Why a file could not be decoded. Separate from [`ParseError`] so the same
/// reason can be reported as either malformed (the file contradicts itself) or
/// unsupported (the file is valid but says something we cannot use).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Reason {
    NoFmtChunk,
    NoDataChunk,
    UnsupportedBitDepth,
    UnsupportedFormatTag,
    UnsupportedChannelCount,
    SampleRateOutOfRange,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ParseError {
    /// Not a failure: the caller must supply at least this many bytes from the
    /// start of the file and call again. Chunk order is not guaranteed, so a
    /// header can need an arbitrary prefix before it is complete.
    NeedAtLeast(u32),
    /// No container we recognise. Distinct from `Malformed` so a caller can
    /// tell "not an audio file" from "a broken audio file".
    NotAudio,
    Malformed(Reason),
    Unsupported(Reason),
}

/// Hints a file gives about being a wavetable. Facts only — whether they are
/// *sufficient* is [`wavetable::accept_as_wavetable`]'s decision.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WavetableHints {
    /// Samples per cycle. Defaults to 2048 when the file says nothing, matching
    /// the C++ parser, whose Serum convention this mirrors.
    pub cycle_size: u32,
    /// A `clm ` tag was present, i.e. the file declares itself a wavetable.
    pub explicitly_specifies_self: bool,
}

/// Everything a header tells us. A plain value: no behaviour, no ownership.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AudioHeader {
    pub container: Container,
    pub num_channels: u8,
    pub byte_depth: u8,
    pub raw_data_format: RawDataFormat,
    pub sample_rate: u32,
    /// Offset from the start of the file to the first audio byte.
    pub audio_data_start_pos_bytes: u32,
    pub audio_data_length_bytes: u32,
    /// 0 == no loop. That is the WAV/AIFF convention (a degenerate 0,0 range),
    /// not a sentinel of ours, and the two can be set independently.
    pub file_loop_start_samples: u32,
    pub file_loop_end_samples: u32,
    /// `None` == the file specified no root note.
    pub midi_note: Option<f32>,
    pub wavetable: WavetableHints,
}

/// Read a little-endian `u32` at `pos`, or `None` if `bytes` is too short.
/// Bounds-checked: no panic, ever.
fn read_u32_le(bytes: &[u8], pos: usize) -> Option<u32> {
    let end = pos.checked_add(4)?;
    let word = bytes.get(pos..end)?;
    Some(u32::from_le_bytes(word.try_into().ok()?))
}

/// Read a big-endian `u32` at `pos`, or `None` if `bytes` is too short.
/// Bounds-checked: no panic, ever.
fn read_u32_be(bytes: &[u8], pos: usize) -> Option<u32> {
    let end = pos.checked_add(4)?;
    let word = bytes.get(pos..end)?;
    Some(u32::from_be_bytes(word.try_into().ok()?))
}

/// Decode the header at the start of `bytes`.
///
/// Returns [`ParseError::NeedAtLeast`] when the prefix is too short to finish;
/// the caller reads that many bytes from offset 0 and calls again.
pub fn parse(bytes: &[u8]) -> Result<AudioHeader, ParseError> {
    const TOP_HEADER_LEN: usize = 12;

    if bytes.len() < TOP_HEADER_LEN {
        return Err(ParseError::NeedAtLeast(TOP_HEADER_LEN as u32));
    }

    let container = match (&bytes[0..4], &bytes[8..12]) {
        (b"RIFF", b"WAVE") => Container::Wav,
        (b"FORM", b"AIFF") => Container::Aiff,
        _ => return Err(ParseError::NotAudio),
    };

    // Bytes 4..8 declare how many bytes follow them: the 4-byte format id
    // plus every chunk after it. A truthful, fully-supplied empty container
    // is self-describing — `bytes.len()` equals `8 + declared`. That's the
    // only signal available for telling a genuinely empty container (no
    // chunks at all) apart from a prefix truncated before its first chunk
    // header; once the walk has read a real chunk we no longer need it (see
    // below).
    let declared_after_top = match container {
        Container::Wav => read_u32_le(bytes, 4),
        Container::Aiff => read_u32_be(bytes, 4),
    }
    .unwrap_or(u32::MAX); // Always Some: bytes.len() >= TOP_HEADER_LEN is guaranteed above.
    let empty_container_is_fully_supplied =
        8usize.checked_add(declared_after_top as usize) == Some(bytes.len());

    // Accumulator the chunk walk fills in. Tasks 5-6 add more fields and more
    // match arms below; for now the WAV `fmt `, `data`, `smpl` and `inst`
    // chunks are handled.
    let mut found_data_chunk = false;
    let mut num_channels_acc: Option<u8> = None;
    let mut byte_depth_acc: Option<u8> = None;
    let mut raw_data_format_acc = RawDataFormat::Native;
    let mut sample_rate_acc: u32 = 44100;
    let mut audio_data_start_pos_bytes_acc: u32 = 0;
    let mut audio_data_length_bytes_acc: u32 = 0;
    let mut file_loop_start_samples_acc: u32 = 0;
    let mut file_loop_end_samples_acc: u32 = 0;
    let mut midi_note_acc: Option<f32> = None;
    let mut wavetable_cycle_size_acc: u32 = 2048;
    let mut wavetable_explicitly_specifies_self_acc = false;

    let mut pos: usize = TOP_HEADER_LEN;
    // Once we've walked at least one real chunk, running out of `bytes`
    // exactly on a chunk boundary is unambiguous: that's the end of the
    // container, whatever bytes 4..8 claim.
    let mut saw_a_chunk = false;

    while pos < bytes.len() {
        // A chunk header the caller can't possibly grow into (pos overflowed
        // usize) can never be satisfied; report the largest ask we can.
        let tag_end = match pos.checked_add(4) {
            Some(n) => n,
            None => return Err(ParseError::NeedAtLeast(u32::MAX)),
        };
        let size_end = match tag_end.checked_add(4) {
            Some(n) => n,
            None => return Err(ParseError::NeedAtLeast(u32::MAX)),
        };
        let tag = match bytes.get(pos..tag_end) {
            Some(t) => t,
            None => return Err(ParseError::NeedAtLeast(size_end as u32)),
        };
        let raw_size = match read_u32_le(bytes, tag_end) {
            Some(s) => s,
            None => return Err(ParseError::NeedAtLeast(size_end as u32)),
        };
        saw_a_chunk = true;

        let data_start = size_end;
        // RIFF chunks are word-aligned: an odd payload has a trailing pad
        // byte. `raw_size` comes straight from the file, so `raw_size == u32::MAX`
        // (odd) must not overflow when padded — no length any real buffer
        // could satisfy anyway, so report the largest possible ask.
        let padded_size = match raw_size.checked_add(raw_size & 1) {
            Some(p) => p,
            None => return Err(ParseError::NeedAtLeast(u32::MAX)),
        };

        match (container, tag) {
            (Container::Wav, b"fmt ") => {
                const FMT_LEN: usize = 16;
                let need = match data_start.checked_add(FMT_LEN) {
                    Some(n) => n,
                    None => return Err(ParseError::NeedAtLeast(u32::MAX)),
                };
                if bytes.len() < need {
                    return Err(ParseError::NeedAtLeast(need as u32));
                }

                // fmt chunk layout: wFormatTag:u16, wChannels:u16, dwSampleRate:u32,
                // dwByteRate:u32 (unused), wBlockAlign:u16, wBitsPerSample:u16.
                let word0 = read_u32_le(bytes, data_start).expect("bounds checked above");
                let sample_rate = read_u32_le(bytes, data_start + 4).expect("bounds checked above");
                let word3 = read_u32_le(bytes, data_start + 12).expect("bounds checked above");

                let format_tag = (word0 & 0xFFFF) as u16;
                let channels = (word0 >> 16) as u16;
                let bits = (word3 >> 16) as u16;

                let byte_depth: u8 = match bits {
                    8 => {
                        raw_data_format_acc = RawDataFormat::Unsigned8;
                        1
                    }
                    16 | 24 | 32 => (bits / 8) as u8,
                    _ => return Err(ParseError::Unsupported(Reason::UnsupportedBitDepth)),
                };

                if format_tag == 1 {
                    // PCM: RawDataFormat stays whatever the bit-depth arm above set
                    // (Native, or Unsigned8 for 8-bit).
                } else if format_tag == 3 && byte_depth == 4 {
                    raw_data_format_acc = RawDataFormat::Float;
                } else {
                    return Err(ParseError::Unsupported(Reason::UnsupportedFormatTag));
                }

                if channels != 1 && channels != 2 {
                    return Err(ParseError::Unsupported(Reason::UnsupportedChannelCount));
                }

                if !(5000..=96000).contains(&sample_rate) {
                    return Err(ParseError::Unsupported(Reason::SampleRateOutOfRange));
                }

                byte_depth_acc = Some(byte_depth);
                num_channels_acc = Some(channels as u8);
                sample_rate_acc = sample_rate;
            }
            (Container::Wav, b"data") => {
                found_data_chunk = true;
                // `data_start` is bounds-checked above (it's `size_end`, and the
                // size read at `tag_end..size_end` already succeeded), but cast
                // defensively rather than assume any relationship between
                // `usize` and `u32` on the host running these tests.
                audio_data_start_pos_bytes_acc = u32::try_from(data_start).unwrap_or(u32::MAX);
                audio_data_length_bytes_acc = raw_size;
                // A Sample parse keeps walking after the data chunk (to pick up
                // AIFF loop markers etc. in later tasks) rather than stopping
                // here the way a WaveTable parse would — see
                // audio_file_format.cpp:74-76.
            }
            (Container::Wav, b"smpl") => {
                // smpl header: manufacturer, product, samplePeriod,
                // midiUnityNote, midiPitchFraction, smpteFormat, smpteOffset,
                // numSampleLoops, samplerData — 9 little-endian u32 fields (36
                // bytes). Each field read is independently bounds-checked; a
                // short/truncated chunk just leaves midi_note/loop points
                // unset rather than erroring here (the generic truncation
                // check after the match still asks for more bytes when the
                // file is genuinely incomplete). Mirrors
                // audio_file_format.cpp:214-247.
                if let (Some(midi_unity_note), Some(pitch_fraction)) = (
                    read_u32_le(bytes, data_start + 12),
                    read_u32_le(bytes, data_start + 16),
                ) {
                    if (midi_unity_note != 0 || pitch_fraction != 0) && midi_unity_note < 128 {
                        // audio_file_format.cpp:227-228 — fractional root
                        // note; 2^32 is exactly representable in f32.
                        midi_note_acc =
                            Some(pitch_fraction as f32 / 4_294_967_296.0 + midi_unity_note as f32);
                    }
                }

                // Only a single declared loop is ever honoured: the C++ gates
                // the loop read on `numLoops == 1` rather than looping
                // `numLoops` times (audio_file_format.cpp:231-245), so a
                // hostile declared count (e.g. 0xFFFF_FFFF) never drives any
                // iteration — bounded or not — and never indexes past what a
                // single loop record needs.
                if read_u32_le(bytes, data_start + 28) == Some(1) {
                    // loop record: cuePointId, type, start, end, fraction,
                    // playCount — 6 u32 fields starting right after the
                    // 36-byte header above.
                    if let (Some(loop_start), Some(loop_end)) = (
                        read_u32_le(bytes, data_start + 44),
                        read_u32_le(bytes, data_start + 48),
                    ) {
                        file_loop_start_samples_acc = loop_start;
                        file_loop_end_samples_acc = loop_end;
                    }
                }
            }
            (Container::Wav, b"inst") => {
                // inst chunk: MIDIUnshiftedNote:u8, fineTune:i8 (signed,
                // 1/100th semitone), lowNote, highNote, lowVelocity,
                // highVelocity, gain — only the first two bytes are used.
                // Applied in file order like `smpl` above: whichever of
                // `smpl`/`inst` is walked later simply overwrites
                // `midi_note_acc` set by the other, with no other precedence
                // rule — that's exactly what the C++ does too, since both
                // arms write `f.midiNote` unconditionally when their own
                // guard passes. Mirrors audio_file_format.cpp:251-266.
                if let (Some(&midi_note_byte), Some(&fine_tune_byte)) =
                    (bytes.get(data_start), bytes.get(data_start + 1))
                {
                    if midi_note_byte < 128 {
                        let fine_tune = fine_tune_byte as i8;
                        midi_note_acc = Some(midi_note_byte as f32 - (fine_tune as f32) * 0.01);
                    }
                }
            }
            (Container::Wav, b"clm ") => {
                // Serum wavetable-hint chunk: "<!>" followed by an ASCII
                // decimal cycle-size, e.g. "<!>2048". The C++ oracle reads
                // exactly 7 raw bytes at the chunk payload *regardless of
                // the chunk's own declared length* (audio_file_format.cpp:
                // 270-271) — it never bounds the read to `thisChunk.length`.
                // That's a real over-read quirk, not a typo: per the task
                // brief this field pair is Serum/Deluge convention, not a
                // documented format, so audio_file_format.cpp is normative
                // and we mirror it exactly, quirks included.
                if let Some(chunk_end) = data_start.checked_add(7) {
                    if let Some(raw) = bytes.get(data_start..chunk_end) {
                        // The C++ loads the first 4 bytes as a little-endian
                        // u32 and masks it with 0x00FFFFFF before comparing
                        // to charsToIntegerConstant('<','!','>',0)
                        // (audio_file_format.cpp:276-277). Masking off the
                        // top byte means the comparison only constrains
                        // bytes 0-2 to be "<!>" — the 4th byte is compared
                        // against nothing (both sides are zero there only
                        // because the mask forces it, and the RHS's 4th
                        // char argument is an unused placeholder — there is
                        // no 3-argument overload of charsToIntegerConstant
                        // for a 32-bit tag). So byte index 3 is left
                        // completely unconstrained and, next, gets read as
                        // the *first* digit character below.
                        if raw[0] == b'<' && raw[1] == b'!' && raw[2] == b'>' {
                            wavetable_explicitly_specifies_self_acc = true;
                            // memToUIntOrError parses raw[3..7] — up to 4
                            // ASCII decimal digits immediately following the
                            // tag, with no separator (audio_file_format.cpp:
                            // 281, util/audio_format_helpers.cpp:22-34).
                            // Any non-digit anywhere in the run aborts the
                            // whole parse (mirroring memToUIntOrError's
                            // immediate `return -1`), which the C++ then
                            // only applies when >= 1
                            // (audio_file_format.cpp:282-283). This window
                            // is fixed at 4 bytes, so a u32 can't actually
                            // overflow here, but accumulate with checked
                            // arithmetic anyway rather than trust the window
                            // size never grows out from under this comment.
                            let mut number: u32 = 0;
                            let mut is_number = true;
                            for &digit_byte in &raw[3..7] {
                                if !digit_byte.is_ascii_digit() {
                                    is_number = false;
                                    break;
                                }
                                let digit = u32::from(digit_byte - b'0');
                                number =
                                    match number.checked_mul(10).and_then(|n| n.checked_add(digit))
                                    {
                                        Some(n) => n,
                                        None => {
                                            is_number = false;
                                            break;
                                        }
                                    };
                            }
                            if is_number && number >= 1 {
                                wavetable_cycle_size_acc = number;
                            }
                        }
                    }
                }
            }
            _ => {}
        }

        let next_pos = match data_start.checked_add(padded_size as usize) {
            Some(p) => p,
            None => return Err(ParseError::NeedAtLeast(u32::MAX)),
        };
        // Chunks we don't otherwise inspect (the `_` arm above) are skipped
        // by arithmetic alone, with no read to bounds-check it — so check
        // here that the skip doesn't run past what we've actually been
        // given.
        if next_pos > bytes.len() {
            return Err(ParseError::NeedAtLeast(next_pos as u32));
        }
        pos = next_pos;
    }

    // We've run out of `bytes` exactly on a chunk boundary. If we never
    // walked a real chunk, we can't tell a genuinely empty container from a
    // prefix truncated before its first chunk header — fall back to the
    // top-level size field. Once at least one chunk has been walked, this is
    // unambiguously the end of the container.
    if !saw_a_chunk && !empty_container_is_fully_supplied {
        let next = match pos.checked_add(8) {
            Some(n) => n,
            None => return Err(ParseError::NeedAtLeast(u32::MAX)),
        };
        return Err(ParseError::NeedAtLeast(next as u32));
    }

    if !found_data_chunk {
        return Err(ParseError::Malformed(Reason::NoDataChunk));
    }

    let num_channels = match num_channels_acc {
        Some(c) => c,
        None => return Err(ParseError::Malformed(Reason::NoFmtChunk)),
    };
    let byte_depth = match byte_depth_acc {
        Some(d) => d,
        None => return Err(ParseError::Malformed(Reason::NoFmtChunk)),
    };

    Ok(AudioHeader {
        container,
        num_channels,
        byte_depth,
        raw_data_format: raw_data_format_acc,
        sample_rate: sample_rate_acc,
        audio_data_start_pos_bytes: audio_data_start_pos_bytes_acc,
        audio_data_length_bytes: audio_data_length_bytes_acc,
        file_loop_start_samples: file_loop_start_samples_acc,
        file_loop_end_samples: file_loop_end_samples_acc,
        midi_note: midi_note_acc,
        wavetable: WavetableHints {
            cycle_size: wavetable_cycle_size_acc,
            explicitly_specifies_self: wavetable_explicitly_specifies_self_acc,
        },
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 12-byte RIFF/WAVE container header with no chunks.
    fn riff_empty() -> [u8; 12] {
        let mut b = [0u8; 12];
        b[0..4].copy_from_slice(b"RIFF");
        b[4..8].copy_from_slice(&4u32.to_le_bytes());
        b[8..12].copy_from_slice(b"WAVE");
        b
    }

    /// 12-byte FORM/AIFF container header with no chunks.
    fn form_empty() -> [u8; 12] {
        let mut b = [0u8; 12];
        b[0..4].copy_from_slice(b"FORM");
        b[4..8].copy_from_slice(&4u32.to_be_bytes());
        b[8..12].copy_from_slice(b"AIFF");
        b
    }

    #[test]
    fn short_input_asks_for_the_container_header() {
        for n in 0..12 {
            assert_eq!(
                parse(&riff_empty()[..n]),
                Err(ParseError::NeedAtLeast(12)),
                "len {n}"
            );
        }
    }

    #[test]
    fn unknown_container_is_not_audio() {
        let mut b = riff_empty();
        b[0..4].copy_from_slice(b"XXXX");
        assert_eq!(parse(&b), Err(ParseError::NotAudio));
    }

    #[test]
    fn riff_and_form_are_detected() {
        // No fmt/data yet, so both are Malformed — but NOT NotAudio: the
        // container was recognised.
        assert!(matches!(
            parse(&riff_empty()),
            Err(ParseError::Malformed(_))
        ));
        assert!(matches!(
            parse(&form_empty()),
            Err(ParseError::Malformed(_))
        ));
    }

    #[test]
    fn never_panics_on_arbitrary_bytes() {
        // Every prefix of every byte value: the parser must return, never panic.
        for b in 0u8..=255 {
            for len in 0..64usize {
                let buf = std::vec![b; len];
                let _ = parse(&buf);
            }
        }
    }

    /// RIFF/WAVE + a `fmt ` chunk. `bits` is bits-per-sample, `fmt_tag` 1 = PCM, 3 = float.
    fn riff_fmt(channels: u16, rate: u32, bits: u16, fmt_tag: u16) -> std::vec::Vec<u8> {
        let mut v = std::vec::Vec::new();
        v.extend_from_slice(b"RIFF");
        v.extend_from_slice(&0u32.to_le_bytes());
        v.extend_from_slice(b"WAVE");
        v.extend_from_slice(b"fmt ");
        v.extend_from_slice(&16u32.to_le_bytes());
        v.extend_from_slice(&fmt_tag.to_le_bytes());
        v.extend_from_slice(&channels.to_le_bytes());
        v.extend_from_slice(&rate.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // byte rate (unused)
        v.extend_from_slice(&0u16.to_le_bytes()); // block align (unused)
        v.extend_from_slice(&bits.to_le_bytes());
        v
    }

    /// riff_fmt + a `data` chunk of `len` zero bytes.
    fn riff_fmt_data(channels: u16, rate: u32, bits: u16, len: usize) -> std::vec::Vec<u8> {
        let mut v = riff_fmt(channels, rate, bits, 1);
        v.extend_from_slice(b"data");
        v.extend_from_slice(&(len as u32).to_le_bytes());
        v.resize(v.len() + len, 0);
        v
    }

    #[test]
    fn fmt_sets_channels_depth_and_rate() {
        let h = parse(&riff_fmt_data(2, 44100, 16, 4)).expect("should parse");
        assert_eq!(h.num_channels, 2);
        assert_eq!(h.byte_depth, 2);
        assert_eq!(h.sample_rate, 44100);
    }

    #[test]
    fn eight_bit_is_unsigned8() {
        let h = parse(&riff_fmt_data(1, 44100, 8, 4)).expect("should parse");
        assert_eq!(h.byte_depth, 1);
        assert_eq!(h.raw_data_format, RawDataFormat::Unsigned8);
    }

    #[test]
    fn data_chunk_completes_the_parse() {
        let bytes = riff_fmt_data(2, 44100, 16, 8);
        let h = parse(&bytes).expect("should parse");
        assert_eq!(h.container, Container::Wav);
        assert_eq!(h.num_channels, 2);
        assert_eq!(h.byte_depth, 2);
        assert_eq!(h.raw_data_format, RawDataFormat::Native);
        assert_eq!(h.sample_rate, 44100);
        assert_eq!(h.audio_data_length_bytes, 8);
        // 12 container + 8 fmt header + 16 fmt payload + 8 data header = 44
        assert_eq!(h.audio_data_start_pos_bytes, 44);
        assert_eq!(h.file_loop_start_samples, 0);
        assert_eq!(h.file_loop_end_samples, 0);
        assert_eq!(h.midi_note, None);
    }

    #[test]
    fn data_before_fmt_is_still_malformed() {
        let mut v = std::vec::Vec::new();
        v.extend_from_slice(b"RIFF");
        v.extend_from_slice(&0u32.to_le_bytes());
        v.extend_from_slice(b"WAVE");
        v.extend_from_slice(b"data");
        v.extend_from_slice(&0u32.to_le_bytes());
        assert_eq!(parse(&v), Err(ParseError::Malformed(Reason::NoFmtChunk)));
    }

    #[test]
    fn float_only_allowed_at_32_bit() {
        assert_eq!(
            parse(&riff_fmt(1, 44100, 16, 3)),
            Err(ParseError::Unsupported(Reason::UnsupportedFormatTag))
        );
        assert!(matches!(
            parse(&riff_fmt(1, 44100, 32, 3)),
            Err(ParseError::Malformed(Reason::NoDataChunk))
        ));
    }

    #[test]
    fn rejects_bad_depth_channels_and_rate() {
        assert_eq!(
            parse(&riff_fmt(1, 44100, 12, 1)),
            Err(ParseError::Unsupported(Reason::UnsupportedBitDepth))
        );
        assert_eq!(
            parse(&riff_fmt(3, 44100, 16, 1)),
            Err(ParseError::Unsupported(Reason::UnsupportedChannelCount))
        );
        assert_eq!(
            parse(&riff_fmt(1, 4000, 16, 1)),
            Err(ParseError::Unsupported(Reason::SampleRateOutOfRange))
        );
        assert_eq!(
            parse(&riff_fmt(1, 96001, 16, 1)),
            Err(ParseError::Unsupported(Reason::SampleRateOutOfRange))
        );
    }

    #[test]
    fn truncated_mid_chunk_asks_for_more() {
        let full = riff_fmt(1, 44100, 16, 1);
        for n in 12..full.len() {
            match parse(&full[..n]) {
                Err(ParseError::NeedAtLeast(k)) => assert!(k as usize > n, "len {n} asked for {k}"),
                other => panic!("len {n}: expected NeedAtLeast, got {other:?}"),
            }
        }
    }

    /// riff_fmt_data + a `smpl` chunk carrying one loop and a MIDI unity note.
    fn riff_with_smpl(loop_start: u32, loop_end: u32, unity_note: u32) -> std::vec::Vec<u8> {
        let mut v = riff_fmt_data(1, 44100, 16, 8);
        v.extend_from_slice(b"smpl");
        v.extend_from_slice(&60u32.to_le_bytes()); // 36-byte smpl header + one 24-byte loop
        v.extend_from_slice(&0u32.to_le_bytes()); // manufacturer
        v.extend_from_slice(&0u32.to_le_bytes()); // product
        v.extend_from_slice(&0u32.to_le_bytes()); // sample period
        v.extend_from_slice(&unity_note.to_le_bytes()); // MIDI unity note
        v.extend_from_slice(&0u32.to_le_bytes()); // pitch fraction
        v.extend_from_slice(&0u32.to_le_bytes()); // SMPTE format
        v.extend_from_slice(&0u32.to_le_bytes()); // SMPTE offset
        v.extend_from_slice(&1u32.to_le_bytes()); // num sample loops
        v.extend_from_slice(&0u32.to_le_bytes()); // sampler data
        v.extend_from_slice(&0u32.to_le_bytes()); // loop: cue point id
        v.extend_from_slice(&0u32.to_le_bytes()); // loop: type
        v.extend_from_slice(&loop_start.to_le_bytes());
        v.extend_from_slice(&loop_end.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // loop: fraction
        v.extend_from_slice(&0u32.to_le_bytes()); // loop: play count
        v
    }

    #[test]
    fn smpl_sets_loop_points_and_root_note() {
        let h = parse(&riff_with_smpl(100, 200, 60)).expect("should parse");
        assert_eq!(h.file_loop_start_samples, 100);
        assert_eq!(h.file_loop_end_samples, 200);
        assert_eq!(h.midi_note, Some(60.0));
    }

    #[test]
    fn absent_smpl_leaves_loops_zero_and_note_none() {
        let h = parse(&riff_fmt_data(1, 44100, 16, 8)).expect("should parse");
        assert_eq!((h.file_loop_start_samples, h.file_loop_end_samples), (0, 0));
        assert_eq!(h.midi_note, None);
    }

    /// riff_fmt_data + an `inst` chunk carrying a root note and fine-tune.
    fn riff_with_inst(midi_note: u8, fine_tune: i8) -> std::vec::Vec<u8> {
        let mut v = riff_fmt_data(1, 44100, 16, 8);
        v.extend_from_slice(b"inst");
        v.extend_from_slice(&7u32.to_le_bytes());
        v.push(midi_note);
        v.push(fine_tune as u8);
        v.extend_from_slice(&[0u8; 5]); // lowNote, highNote, lowVelocity, highVelocity, gain
        v.push(0); // RIFF word-alignment pad (7-byte payload is odd)
        v
    }

    #[test]
    fn inst_sets_root_note_with_fine_tune() {
        let h = parse(&riff_with_inst(60, -50)).expect("should parse");
        // audio_file_format.cpp:262 — midiNote - fineTune * 0.01
        assert_eq!(h.midi_note, Some(60.0 - (-50.0) * 0.01));
    }

    #[test]
    fn inst_note_out_of_range_is_ignored() {
        let h = parse(&riff_with_inst(200, 0)).expect("should parse");
        // 200 as a raw byte is >= 128, so the C++ guard (`midiNote < 128`)
        // rejects it and midi_note stays unset.
        assert_eq!(h.midi_note, None);
    }

    /// riff_fmt_data + a `clm ` chunk whose ASCII payload declares samples-per-cycle.
    fn riff_with_clm(payload: &str) -> std::vec::Vec<u8> {
        let mut v = riff_fmt_data(1, 44100, 16, 8);
        v.extend_from_slice(b"clm ");
        v.extend_from_slice(&(payload.len() as u32).to_le_bytes());
        v.extend_from_slice(payload.as_bytes());
        if payload.len() % 2 == 1 {
            v.push(0);
        }
        v
    }

    #[test]
    fn clm_declares_a_wavetable() {
        let h = parse(&riff_with_clm("<!>2048 00000000 wavetable")).expect("should parse");
        assert!(h.wavetable.explicitly_specifies_self);
        assert_eq!(h.wavetable.cycle_size, 2048);
    }

    #[test]
    fn absent_clm_means_no_declaration() {
        let h = parse(&riff_fmt_data(1, 44100, 16, 8)).expect("should parse");
        assert!(!h.wavetable.explicitly_specifies_self);
        // The C++ default is 2048 even with no clm chunk (audio_file_format.cpp:52).
        assert_eq!(h.wavetable.cycle_size, 2048);
    }

    #[test]
    fn clm_digit_overflow_does_not_panic() {
        // The C++ oracle only ever reads 4 digit characters after "<!>" (a
        // fixed 7-byte payload read, audio_file_format.cpp:270-281), so this
        // can't overflow a u32 even naively. But a naive Rust port might
        // instead walk the *whole* declared chunk payload as digits — this
        // chunk's ASCII payload is a long run of digits, well beyond what
        // fits in a u32 if accumulated without checked/saturating arithmetic.
        // Assert only that parse returns rather than panicking.
        let mut payload = std::string::String::from("<!>");
        payload.push_str(&"9".repeat(200));
        let _ = parse(&riff_with_clm(&payload));
    }

    #[test]
    fn later_chunk_wins_when_smpl_and_inst_both_set_the_note() {
        // smpl before inst in the file: inst, appearing later in the walk,
        // overwrites smpl's note — the walk applies both arms in file order
        // with no extra precedence rule (audio_file_format.cpp:213-266).
        let mut v = riff_with_smpl(0, 0, 60);
        v.extend_from_slice(b"inst");
        v.extend_from_slice(&7u32.to_le_bytes());
        v.push(72);
        v.push(0);
        v.extend_from_slice(&[0u8; 5]);
        v.push(0); // RIFF word-alignment pad (7-byte payload is odd)
        let h = parse(&v).expect("should parse");
        assert_eq!(h.midi_note, Some(72.0));
    }

    #[test]
    fn hostile_smpl_loop_count_does_not_panic_or_hang() {
        // A `smpl` chunk that declares an enormous loop count (0xFFFF_FFFF)
        // while its actual payload holds only the 36-byte header — no loop
        // records at all. The C++ reference gates the loop read on
        // `numLoops == 1` (audio_file_format.cpp:231), so a count that isn't
        // exactly 1 must never drive any iteration, bounded or not.
        let mut v = riff_fmt_data(1, 44100, 16, 8);
        v.extend_from_slice(b"smpl");
        v.extend_from_slice(&36u32.to_le_bytes()); // header only, no loop data follows
        v.extend_from_slice(&0u32.to_le_bytes()); // manufacturer
        v.extend_from_slice(&0u32.to_le_bytes()); // product
        v.extend_from_slice(&0u32.to_le_bytes()); // sample period
        v.extend_from_slice(&0u32.to_le_bytes()); // MIDI unity note
        v.extend_from_slice(&0u32.to_le_bytes()); // pitch fraction
        v.extend_from_slice(&0u32.to_le_bytes()); // SMPTE format
        v.extend_from_slice(&0u32.to_le_bytes()); // SMPTE offset
        v.extend_from_slice(&0xFFFF_FFFFu32.to_le_bytes()); // num sample loops: hostile
        v.extend_from_slice(&0u32.to_le_bytes()); // sampler data
                                                  // No loop records follow.
        let _ = parse(&v); // Must return promptly, not panic and not hang.
    }

    #[test]
    fn huge_chunk_size_does_not_panic() {
        // A chunk size near u32::MAX must not overflow when word-aligned
        // (`raw_size + (raw_size & 1)`). None of these declared sizes can be
        // satisfied by any real buffer, so any `Err` is fine here — the
        // point is that `parse` returns at all instead of panicking.
        for size in [0xFFFFFFFFu32, 0xFFFFFFFEu32, 0x7FFFFFFFu32] {
            let mut v = std::vec::Vec::new();
            v.extend_from_slice(b"RIFF");
            v.extend_from_slice(&0u32.to_le_bytes());
            v.extend_from_slice(b"WAVE");
            v.extend_from_slice(b"xxxx"); // Not "fmt " - skipped by arithmetic alone.
            v.extend_from_slice(&size.to_le_bytes());
            assert!(parse(&v).is_err(), "size {size:#x} should error, not panic");
        }
    }
}
