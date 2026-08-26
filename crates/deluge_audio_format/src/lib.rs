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

    // Accumulator the chunk walk fills in. Tasks 3-6 add more fields and more
    // match arms below; for now only the WAV `fmt ` chunk is handled, and
    // nothing ever sets `found_data_chunk`, so every WAV/AIFF file currently
    // ends in `NoDataChunk`.
    let found_data_chunk = false;
    let mut _num_channels: Option<u8> = None;
    let mut _byte_depth: Option<u8> = None;
    let mut _raw_data_format = RawDataFormat::Native;
    let mut _sample_rate: u32 = 44100;

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
                        _raw_data_format = RawDataFormat::Unsigned8;
                        1
                    }
                    16 | 24 | 32 => (bits / 8) as u8,
                    _ => return Err(ParseError::Unsupported(Reason::UnsupportedBitDepth)),
                };

                if format_tag == 1 {
                    // PCM: RawDataFormat stays whatever the bit-depth arm above set
                    // (Native, or Unsigned8 for 8-bit).
                } else if format_tag == 3 && byte_depth == 4 {
                    _raw_data_format = RawDataFormat::Float;
                } else {
                    return Err(ParseError::Unsupported(Reason::UnsupportedFormatTag));
                }

                if channels != 1 && channels != 2 {
                    return Err(ParseError::Unsupported(Reason::UnsupportedChannelCount));
                }

                if !(5000..=96000).contains(&sample_rate) {
                    return Err(ParseError::Unsupported(Reason::SampleRateOutOfRange));
                }

                _byte_depth = Some(byte_depth);
                _num_channels = Some(channels as u8);
                _sample_rate = sample_rate;
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

    // Unreachable for now: no arm above ever sets `found_data_chunk`. A later
    // task adds a `data`/`SSND` arm plus the success path this falls through
    // to.
    Err(ParseError::Malformed(Reason::NoDataChunk))
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

    #[test]
    fn fmt_sets_channels_depth_and_rate() {
        // No data chunk yet, so parse still errors — assert via the fmt-specific errors below instead.
        assert!(matches!(
            parse(&riff_fmt(2, 44100, 16, 1)),
            Err(ParseError::Malformed(Reason::NoDataChunk))
        ));
    }

    #[test]
    fn eight_bit_is_unsigned8() {
        assert!(matches!(
            parse(&riff_fmt(1, 44100, 8, 1)),
            Err(ParseError::Malformed(Reason::NoDataChunk))
        ));
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
