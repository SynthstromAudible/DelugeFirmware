//! Audio-file header decoding: WAV/RIFF and AIFF/FORM.
//!
//! No I/O, no allocation, no async, and no panic on any input. The caller owns
//! all reading; [`HeaderParser`] owns the walk and asks for one small window at
//! a time. That shape is the point: it lets an async task drive the parse,
//! which the pull-based C++ parser this replaces could not, and it makes the
//! decoder trivially fuzzable.
//!
//! **Windows, not prefixes.** The parser asks for at most
//! [`MAX_WINDOW_BYTES`] at a time, at an offset of its choosing, and steps over
//! the audio payload by arithmetic — it is never read, never buffered. Asking
//! instead for a growing *prefix* would have made bounded-memory decoding
//! impossible: a chunk's payload sits between the caller and the next chunk
//! header, so over `~/Deluge Backup` the required prefix was 100% of the file
//! at the median, p90 and maximum — up to 183 MB for one file, on a device with
//! 8 MB of SDRAM. The C++ parser being replaced here *seeks*; losing that and
//! substituting contiguity was the defect this API shape fixes.
//!
//! [`parse`] remains as a convenience for callers that hold a whole file (tests,
//! tools, the corpus differential).
//!
//! Wavetable *acceptance* deliberately lives elsewhere ([`wavetable`]): this
//! module reports what a file says, not whether some caller may use it.

#![no_std]
#![forbid(unsafe_code)]

#[cfg(test)]
extern crate std;

pub mod wavetable;

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
    /// Not a failure: [`parse`]'s prefix ran out. The caller must supply at
    /// least this many bytes from the start of the file and call again. Chunk
    /// order is not guaranteed, so a header can need an arbitrary prefix before
    /// it is complete — which is exactly why a streaming caller should drive
    /// [`HeaderParser`] instead, where this means "the file is truncated" and
    /// nothing more.
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

/// Read a big-endian `u16` at `pos`, or `None` if `bytes` is too short.
/// Bounds-checked: no panic, ever.
fn read_u16_be(bytes: &[u8], pos: usize) -> Option<u16> {
    let end = pos.checked_add(2)?;
    let half = bytes.get(pos..end)?;
    Some(u16::from_be_bytes(half.try_into().ok()?))
}

/// Decode a big-endian 80-bit IEEE 754 extended-precision float (the AIFF
/// `COMM` sample rate) straight to a `u32`.
///
/// AIFF sample rates are always small positive integers, which this format
/// represents exactly — so unlike the C++ oracle (`ConvertFromIeeeExtended`
/// in `audio_format_helpers.cpp`), which goes through `f64` and then
/// truncates to `uint32_t` (undefined behaviour in C++ when that `f64` is
/// negative or out of `u32` range), this decodes via integer shifts on the
/// 64-bit mantissa and never touches a float. A hostile exponent (e.g. the
/// all-ones "infinity" encoding, or one that would shift the mantissa out of
/// `u32` range entirely) saturates to `0` or `u32::MAX` rather than
/// panicking; the caller's `5000..=96000` range check then rejects it just
/// as the C++'s own range check rejects its infinite/huge/negative result —
/// so this is a deliberate implementation change, not a spec deviation.
fn decode_aiff_extended_rate(bytes: &[u8; 10]) -> u32 {
    let expon = (u16::from(bytes[0] & 0x7F) << 8) | u16::from(bytes[1]);
    let hi = u32::from_be_bytes([bytes[2], bytes[3], bytes[4], bytes[5]]);
    let lo = u32::from_be_bytes([bytes[6], bytes[7], bytes[8], bytes[9]]);

    if expon == 0 && hi == 0 && lo == 0 {
        return 0;
    }
    if expon == 0x7FFF {
        // Infinity or NaN in the source encoding: no finite value to report.
        return u32::MAX;
    }
    if bytes[0] & 0x80 != 0 {
        // Negative "sample rate" is nonsensical; 0 fails the range check
        // below exactly as reliably as any other sentinel would.
        return 0;
    }

    // value == mantissa * 2^exp. The C++ reaches this same quantity via two
    // `ldexp` calls (on the high and low 32-bit halves separately, at -31
    // and a further -32) that combine to the same -63 used here on the
    // unified 64-bit mantissa.
    let mantissa = (u64::from(hi) << 32) | u64::from(lo);
    let exp = i32::from(expon) - 16383 - 63;

    if exp >= 0 {
        // exp is bounded well under 64 by realistic files, but a hostile
        // exponent could ask for a shift the type can't perform: `<<` panics
        // if the shift amount reaches the bit width, in every build profile,
        // not just under overflow-checks.
        let shift = exp as u32;
        if shift >= u64::BITS {
            return u32::MAX; // far beyond any real sample rate
        }
        u32::try_from(mantissa << shift).unwrap_or(u32::MAX)
    } else {
        let shift = (-exp) as u32;
        if shift >= u64::BITS {
            return 0; // shifted away to nothing
        }
        u32::try_from(mantissa >> shift).unwrap_or(u32::MAX)
    }
}

/// The largest window [`HeaderParser`] can ever ask a caller for, in bytes.
///
/// Every request is one of: the 12-byte container header; a chunk header (8
/// bytes, plus at most one preceding alignment pad byte); or a fixed-size slice
/// of one chunk's payload, the largest being `smpl`'s 52 (its loop record's
/// last field ends at offset 52). The AIFF `MARK` table is walked one 7-byte
/// marker record at a time rather than as a whole chunk, so no size a *file*
/// declares can influence how much the caller has to hold.
///
/// This constant is the point of the windowed protocol: peak caller memory is
/// this many bytes, not the file. A caller may always supply more than asked
/// for — a whole SD cluster, say — and the parser will consume as much of it as
/// it can before asking again.
pub const MAX_WINDOW_BYTES: u32 = 52;

const TOP_HEADER_LEN: u32 = 12;
const CHUNK_HEADER_LEN: u32 = 8;

/// AIFF `MARK` marker table bound (audio_file_format.cpp:31, `kMaxNumMarkers`).
/// A fixed-size array, no `alloc`: markers past this bound are ignored exactly
/// as the C++ does (it clamps the declared count down to this before the read
/// loop runs).
const MAX_NUM_MARKERS: usize = 8;

/// An AIFF `MARK` chunk's leading marker count.
const MARK_COUNT_LEN: u32 = 2;
/// One `MARK` marker record's fixed head: id:u16, position:u32, name length:u8.
/// The name itself is only skipped over, never read.
const MARK_ENTRY_LEN: u32 = 7;

/// What the parser wants next, or the finished header.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum FeedResult {
    Done(AudioHeader),
    /// Supply `len` bytes starting at absolute file offset `start`. `len` never
    /// exceeds [`MAX_WINDOW_BYTES`].
    Need {
        start: u32,
        len: u32,
    },
}

/// Which chunk payload a [`Phase::Payload`] is waiting on, and how many bytes
/// of it the corresponding arm reads.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum PayloadKind {
    Fmt,
    Smpl,
    Inst,
    Clm,
    Comm,
    Ssnd,
    AiffInst,
}

impl PayloadKind {
    /// Bytes the arm reads from the chunk's payload start. Deliberately a
    /// constant per chunk type and NOT the chunk's declared length: the C++
    /// oracle reads these fixed extents regardless of what the chunk claims
    /// (see the `clm ` over-read note in the arm below), and a file-declared
    /// length would put a file in charge of the caller's buffer size.
    const fn window_len(self) -> u32 {
        match self {
            PayloadKind::Fmt => 16,
            PayloadKind::Smpl => 52,
            PayloadKind::Inst => 2,
            PayloadKind::Clm => 7,
            PayloadKind::Comm => 18,
            PayloadKind::Ssnd => 8,
            PayloadKind::AiffInst => 14,
        }
    }

    /// Every kind, so the bound below can be checked over all of them.
    const ALL: [PayloadKind; 7] = [
        PayloadKind::Fmt,
        PayloadKind::Smpl,
        PayloadKind::Inst,
        PayloadKind::Clm,
        PayloadKind::Comm,
        PayloadKind::Ssnd,
        PayloadKind::AiffInst,
    ];

    /// Whether a short window is a truncated *header* (ask for more) rather
    /// than simply a chunk that runs out. The mandatory ones are the chunks the
    /// C++ oracle rejects a file over; the rest are metadata whose absence just
    /// leaves a field at its default, mirroring the oracle's bounds-checked
    /// reads that silently skip.
    const fn required(self) -> bool {
        matches!(
            self,
            PayloadKind::Fmt | PayloadKind::Comm | PayloadKind::Ssnd
        )
    }
}

/// [`MAX_WINDOW_BYTES`] is a promise to the caller about its buffer, so it is
/// checked here at compile time rather than left to a test to notice: every
/// window the parser can ask for is one of these, and raising the bound has to
/// be a deliberate edit to the constant, not a side effect of a new chunk arm.
const _: () = {
    let mut i = 0;
    while i < PayloadKind::ALL.len() {
        assert!(PayloadKind::ALL[i].window_len() <= MAX_WINDOW_BYTES);
        i += 1;
    }
    assert!(TOP_HEADER_LEN <= MAX_WINDOW_BYTES);
    assert!(CHUNK_HEADER_LEN + 1 <= MAX_WINDOW_BYTES); // header plus a pad byte
    assert!(MARK_COUNT_LEN <= MAX_WINDOW_BYTES);
    assert!(MARK_ENTRY_LEN <= MAX_WINDOW_BYTES);
};

/// One chunk's payload extent, carried across the states that decode it.
#[derive(Clone, Copy, Debug)]
struct Chunk {
    data_start: u32,
    raw_size: u32,
}

/// Where the walk is. Every non-terminal variant maps to exactly one bounded
/// window request (see `HeaderParser::request`).
#[derive(Clone, Copy, Debug)]
enum Phase {
    /// Wants the 12-byte RIFF/FORM container header.
    Top,
    /// Wants the chunk header at `at + pad`. `pad` is the previous chunk's
    /// trailing alignment byte, requested together with the header so that a
    /// file which omits it (legal when the odd chunk is last) still reads as a
    /// clean end of walk rather than a demand for a byte that isn't there.
    Header {
        at: u32,
        pad: u32,
    },
    Payload {
        kind: PayloadKind,
        chunk: Chunk,
    },
    /// Wants an AIFF `MARK` chunk's 2-byte marker count.
    MarkCount {
        chunk: Chunk,
        chunk_end: u32,
    },
    /// Wants one `MARK` marker record: id, position and name length.
    MarkEntry {
        chunk: Chunk,
        chunk_end: u32,
        cursor: u32,
        index: usize,
        declared: usize,
    },
    Done(AudioHeader),
    Failed(ParseError),
}

/// A resumable WAV/AIFF header decode.
///
/// The caller owns all reading; the parser owns the walk. It asks for one small
/// window at a time ([`FeedResult::Need`]) and is handed the bytes
/// ([`HeaderParser::feed`]) until it reports [`FeedResult::Done`] or fails.
/// Nothing here allocates, blocks, panics or reads a file: the whole state is
/// this fixed-size value, so an Embassy task, a fiber, or a plain loop can each
/// drive it the same way.
///
/// The audio payload is **skipped by arithmetic** — never requested, never
/// buffered. That is the difference between this and a prefix-only protocol,
/// and it is why the peak buffer is [`MAX_WINDOW_BYTES`] rather than the file.
///
/// ```
/// # use deluge_audio_format::{HeaderParser, FeedResult};
/// # fn read_at(_f: &[u8], _s: u32, _l: u32) -> &'static [u8] { &[] }
/// let mut parser = HeaderParser::new();
/// while let Some((start, len)) = parser.pending() {
///     let window = read_at(&[], start, len); // whatever exists at `start`
///     match parser.feed(window, start) {
///         Ok(FeedResult::Done(header)) => { let _ = header; break }
///         Ok(FeedResult::Need { .. }) => continue,
///         Err(_e) => break,
///     }
/// }
/// ```
#[derive(Clone, Debug)]
pub struct HeaderParser {
    phase: Phase,
    container: Container,
    /// Bytes 4..8 of the container header: how many bytes the file claims
    /// follow them. Only consulted to tell a genuinely empty container from a
    /// prefix truncated before its first chunk header.
    declared_after_top: u32,
    saw_a_chunk: bool,
    found_data_chunk: bool,
    num_channels: Option<u8>,
    byte_depth: Option<u8>,
    raw_data_format: RawDataFormat,
    sample_rate: u32,
    audio_data_start_pos_bytes: u32,
    audio_data_length_bytes: u32,
    file_loop_start_samples: u32,
    file_loop_end_samples: u32,
    midi_note: Option<f32>,
    wavetable_cycle_size: u32,
    wavetable_explicitly_specifies_self: bool,
    /// AIFF `MARK`/`INST` marker-resolved loop state. Marker IDs are signed
    /// 16-bit per the AIFF spec (audio_file_format.cpp:93-94 uses int16_t), and
    /// -1 is not a valid marker ID (it's outside the field's 16-bit *unsigned*
    /// wire encoding), so it doubles as "INST not seen yet", exactly mirroring
    /// the C++ default.
    marker_ids: [i16; MAX_NUM_MARKERS],
    marker_positions: [u32; MAX_NUM_MARKERS],
    /// How many entries of the two arrays above actually hold a
    /// successfully-read marker — *not* simply the declared/clamped count.
    num_markers: usize,
    sustain_loop_begin_marker_id: i16,
    sustain_loop_end_marker_id: i16,
}

impl Default for HeaderParser {
    fn default() -> Self {
        Self::new()
    }
}

impl HeaderParser {
    /// A parser positioned at the start of a file, wanting its container header.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            phase: Phase::Top,
            container: Container::Wav,
            declared_after_top: u32::MAX,
            saw_a_chunk: false,
            found_data_chunk: false,
            num_channels: None,
            byte_depth: None,
            raw_data_format: RawDataFormat::Native,
            sample_rate: 44100,
            audio_data_start_pos_bytes: 0,
            audio_data_length_bytes: 0,
            file_loop_start_samples: 0,
            file_loop_end_samples: 0,
            midi_note: None,
            wavetable_cycle_size: 2048,
            wavetable_explicitly_specifies_self: false,
            marker_ids: [0; MAX_NUM_MARKERS],
            marker_positions: [0; MAX_NUM_MARKERS],
            num_markers: 0,
            sustain_loop_begin_marker_id: -1,
            sustain_loop_end_marker_id: -1,
        }
    }

    /// The window `feed` is waiting for as `(start, len)`, or `None` once the
    /// parse has finished (successfully or not).
    #[must_use]
    pub fn pending(&self) -> Option<(u32, u32)> {
        match self.phase {
            Phase::Done(_) | Phase::Failed(_) => None,
            _ => Some(self.request()),
        }
    }

    /// Hand the parser the bytes it asked for.
    ///
    /// `window_start` is the absolute file offset `window` begins at; it should
    /// be the `start` of the pending request. **A window shorter than the
    /// request declares that the file ends inside it** — that is how the parser
    /// learns where the file stops, since it never asks for a total size. A
    /// caller that supplies more than was asked for is welcome to: the parser
    /// consumes as many further requests from the same window as it fully
    /// covers before returning.
    ///
    /// A window that does not reach the requested offset at all is treated as
    /// "nothing lives there", which ends the walk deterministically rather than
    /// re-asking — a misbehaving caller gets a wrong-but-terminating answer,
    /// never a hang.
    pub fn feed(&mut self, window: &[u8], window_start: u32) -> Result<FeedResult, ParseError> {
        loop {
            match self.phase {
                Phase::Done(header) => return Ok(FeedResult::Done(header)),
                Phase::Failed(error) => return Err(error),
                _ => {}
            }

            let (start, len) = self.request();
            let available = window_at(window, window_start, start);
            let usable = &available[..available.len().min(len as usize)];
            if let Err(error) = self.step(usable) {
                self.phase = Phase::Failed(error);
                return Err(error);
            }

            if let Some((next_start, next_len)) = self.pending() {
                if !covers(window, window_start, next_start, next_len) {
                    return Ok(FeedResult::Need {
                        start: next_start,
                        len: next_len,
                    });
                }
            }
        }
    }

    /// The pending window for a non-terminal phase.
    fn request(&self) -> (u32, u32) {
        match self.phase {
            Phase::Top => (0, TOP_HEADER_LEN),
            Phase::Header { at, pad } => (at, pad + CHUNK_HEADER_LEN),
            Phase::Payload { kind, chunk } => (chunk.data_start, kind.window_len()),
            Phase::MarkCount { chunk, .. } => (chunk.data_start, MARK_COUNT_LEN),
            Phase::MarkEntry { cursor, .. } => (cursor, MARK_ENTRY_LEN),
            // Terminal phases are filtered out by `pending`/`feed` before this
            // is reached; a zero-length request is inert if one ever isn't.
            Phase::Done(_) | Phase::Failed(_) => (0, 0),
        }
    }

    fn step(&mut self, win: &[u8]) -> Result<(), ParseError> {
        match self.phase {
            Phase::Top => self.step_top(win),
            Phase::Header { at, pad } => self.step_header(win, at, pad),
            Phase::Payload { kind, chunk } => self.step_payload(win, kind, chunk),
            Phase::MarkCount { chunk, chunk_end } => self.step_mark_count(win, chunk, chunk_end),
            Phase::MarkEntry {
                chunk,
                chunk_end,
                cursor,
                index,
                declared,
            } => self.step_mark_entry(win, chunk, chunk_end, cursor, index, declared),
            Phase::Done(_) | Phase::Failed(_) => Ok(()),
        }
    }

    fn step_top(&mut self, win: &[u8]) -> Result<(), ParseError> {
        if win.len() < TOP_HEADER_LEN as usize {
            return Err(ParseError::NeedAtLeast(TOP_HEADER_LEN));
        }
        self.container = match (&win[0..4], &win[8..12]) {
            (b"RIFF", b"WAVE") => Container::Wav,
            (b"FORM", b"AIFF") => Container::Aiff,
            _ => return Err(ParseError::NotAudio),
        };
        // Bytes 4..8 declare how many bytes follow them: the 4-byte format id
        // plus every chunk after it. A truthful, fully-supplied empty container
        // is self-describing — the file's real end equals `8 + declared`.
        // That's the only signal available for telling a genuinely empty
        // container (no chunks at all) apart from a file truncated before its
        // first chunk header; once the walk has read a real chunk we no longer
        // need it (see `finish`).
        self.declared_after_top = match self.container {
            Container::Wav => read_u32_le(win, 4),
            Container::Aiff => read_u32_be(win, 4),
        }
        .unwrap_or(u32::MAX); // Always Some: the length check above guarantees it.
        self.phase = Phase::Header {
            at: TOP_HEADER_LEN,
            pad: 0,
        };
        Ok(())
    }

    fn step_header(&mut self, win: &[u8], at: u32, pad: u32) -> Result<(), ParseError> {
        // The previous chunk's trailing pad byte is deliberately NOT required
        // to be present: it exists only so the NEXT chunk starts on an even
        // offset, and real-world files routinely omit it when the odd chunk is
        // the last thing in the file — there's nothing to align if there is no
        // next chunk. (Verified against `~/Deluge Backup/SONGS/problem_songs/…/
        // tropic regsnare1.wav`, whose `data` chunk is exactly 249477 bytes —
        // odd — ending exactly at the file's real 249557-byte end with the pad
        // byte never written. Demanding it rejected that complete, valid file.)
        let header = win.get(pad as usize..).unwrap_or(&[]);
        if header.is_empty() {
            // Nothing at this offset: the file ends here, which is the end of
            // the walk — the same conclusion the C++ oracle reaches when its
            // `bytePos < src.size()` loop condition stops holding.
            let eof = at.saturating_add(win.len() as u32);
            return self.finish(eof);
        }
        let header_start = at.saturating_add(pad);
        if header.len() < CHUNK_HEADER_LEN as usize {
            return Err(ParseError::NeedAtLeast(
                header_start.saturating_add(CHUNK_HEADER_LEN),
            ));
        }

        let tag: [u8; 4] = [header[0], header[1], header[2], header[3]];
        // Chunk *sizes* follow the container's endianness too — AIFF is
        // big-endian throughout, not just in its field values.
        let raw_size = match self.container {
            Container::Wav => read_u32_le(header, 4),
            Container::Aiff => read_u32_be(header, 4),
        }
        .unwrap_or(0); // Always Some: the length check above guarantees it.
        self.saw_a_chunk = true;

        let data_start = match header_start.checked_add(CHUNK_HEADER_LEN) {
            Some(v) => v,
            // A chunk header the caller can't possibly grow into can never be
            // satisfied; report the largest ask we can.
            None => return Err(ParseError::NeedAtLeast(u32::MAX)),
        };
        let chunk = Chunk {
            data_start,
            raw_size,
        };

        match (self.container, &tag) {
            (Container::Wav, b"fmt ") => self.wait_for(PayloadKind::Fmt, chunk),
            (Container::Wav, b"data") => {
                self.found_data_chunk = true;
                self.audio_data_start_pos_bytes = data_start;
                self.audio_data_length_bytes = raw_size;
                // A Sample parse keeps walking after the data chunk (to pick up
                // `smpl`/`inst`/`clm ` metadata, which legally follow it) rather
                // than stopping here — see audio_file_format.cpp:74-76. The
                // payload itself is stepped over, never read.
                self.advance_past(chunk)
            }
            (Container::Wav, b"smpl") => self.wait_for(PayloadKind::Smpl, chunk),
            (Container::Wav, b"inst") => self.wait_for(PayloadKind::Inst, chunk),
            (Container::Wav, b"clm ") => self.wait_for(PayloadKind::Clm, chunk),
            (Container::Aiff, b"COMM") => {
                // COMM is exactly 18 bytes. The C++ oracle rejects any other
                // declared length outright rather than reading a partial or
                // extended COMM (audio_file_format.cpp:317-319) — and it does so
                // before looking at the payload at all, so this check precedes
                // the window request.
                if raw_size != PayloadKind::Comm.window_len() {
                    return Err(ParseError::Unsupported(Reason::UnsupportedFormatTag));
                }
                self.wait_for(PayloadKind::Comm, chunk)
            }
            (Container::Aiff, b"SSND") => {
                self.found_data_chunk = true;
                self.wait_for(PayloadKind::Ssnd, chunk)
            }
            (Container::Aiff, b"MARK") => {
                // Bounded to this chunk's own declared payload rather than the
                // rest of the file: the C++ oracle reads from the raw file
                // stream with no such bound, so a corrupted MARK (declared count
                // too high for its payload) can walk it into a neighbouring
                // chunk's bytes. That's an accident of the C++'s single-pass
                // streaming design, not documented AIFF behaviour, so it isn't
                // worth reproducing — we stop at the chunk boundary instead.
                let chunk_end = data_start.saturating_add(raw_size);
                self.phase = Phase::MarkCount { chunk, chunk_end };
                Ok(())
            }
            (Container::Aiff, b"INST") => self.wait_for(PayloadKind::AiffInst, chunk),
            // Chunks we don't inspect are skipped by arithmetic alone.
            _ => self.advance_past(chunk),
        }
    }

    fn wait_for(&mut self, kind: PayloadKind, chunk: Chunk) -> Result<(), ParseError> {
        self.phase = Phase::Payload { kind, chunk };
        Ok(())
    }

    /// Step over the rest of `chunk` — payload included, unread — and line up
    /// the next chunk header. RIFF/IFF chunks are word-aligned, so an odd
    /// payload is followed by a pad byte, folded into the next request.
    fn advance_past(&mut self, chunk: Chunk) -> Result<(), ParseError> {
        let unpadded_end = match chunk.data_start.checked_add(chunk.raw_size) {
            Some(v) => v,
            // `raw_size` comes straight from the file: a hostile size that walks
            // the cursor past the end of the address space can never be
            // satisfied by any buffer.
            None => return Err(ParseError::NeedAtLeast(u32::MAX)),
        };
        self.phase = Phase::Header {
            at: unpadded_end,
            pad: chunk.raw_size & 1,
        };
        Ok(())
    }

    fn step_payload(
        &mut self,
        win: &[u8],
        kind: PayloadKind,
        chunk: Chunk,
    ) -> Result<(), ParseError> {
        if kind.required() && (win.len() as u32) < kind.window_len() {
            return Err(ParseError::NeedAtLeast(
                chunk.data_start.saturating_add(kind.window_len()),
            ));
        }

        match kind {
            PayloadKind::Fmt => {
                // fmt chunk layout: wFormatTag:u16, wChannels:u16, dwSampleRate:u32,
                // dwByteRate:u32 (unused), wBlockAlign:u16, wBitsPerSample:u16.
                let word0 = read_u32_le(win, 0).expect("required window is 16 bytes");
                let sample_rate = read_u32_le(win, 4).expect("required window is 16 bytes");
                let word3 = read_u32_le(win, 12).expect("required window is 16 bytes");

                let format_tag = (word0 & 0xFFFF) as u16;
                let channels = (word0 >> 16) as u16;
                let bits = (word3 >> 16) as u16;

                let byte_depth: u8 = match bits {
                    8 => {
                        self.raw_data_format = RawDataFormat::Unsigned8;
                        1
                    }
                    16 | 24 | 32 => (bits / 8) as u8,
                    _ => return Err(ParseError::Unsupported(Reason::UnsupportedBitDepth)),
                };

                if format_tag == 1 {
                    // PCM: RawDataFormat stays whatever the bit-depth arm above set
                    // (Native, or Unsigned8 for 8-bit).
                } else if format_tag == 3 && byte_depth == 4 {
                    self.raw_data_format = RawDataFormat::Float;
                } else {
                    return Err(ParseError::Unsupported(Reason::UnsupportedFormatTag));
                }

                if channels != 1 && channels != 2 {
                    return Err(ParseError::Unsupported(Reason::UnsupportedChannelCount));
                }

                if !(5000..=96000).contains(&sample_rate) {
                    return Err(ParseError::Unsupported(Reason::SampleRateOutOfRange));
                }

                self.byte_depth = Some(byte_depth);
                self.num_channels = Some(channels as u8);
                self.sample_rate = sample_rate;
            }
            PayloadKind::Smpl => {
                // smpl header: manufacturer, product, samplePeriod,
                // midiUnityNote, midiPitchFraction, smpteFormat, smpteOffset,
                // numSampleLoops, samplerData — 9 little-endian u32 fields (36
                // bytes), then the loop records. Each field read is
                // independently bounds-checked; a short/truncated chunk just
                // leaves midi_note/loop points unset rather than erroring.
                // Mirrors audio_file_format.cpp:214-247.
                if let (Some(midi_unity_note), Some(pitch_fraction)) =
                    (read_u32_le(win, 12), read_u32_le(win, 16))
                {
                    if (midi_unity_note != 0 || pitch_fraction != 0) && midi_unity_note < 128 {
                        // audio_file_format.cpp:227-228 — fractional root
                        // note; 2^32 is exactly representable in f32.
                        self.midi_note =
                            Some(pitch_fraction as f32 / 4_294_967_296.0 + midi_unity_note as f32);
                    }
                }

                // Only a single declared loop is ever honoured: the C++ gates
                // the loop read on `numLoops == 1` rather than looping
                // `numLoops` times (audio_file_format.cpp:231-245), so a
                // hostile declared count (e.g. 0xFFFF_FFFF) never drives any
                // iteration — bounded or not — and never indexes past what a
                // single loop record needs.
                if read_u32_le(win, 28) == Some(1) {
                    // loop record: cuePointId, type, start, end, fraction,
                    // playCount — 6 u32 fields starting right after the
                    // 36-byte header above.
                    if let (Some(loop_start), Some(loop_end)) =
                        (read_u32_le(win, 44), read_u32_le(win, 48))
                    {
                        self.file_loop_start_samples = loop_start;
                        self.file_loop_end_samples = loop_end;
                    }
                }
            }
            PayloadKind::Inst => {
                // inst chunk: MIDIUnshiftedNote:u8, fineTune:i8 (signed,
                // 1/100th semitone), lowNote, highNote, lowVelocity,
                // highVelocity, gain — only the first two bytes are used.
                // Applied in file order like `smpl` above: whichever of
                // `smpl`/`inst` is walked later simply overwrites the note set
                // by the other, with no other precedence rule — that's exactly
                // what the C++ does too, since both arms write `f.midiNote`
                // unconditionally when their own guard passes. Mirrors
                // audio_file_format.cpp:251-266.
                if let (Some(&midi_note_byte), Some(&fine_tune_byte)) = (win.first(), win.get(1)) {
                    if midi_note_byte < 128 {
                        let fine_tune = fine_tune_byte as i8;
                        self.midi_note = Some(midi_note_byte as f32 - (fine_tune as f32) * 0.01);
                    }
                }
            }
            PayloadKind::Clm => {
                // Serum wavetable-hint chunk: "<!>" followed by an ASCII
                // decimal cycle-size, e.g. "<!>2048". The C++ oracle reads
                // exactly 7 raw bytes at the chunk payload *regardless of the
                // chunk's own declared length* (audio_file_format.cpp:270-271) —
                // it never bounds the read to `thisChunk.length`. That's a real
                // over-read quirk, not a typo: this field pair is Serum/Deluge
                // convention, not a documented format, so audio_file_format.cpp
                // is normative and we mirror it exactly, quirks included. It is
                // also why `window_len` is a constant per chunk type rather than
                // the declared length.
                if let Some(raw) = win.get(0..7) {
                    // The C++ loads the first 4 bytes as a little-endian u32 and
                    // masks it with 0x00FFFFFF before comparing to
                    // charsToIntegerConstant('<','!','>',0)
                    // (audio_file_format.cpp:276-277). Masking off the top byte
                    // means the comparison only constrains bytes 0-2 to be
                    // "<!>" — byte index 3 is left completely unconstrained and,
                    // next, gets read as the *first* digit character below.
                    if raw[0] == b'<' && raw[1] == b'!' && raw[2] == b'>' {
                        self.wavetable_explicitly_specifies_self = true;
                        // memToUIntOrError parses raw[3..7] — up to 4 ASCII
                        // decimal digits immediately following the tag, with no
                        // separator (audio_file_format.cpp:281,
                        // util/audio_format_helpers.cpp:22-34). Any non-digit
                        // anywhere in the run aborts the whole parse (mirroring
                        // memToUIntOrError's immediate `return -1`), which the
                        // C++ then only applies when >= 1
                        // (audio_file_format.cpp:282-283). This window is fixed
                        // at 4 bytes, so a u32 can't actually overflow here, but
                        // accumulate with checked arithmetic anyway rather than
                        // trust the window size never grows out from under this
                        // comment.
                        let mut number: u32 = 0;
                        let mut is_number = true;
                        for &digit_byte in &raw[3..7] {
                            if !digit_byte.is_ascii_digit() {
                                is_number = false;
                                break;
                            }
                            let digit = u32::from(digit_byte - b'0');
                            number = match number.checked_mul(10).and_then(|n| n.checked_add(digit))
                            {
                                Some(n) => n,
                                None => {
                                    is_number = false;
                                    break;
                                }
                            };
                        }
                        if is_number && number >= 1 {
                            self.wavetable_cycle_size = number;
                        }
                    }
                }
            }
            PayloadKind::Comm => {
                // COMM layout (big-endian): numChannels:u16,
                // numSampleFrames:u32 (unused here), sampleSize:u16,
                // sampleRate:extended80 (10 bytes) — exactly 18 bytes.
                let channels = read_u16_be(win, 0).expect("required window is 18 bytes");
                let bits = read_u16_be(win, 6).expect("required window is 18 bytes");
                let extended: [u8; 10] = win[8..18]
                    .try_into()
                    .expect("required window is 18 bytes: exactly 10 bytes wide here");

                if channels != 1 && channels != 2 {
                    return Err(ParseError::Unsupported(Reason::UnsupportedChannelCount));
                }

                let byte_depth: u8 = match bits {
                    8 | 16 | 24 | 32 => (bits / 8) as u8,
                    _ => return Err(ParseError::Unsupported(Reason::UnsupportedBitDepth)),
                };

                // AIFF PCM is big-endian; anything wider than 8 bits needs a
                // byte-swap on this (little-endian) target, hence
                // EndiannessWrong*. 8-bit AIFF is signed already (unlike WAV's
                // unsigned-8 convention), so it needs neither a swap nor
                // `Unsigned8` — it stays `Native`. Mirrors
                // audio_file_format.cpp:339-343.
                self.raw_data_format = match byte_depth {
                    2 => RawDataFormat::EndiannessWrong16,
                    3 => RawDataFormat::EndiannessWrong24,
                    4 => RawDataFormat::EndiannessWrong32,
                    _ => RawDataFormat::Native,
                };

                let sample_rate = decode_aiff_extended_rate(&extended);
                if !(5000..=96000).contains(&sample_rate) {
                    return Err(ParseError::Unsupported(Reason::SampleRateOutOfRange));
                }

                self.byte_depth = Some(byte_depth);
                self.num_channels = Some(channels as u8);
                self.sample_rate = sample_rate;
            }
            PayloadKind::Ssnd => {
                // SSND payload is offset:u32 + blockSize:u32 (both big-endian)
                // followed by the audio bytes; the audio start must skip past
                // both plus the declared `offset`, and the audio length excludes
                // them from the chunk's declared size. Mirrors
                // audio_file_format.cpp:297-311.
                const SSND_PREFIX_LEN: u32 = 8;
                let offset = read_u32_be(win, 0).expect("required window is 8 bytes");

                // The C++ computes this as an unsigned subtraction that silently
                // wraps to a huge value when a hostile `offset` exceeds the
                // declared chunk size; that's not part of the AIFF spec, just
                // unhandled-malformed-input behaviour in the oracle, so we
                // deliberately saturate to 0 instead of mirroring the wraparound.
                self.audio_data_length_bytes = chunk
                    .raw_size
                    .saturating_sub(offset)
                    .saturating_sub(SSND_PREFIX_LEN);

                self.audio_data_start_pos_bytes = chunk
                    .data_start
                    .checked_add(SSND_PREFIX_LEN)
                    .and_then(|p| p.checked_add(offset))
                    .unwrap_or(u32::MAX);
            }
            PayloadKind::AiffInst => {
                // INST layout (big-endian): baseNote:u8, detune:i8 (1/100th
                // semitone), lowNote, highNote, lowVelocity, highVelocity,
                // gain:i16 — 8 bytes — then the sustain loop: playMode:u16,
                // beginID:u16, endID:u16 (6 bytes). The release loop that
                // follows is never read. Mirrors audio_file_format.cpp:402-431.
                if let (Some(&midi_note_byte), Some(&fine_tune_byte)) = (win.first(), win.get(1)) {
                    let fine_tune = fine_tune_byte as i8;
                    if (midi_note_byte != 0 || fine_tune != 0) && midi_note_byte < 128 {
                        self.midi_note = Some(midi_note_byte as f32 - (fine_tune as f32) * 0.01);
                    }
                }

                if let (Some(begin_id), Some(end_id)) = (read_u16_be(win, 10), read_u16_be(win, 12))
                {
                    self.sustain_loop_begin_marker_id = begin_id as i16;
                    self.sustain_loop_end_marker_id = end_id as i16;
                }
            }
        }

        self.advance_past(chunk)
    }

    fn step_mark_count(
        &mut self,
        win: &[u8],
        chunk: Chunk,
        chunk_end: u32,
    ) -> Result<(), ParseError> {
        // MARK layout (big-endian): numMarkers:u16, then that many marker
        // records back to back. The count field is bounded by the chunk's own
        // declared end the same way every per-marker read below is.
        let declared = chunk
            .data_start
            .checked_add(2)
            .filter(|&e| e <= chunk_end)
            .and_then(|_| read_u16_be(win, 0))
            .map_or(0, |count| (count as usize).min(MAX_NUM_MARKERS));
        if declared == 0 {
            return self.advance_past(chunk);
        }
        self.phase = Phase::MarkEntry {
            chunk,
            chunk_end,
            cursor: chunk.data_start + 2, // bounds-checked against chunk_end above
            index: 0,
            declared,
        };
        Ok(())
    }

    fn step_mark_entry(
        &mut self,
        win: &[u8],
        chunk: Chunk,
        chunk_end: u32,
        cursor: u32,
        index: usize,
        declared: usize,
    ) -> Result<(), ParseError> {
        // One marker record: id:u16 (bit-reinterpreted as the spec's signed
        // 16-bit ID, matching the C++'s own uint16_t-into-int16_t assignment),
        // position:u32, then a Pascal string name (u8 length prefix + that many
        // bytes, padded to an even total). Mirrors audio_file_format.cpp:357-400.
        let id = match cursor
            .checked_add(2)
            .filter(|&e| e <= chunk_end)
            .and_then(|_| read_u16_be(win, 0))
        {
            Some(v) => v as i16,
            None => return self.advance_past(chunk),
        };
        let cursor = cursor + 2;

        let position = match cursor
            .checked_add(4)
            .filter(|&e| e <= chunk_end)
            .and_then(|_| read_u32_be(win, 2))
        {
            Some(v) => v,
            None => return self.advance_past(chunk),
        };
        let cursor = cursor + 4;

        // The id/position pair is fully read at this point, so record it even if
        // the trailing name string (read next, only to find where the *next*
        // marker starts) turns out to be truncated.
        self.marker_ids[index] = id;
        self.marker_positions[index] = position;
        self.num_markers = index + 1;

        let str_len = match win.get(6) {
            Some(&b) if cursor < chunk_end => b,
            _ => return self.advance_past(chunk),
        };
        let cursor = cursor + 1;

        // The pstring-skip amount below is a DELIBERATE CORRECTION, not a
        // mirror, of audio_file_format.cpp:394-395's `(stringLength + 1) & ~1`.
        // That rounds only the *name* length up to even and skips that many
        // bytes after the already-consumed 1-byte length prefix — but the AIFF
        // pstring convention (and what real encoders, including this crate's own
        // corpus generator, actually write) pads the *whole* record — length
        // byte plus name — to an even total. Those two rules disagree whenever
        // the name length is odd or zero, which is exactly the common case (e.g.
        // 3-letter names like "beg"/"end"). Verified against
        // `~/deluge-aiff-corpus`: the C++ formula misreads every second marker
        // in every multi-marker real file; the corrected formula below reads
        // sane positions matching the file. See task-7-report.md for the trace.
        let skip = if str_len % 2 == 0 {
            u32::from(str_len) + 1
        } else {
            u32::from(str_len)
        };
        let cursor = match cursor.checked_add(skip).filter(|&c| c <= chunk_end) {
            Some(c) => c,
            None => return self.advance_past(chunk),
        };

        if index + 1 >= declared {
            return self.advance_past(chunk);
        }
        self.phase = Phase::MarkEntry {
            chunk,
            chunk_end,
            cursor,
            index: index + 1,
            declared,
        };
        Ok(())
    }

    /// The walk has reached the end of the file at `eof`. Resolve what the
    /// chunks said into a header, or report why they don't add up.
    fn finish(&mut self, eof: u32) -> Result<(), ParseError> {
        // If we never walked a real chunk, we can't tell a genuinely empty
        // container from a file truncated before its first chunk header — fall
        // back to the top-level size field. Once at least one chunk has been
        // walked, running out on a chunk boundary is unambiguously the end of
        // the container.
        if !self.saw_a_chunk && 8u32.checked_add(self.declared_after_top) != Some(eof) {
            return Err(ParseError::NeedAtLeast(
                eof.saturating_add(CHUNK_HEADER_LEN),
            ));
        }

        if !self.found_data_chunk {
            return Err(ParseError::Malformed(Reason::NoDataChunk));
        }
        let num_channels = match self.num_channels {
            Some(c) => c,
            None => return Err(ParseError::Malformed(Reason::NoFmtChunk)),
        };
        let byte_depth = match self.byte_depth {
            Some(d) => d,
            None => return Err(ParseError::Malformed(Reason::NoFmtChunk)),
        };

        // AIFF marker-resolved loop finalisation: `INST`'s sustain-loop marker
        // IDs only mean anything once the whole header has been scanned, since
        // `MARK` (which maps IDs to sample positions) can appear before or after
        // `INST`. `sustain_loop_end_marker_id != -1` gates on "an INST chunk was
        // seen" the same way the C++ does; an ID with no matching `MARK` entry —
        // or no `MARK` chunk at all — simply never writes either field, leaving
        // both at their 0 ("no loop") default. Mirrors
        // audio_file_format.cpp:444-454.
        if self.container == Container::Aiff && self.sustain_loop_end_marker_id != -1 {
            for i in 0..self.num_markers {
                if self.marker_ids[i] == self.sustain_loop_begin_marker_id {
                    self.file_loop_start_samples = self.marker_positions[i];
                }
                if self.marker_ids[i] == self.sustain_loop_end_marker_id {
                    self.file_loop_end_samples = self.marker_positions[i];
                }
            }
        }

        self.phase = Phase::Done(AudioHeader {
            container: self.container,
            num_channels,
            byte_depth,
            raw_data_format: self.raw_data_format,
            sample_rate: self.sample_rate,
            audio_data_start_pos_bytes: self.audio_data_start_pos_bytes,
            audio_data_length_bytes: self.audio_data_length_bytes,
            file_loop_start_samples: self.file_loop_start_samples,
            file_loop_end_samples: self.file_loop_end_samples,
            midi_note: self.midi_note,
            wavetable: WavetableHints {
                cycle_size: self.wavetable_cycle_size,
                explicitly_specifies_self: self.wavetable_explicitly_specifies_self,
            },
        });
        Ok(())
    }
}

/// The part of `window` that lives at absolute offset `start` onwards, or
/// nothing at all if the window doesn't reach that far.
fn window_at(window: &[u8], window_start: u32, start: u32) -> &[u8] {
    match start.checked_sub(window_start) {
        Some(offset) => window.get(offset as usize..).unwrap_or(&[]),
        // The window begins after the bytes we need, so it says nothing about
        // what is at `start`.
        None => &[],
    }
}

/// Whether `window` fully contains the `len` bytes at absolute offset `start`.
fn covers(window: &[u8], window_start: u32, start: u32, len: u32) -> bool {
    match start.checked_sub(window_start) {
        Some(offset) => window.len() as u64 >= u64::from(offset) + u64::from(len),
        None => false,
    }
}

/// Decode the header of a file supplied whole, in one slice.
///
/// A convenience wrapper over [`HeaderParser`] for callers that already hold
/// the entire file — tests, tools, the corpus differential. `bytes` is treated
/// as a *prefix*, not necessarily the whole file: when the walk needs bytes
/// beyond it, this returns [`ParseError::NeedAtLeast`] with the offset the
/// caller must reach, exactly as it always has.
///
/// Streaming callers — anything reading off a card — should drive
/// [`HeaderParser`] directly instead: it never asks for more than
/// [`MAX_WINDOW_BYTES`] at a time, where this can ask for the whole file.
pub fn parse(bytes: &[u8]) -> Result<AudioHeader, ParseError> {
    let mut parser = HeaderParser::new();
    loop {
        let Some((start, len)) = parser.pending() else {
            // Terminal already: one inert feed hands back the stored outcome.
            return match parser.feed(&[], 0)? {
                FeedResult::Done(header) => Ok(header),
                FeedResult::Need { start, .. } => Err(ParseError::NeedAtLeast(start)),
            };
        };

        // A request that starts past the prefix is the one thing this shape
        // can't answer: we don't know whether the file really ends here or the
        // caller simply stopped reading, so we ask for more rather than
        // silently truncating the walk. (A `HeaderParser` caller has no such
        // ambiguity — a short window means EOF, full stop.)
        if start as usize > bytes.len() {
            return Err(ParseError::NeedAtLeast(start));
        }
        let from = start as usize;
        let to = from.saturating_add(len as usize).min(bytes.len());
        match parser.feed(&bytes[from..to], start)? {
            FeedResult::Done(header) => return Ok(header),
            FeedResult::Need { .. } => {}
        }
    }
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

    /// 80-bit IEEE 754 extended, big-endian, for the small positive integers AIFF rates use.
    fn extended80(value: u32) -> [u8; 10] {
        let mut out = [0u8; 10];
        if value == 0 {
            return out;
        }
        let exp = 31 - value.leading_zeros();
        let mantissa = (value as u64) << (63 - exp);
        out[0..2].copy_from_slice(&((16383 + exp) as u16).to_be_bytes());
        out[2..10].copy_from_slice(&mantissa.to_be_bytes());
        out
    }

    fn form_comm_ssnd(channels: u16, rate: u32, bits: u16, frames: u32) -> std::vec::Vec<u8> {
        let mut v = std::vec::Vec::new();
        v.extend_from_slice(b"FORM");
        v.extend_from_slice(&0u32.to_be_bytes());
        v.extend_from_slice(b"AIFF");
        v.extend_from_slice(b"COMM");
        v.extend_from_slice(&18u32.to_be_bytes());
        v.extend_from_slice(&channels.to_be_bytes());
        v.extend_from_slice(&frames.to_be_bytes());
        v.extend_from_slice(&bits.to_be_bytes());
        v.extend_from_slice(&extended80(rate));
        let data_len = frames * u32::from(channels) * u32::from(bits / 8);
        v.extend_from_slice(b"SSND");
        v.extend_from_slice(&(data_len + 8).to_be_bytes());
        v.extend_from_slice(&0u32.to_be_bytes()); // offset
        v.extend_from_slice(&0u32.to_be_bytes()); // block size
        v.resize(v.len() + data_len as usize, 0);
        v
    }

    #[test]
    fn aiff_comm_and_ssnd_parse() {
        let bytes = form_comm_ssnd(1, 44100, 16, 4);
        let h = parse(&bytes).expect("should parse");
        assert_eq!(h.container, Container::Aiff);
        assert_eq!(h.num_channels, 1);
        assert_eq!(h.byte_depth, 2);
        assert_eq!(h.sample_rate, 44100);
        assert_eq!(h.raw_data_format, RawDataFormat::EndiannessWrong16);
        assert_eq!(h.audio_data_length_bytes, 8);
    }

    #[test]
    fn aiff_comm_extreme_exponent_does_not_panic() {
        // A COMM chunk whose 80-bit extended sample rate has an all-ones
        // exponent (the IEEE 754 extended infinity/NaN encoding) and a zero
        // mantissa. The C++ oracle's ConvertFromIeeeExtended returns
        // HUGE_VAL for this and then truncates it to uint32_t — UB in C++.
        // Our decode must just return, not panic, on the shift-by-a-hostile-
        // exponent this implies.
        let mut v = std::vec::Vec::new();
        v.extend_from_slice(b"FORM");
        v.extend_from_slice(&0u32.to_be_bytes());
        v.extend_from_slice(b"AIFF");
        v.extend_from_slice(b"COMM");
        v.extend_from_slice(&18u32.to_be_bytes());
        v.extend_from_slice(&1u16.to_be_bytes()); // channels
        v.extend_from_slice(&0u32.to_be_bytes()); // numSampleFrames (unused)
        v.extend_from_slice(&16u16.to_be_bytes()); // bits
        v.extend_from_slice(&[0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0]); // extended: exponent all-ones, mantissa 0
        let _ = parse(&v); // Must return, not panic.
    }

    /// form_comm_ssnd + MARK (two markers) + INST naming them as the sustain loop.
    fn form_with_markers(
        begin_id: i16,
        begin_pos: u32,
        end_id: i16,
        end_pos: u32,
    ) -> std::vec::Vec<u8> {
        let mut v = form_comm_ssnd(1, 44100, 16, 8);
        // MARK: numMarkers, then per marker: id (i16), position (u32), pstring name.
        let mut mark = std::vec::Vec::new();
        mark.extend_from_slice(&2u16.to_be_bytes());
        for (id, pos) in [(begin_id, begin_pos), (end_id, end_pos)] {
            mark.extend_from_slice(&id.to_be_bytes());
            mark.extend_from_slice(&pos.to_be_bytes());
            mark.push(0); // zero-length pstring
            mark.push(0); // pad to even
        }
        v.extend_from_slice(b"MARK");
        v.extend_from_slice(&(mark.len() as u32).to_be_bytes());
        v.extend_from_slice(&mark);

        // INST: 20 bytes; sustain loop is playMode + beginID + endID at offset 8.
        let mut inst = std::vec::from_elem(0u8, 20);
        inst[8..10].copy_from_slice(&1u16.to_be_bytes()); // playMode = forward loop
        inst[10..12].copy_from_slice(&begin_id.to_be_bytes());
        inst[12..14].copy_from_slice(&end_id.to_be_bytes());
        v.extend_from_slice(b"INST");
        v.extend_from_slice(&20u32.to_be_bytes());
        v.extend_from_slice(&inst);
        v
    }

    #[test]
    fn aiff_markers_resolve_to_loop_points() {
        let h = parse(&form_with_markers(1, 100, 2, 200)).expect("should parse");
        assert_eq!(h.file_loop_start_samples, 100);
        assert_eq!(h.file_loop_end_samples, 200);
    }

    #[test]
    fn aiff_unmatched_marker_ids_leave_loops_zero() {
        // INST names IDs 7/8, MARK only defines 1/2 — nothing to resolve.
        //
        // NOTE: the brief's original version of this test called
        // `form_with_markers(7, 100, 8, 200)`, but that helper uses the same
        // `begin_id`/`end_id` pair for BOTH the MARK entries and the INST
        // sustain-loop fields — so that call actually defines MARK markers
        // 7 and 8 too, which DOES resolve (it's a duplicate of
        // `aiff_markers_resolve_to_loop_points` with different IDs, not an
        // unmatched-ID case at all). Verified against
        // audio_file_format.cpp:444-454: the finalisation loop only ever
        // *writes* fileLoopStart/EndSamples when a markerIDs[m] entry equals
        // the wanted ID — with no match (as constructed below, where MARK
        // and INST genuinely disagree), both stay at their zero-initialised
        // default, confirming the test's intended behaviour is correct; only
        // its construction needed fixing.
        let mut v = form_comm_ssnd(1, 44100, 16, 8);
        let mut mark = std::vec::Vec::new();
        mark.extend_from_slice(&2u16.to_be_bytes());
        for (id, pos) in [(1i16, 100u32), (2i16, 200u32)] {
            mark.extend_from_slice(&id.to_be_bytes());
            mark.extend_from_slice(&pos.to_be_bytes());
            mark.push(0); // zero-length pstring
            mark.push(0); // pad to even
        }
        v.extend_from_slice(b"MARK");
        v.extend_from_slice(&(mark.len() as u32).to_be_bytes());
        v.extend_from_slice(&mark);

        let mut inst = std::vec::from_elem(0u8, 20);
        inst[8..10].copy_from_slice(&1u16.to_be_bytes()); // playMode = forward loop
        inst[10..12].copy_from_slice(&7i16.to_be_bytes()); // beginID: not in MARK
        inst[12..14].copy_from_slice(&8i16.to_be_bytes()); // endID: not in MARK
        v.extend_from_slice(b"INST");
        v.extend_from_slice(&20u32.to_be_bytes());
        v.extend_from_slice(&inst);

        let h = parse(&v).expect("should parse");
        assert_eq!((h.file_loop_start_samples, h.file_loop_end_samples), (0, 0));
    }

    #[test]
    fn aiff_marker_count_past_bound_is_clamped() {
        // kMaxNumMarkers is 8 (audio_file_format.cpp:31). Declare 9
        // well-formed markers (ids 1..=9, positions 100..=900) — one more
        // than the table holds — and have INST name marker 8 (the last one
        // *inside* the bound, table index 7) as the loop start and marker 9
        // (the first one *past* the bound) as the loop end.
        //
        // This exercises the boundary from both directions: marker 8 must
        // still resolve (the clamp must not be off-by-one in the stricter
        // direction and cut the table short), while marker 9 must NOT
        // resolve (it must never be written into the table at all — with a
        // fixed `[_; 8]` array and no clamp, writing a 9th entry would be an
        // out-of-bounds array write, i.e. this test would panic rather than
        // merely assert wrong, if the clamp were missing or off-by-one).
        let mut v = form_comm_ssnd(1, 44100, 16, 8);
        let mut mark = std::vec::Vec::new();
        mark.extend_from_slice(&9u16.to_be_bytes()); // numMarkers: one past kMaxNumMarkers
        for id in 1i16..=9 {
            mark.extend_from_slice(&id.to_be_bytes());
            mark.extend_from_slice(&(id as u32 * 100).to_be_bytes());
            mark.push(0); // zero-length pstring
            mark.push(0); // pad to even
        }
        v.extend_from_slice(b"MARK");
        v.extend_from_slice(&(mark.len() as u32).to_be_bytes());
        v.extend_from_slice(&mark);

        let mut inst = std::vec::from_elem(0u8, 20);
        inst[8..10].copy_from_slice(&1u16.to_be_bytes()); // playMode = forward loop
        inst[10..12].copy_from_slice(&8i16.to_be_bytes()); // beginID: last marker inside the bound
        inst[12..14].copy_from_slice(&9i16.to_be_bytes()); // endID: first marker past the bound
        v.extend_from_slice(b"INST");
        v.extend_from_slice(&20u32.to_be_bytes());
        v.extend_from_slice(&inst);

        let h = parse(&v).expect("should parse");
        assert_eq!(
            h.file_loop_start_samples, 800,
            "marker 8 (in bound) should resolve"
        );
        assert_eq!(
            h.file_loop_end_samples, 0,
            "marker 9 (past the bound) must not resolve"
        );
    }

    #[test]
    fn aiff_hostile_mark_huge_count_short_payload_does_not_panic_or_hang() {
        // MARK declares 0xFFFF markers but the chunk payload holds none.
        // numMarkers must be clamped (to kMaxNumMarkers = 8) before any loop
        // runs, and each per-marker field read must be bounds-checked, so
        // this must return promptly rather than hang or panic.
        let mut v = form_comm_ssnd(1, 44100, 16, 8);
        v.extend_from_slice(b"MARK");
        v.extend_from_slice(&2u32.to_be_bytes()); // declared chunk payload: just the count
        v.extend_from_slice(&0xFFFFu16.to_be_bytes()); // hostile numMarkers
        let _ = parse(&v); // Must return, not panic and not hang.
    }

    #[test]
    fn aiff_hostile_mark_pstring_overruns_chunk_does_not_panic() {
        // One marker whose pstring declares a length that walks the cursor
        // past the MARK chunk's own declared end. The marker's id/position
        // are still fully present and readable; only the trailing name
        // string is truncated.
        let mut v = form_comm_ssnd(1, 44100, 16, 8);
        let mut mark = std::vec::Vec::new();
        mark.extend_from_slice(&1u16.to_be_bytes()); // numMarkers
        mark.extend_from_slice(&1i16.to_be_bytes()); // id
        mark.extend_from_slice(&42u32.to_be_bytes()); // position
        mark.push(0xFF); // pstring length claims 255 bytes that aren't there
        v.extend_from_slice(b"MARK");
        v.extend_from_slice(&(mark.len() as u32).to_be_bytes());
        v.extend_from_slice(&mark);
        let _ = parse(&v); // Must return, not panic.
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
