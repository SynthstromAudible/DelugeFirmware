//! Audio-file header decoding: WAV/RIFF and AIFF/FORM.
//!
//! No I/O, no allocation, no async, and no panic on any input. The caller owns all reading;
//! [`HeaderParser`] owns the walk and asks for one small window at a time, so an async task can
//! drive the parse and the decoder is easy to fuzz. The C ABI over it is [`abi`]
//! (`libdeluge/audio_format.h`).
//!
//! # Windows, not prefixes
//!
//! The parser asks for at most [`MAX_WINDOW_BYTES`] at a time, at an offset of its choosing, and
//! steps over the audio payload by arithmetic without reading it. A growing-prefix protocol cannot
//! bound memory: a chunk's payload sits between the caller and the next chunk header, so the
//! prefix a real file needs is usually the whole file.
//!
//! # File size and the two error types
//!
//! [`HeaderParser`] is told the file's size, so no request can name a byte past the end and the
//! walk is finite by construction. It fails with [`HeaderError`], which has no "supply more and
//! retry" variant. Only [`parse`], whose slice is a prefix the caller can extend, returns
//! [`ParseError::NeedAtLeast`]. [`parse`] is for callers that hold a whole file (tests, tools).
//!
//! Whether a header may be used as a wavetable is a separate policy decision, in [`wavetable`].

#![no_std]
// `deny`, not `forbid`: the C ABI in [`abi`] needs `unsafe` to turn raw pointers into references,
// and `forbid` cannot be lifted for one module. The decoder itself is unsafe-free.
#![deny(unsafe_code)]

#[cfg(test)]
extern crate std;

pub mod abi;
pub mod wavetable;

/// The file's container. Decides endianness after the 12-byte top header: RIFF is little-endian,
/// AIFF big-endian (chunk sizes included).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Container {
    Wav = 0,
    Aiff = 1,
}

/// How the audio bytes are encoded relative to what the engine wants.
///
/// Discriminants match the C++ `RawDataFormat` enum. `EndiannessWrong*` means the samples need
/// byte-swapping: AIFF PCM wider than 8 bits is big-endian, so it always lands on one of them.
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

/// Why a file could not be decoded.
///
/// Carried by both `Malformed` (the file contradicts itself) and `Unsupported` (valid, but says
/// something we cannot use) variants of [`ParseError`] and [`HeaderError`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Reason {
    NoFmtChunk,
    NoDataChunk,
    UnsupportedBitDepth,
    UnsupportedFormatTag,
    UnsupportedChannelCount,
    SampleRateOutOfRange,
}

/// Why [`parse`] could not produce a header from its (possibly partial) buffer.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ParseError {
    /// Not a failure: the prefix ran out. Supply at least this many bytes from the start of the
    /// file and call again. Chunk order is not fixed, so a header can need an arbitrarily long
    /// prefix; streaming callers should drive [`HeaderParser`] instead.
    NeedAtLeast(u32),
    /// No container we recognise: not an audio file, as opposed to a broken one.
    NotAudio,
    Malformed(Reason),
    Unsupported(Reason),
}

/// Why a [`HeaderParser`] walk stopped.
///
/// Unlike [`ParseError`] there is no "supply more and retry": the parser knows the file's size
/// and never asks past it. An adapter that retried on [`HeaderError::Truncated`] as if it were
/// [`ParseError::NeedAtLeast`] would loop forever.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HeaderError {
    /// No container we recognise: not an audio file, as opposed to a broken one.
    NotAudio,
    Malformed(Reason),
    Unsupported(Reason),
    /// The file ends before the header does, or a read came back short. `needs_at_least` is how
    /// long the file would have to be for the field being read to exist — a fact about the file,
    /// not an instruction to fetch more.
    Truncated {
        needs_at_least: u32,
    },
}

impl From<HeaderError> for ParseError {
    /// Maps `Truncated` to `NeedAtLeast`: [`parse`]'s slice is a prefix the caller can extend,
    /// so there running out of bytes does mean "supply more".
    fn from(error: HeaderError) -> Self {
        match error {
            HeaderError::NotAudio => ParseError::NotAudio,
            HeaderError::Malformed(reason) => ParseError::Malformed(reason),
            HeaderError::Unsupported(reason) => ParseError::Unsupported(reason),
            HeaderError::Truncated { needs_at_least } => ParseError::NeedAtLeast(needs_at_least),
        }
    }
}

/// What a walk is for: which chunks it reads, and which of the file's claims it insists on.
///
/// A sample load and a wavetable load accept different files; collapsing the two changes which
/// files load.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum WalkGoal {
    /// Everything the header says. What a Sample load wants.
    WholeHeader = 0,
    /// Only what locates and shapes the audio. What a WaveTable load wants:
    ///
    /// * per-note metadata (`smpl`, `inst`, AIFF `INST`) is stepped over unread, so root note and
    ///   loop points stay unset, and payload windows are bounded so the walk never reads past the
    ///   audio start — the caller's reader goes on to read the audio from where the walk left it;
    /// * the sample rate is reported but need not be playable (a 192 kHz single-cycle file is a
    ///   valid wavetable; wavetable rendering ignores the rate).
    AudioOnly = 1,
}

/// Hints a file gives about being a wavetable. Facts only; whether they suffice is
/// [`wavetable::accept_as_wavetable`]'s decision.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WavetableHints {
    /// Samples per cycle, from a `clm ` tag; 2048 (the Serum default) when the file says nothing.
    pub cycle_size: u32,
    /// A `clm ` tag was present: the file declares itself a wavetable.
    pub explicitly_specifies_self: bool,
}

/// Everything a header tells us, as a plain value.
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
    /// Loop start in sample frames; 0 means no loop (the WAV/AIFF convention). Start and end are
    /// set independently.
    pub file_loop_start_samples: u32,
    pub file_loop_end_samples: u32,
    /// Root note as a fractional MIDI note number; `None` if the file specifies none.
    pub midi_note: Option<f32>,
    pub wavetable: WavetableHints,
}

/// Reads a little-endian `u32` at `pos`, or `None` if `bytes` is too short.
fn read_u32_le(bytes: &[u8], pos: usize) -> Option<u32> {
    let end = pos.checked_add(4)?;
    let word = bytes.get(pos..end)?;
    Some(u32::from_le_bytes(word.try_into().ok()?))
}

/// Reads a big-endian `u32` at `pos`, or `None` if `bytes` is too short.
fn read_u32_be(bytes: &[u8], pos: usize) -> Option<u32> {
    let end = pos.checked_add(4)?;
    let word = bytes.get(pos..end)?;
    Some(u32::from_be_bytes(word.try_into().ok()?))
}

/// Reads a big-endian `u16` at `pos`, or `None` if `bytes` is too short.
fn read_u16_be(bytes: &[u8], pos: usize) -> Option<u16> {
    let end = pos.checked_add(2)?;
    let half = bytes.get(pos..end)?;
    Some(u16::from_be_bytes(half.try_into().ok()?))
}

/// Decodes a big-endian 80-bit IEEE 754 extended float (the AIFF `COMM` sample rate) to a `u32`.
///
/// Integer shifts on the 64-bit mantissa, no floating point: real rates are small positive
/// integers, which the format represents exactly. Fractions truncate. Values that cannot be a rate
/// (negative, infinite/NaN, beyond `u32`) saturate to `0` or `u32::MAX`, which the caller's
/// `5000..=96000` range check rejects.
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
        // A negative rate is nonsense; 0 fails the caller's range check.
        return 0;
    }

    // value == mantissa * 2^exp, with the 64-bit mantissa's binary point after its top bit
    // (bias 16383, 63 fraction bits).
    let mantissa = (u64::from(hi) << 32) | u64::from(lo);
    let exp = i32::from(expon) - 16383 - 63;

    if exp >= 0 {
        // A hostile exponent can ask for a shift of 64 or more, which panics in every build
        // profile.
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

/// The largest window [`HeaderParser`] can ask a caller for, in bytes: the caller's peak buffer.
///
/// Every request is the 12-byte container header, a chunk header (8 bytes, plus at most one
/// preceding pad byte), or a fixed-size slice of one chunk's payload — the largest being `smpl`'s
/// 52, where its first loop record's end field ends. `MARK` is walked one 7-byte record at a time,
/// so no size a file declares affects the bound.
///
/// This is a floor, not a serving size. Callers should hand back whatever they already hold (a
/// whole SD cluster is ideal); the parser satisfies every request the window covers before asking
/// again. It matters: a `data` chunk that under-declares its length (an unfinalised recording, a
/// card that filled mid-write) makes the walk read the audio as a stream of junk chunk headers,
/// one request each, so small windows mean many round trips to the card.
pub const MAX_WINDOW_BYTES: u32 = 52;

const TOP_HEADER_LEN: u32 = 12;
const CHUNK_HEADER_LEN: u32 = 8;

/// Capacity of the AIFF `MARK` marker table. The declared count is clamped to this before any
/// marker is read; later markers are ignored.
const MAX_NUM_MARKERS: usize = 8;

/// Length of an AIFF `MARK` chunk's leading marker count (`u16`).
const MARK_COUNT_LEN: u32 = 2;
/// Length of one `MARK` record's fixed head: id `u16`, position `u32`, name length `u8`. The name
/// is skipped, never read.
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

/// Which chunk payload a [`Phase::Payload`] is waiting on.
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
    /// Bytes read from the chunk's payload start.
    ///
    /// A fixed extent per chunk type, not the chunk's declared length: a declared length would put
    /// the file in charge of the caller's buffer size. The extents (and the `clm ` over-read they
    /// imply) match the C++ decoder this crate replaced.
    const fn window_len(self) -> u32 {
        match self {
            PayloadKind::Fmt => 16,
            PayloadKind::Smpl => 52,
            PayloadKind::Inst => 2,
            PayloadKind::Clm => 7,
            PayloadKind::Comm => 18,
            // offset + blockSize. SSND audio starts at `data_start + 8 + offset`, so this never
            // reaches into the audio.
            PayloadKind::Ssnd => 8,
            PayloadKind::AiffInst => 14,
        }
    }

    /// Every kind, for the compile-time bound check below.
    const ALL: [PayloadKind; 7] = [
        PayloadKind::Fmt,
        PayloadKind::Smpl,
        PayloadKind::Inst,
        PayloadKind::Clm,
        PayloadKind::Comm,
        PayloadKind::Ssnd,
        PayloadKind::AiffInst,
    ];
}

// Every window the parser can request fits [`MAX_WINDOW_BYTES`]. Checked at compile time so that
// raising the bound is a deliberate edit to the constant, not a side effect of a new chunk arm.
const _: () = {
    let mut i = 0;
    while i < PayloadKind::ALL.len() {
        assert!(PayloadKind::ALL[i].window_len() <= MAX_WINDOW_BYTES);
        i += 1;
    }
    assert!(TOP_HEADER_LEN <= MAX_WINDOW_BYTES);
    // A chunk header plus the pad byte that can precede it.
    assert!(CHUNK_HEADER_LEN < MAX_WINDOW_BYTES);
    assert!(MARK_COUNT_LEN <= MAX_WINDOW_BYTES);
    assert!(MARK_ENTRY_LEN <= MAX_WINDOW_BYTES);
};

/// One chunk's payload extent, carried across the states that decode it.
#[derive(Clone, Copy, Debug)]
struct Chunk {
    data_start: u32,
    raw_size: u32,
}

/// Where the walk is. Every non-terminal variant maps to one bounded window request (see
/// [`HeaderParser::request`]).
#[derive(Clone, Copy, Debug)]
enum Phase {
    /// Wants the 12-byte RIFF/FORM container header.
    Top,
    /// Wants the chunk header at `at + pad`. `pad` is the previous chunk's trailing alignment byte,
    /// requested with the header so a file that omits it after its last chunk still ends cleanly.
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
    /// Wants one `MARK` record's fixed head at `cursor`: record `index` of `declared`.
    MarkEntry {
        chunk: Chunk,
        chunk_end: u32,
        cursor: u32,
        index: usize,
        declared: usize,
    },
    Done(AudioHeader),
    Failed(HeaderError),
}

/// A resumable WAV/AIFF header decode.
///
/// The caller owns all reading; the parser owns the walk. It asks for one window at a time
/// ([`HeaderParser::pending`]) and is handed the bytes ([`HeaderParser::feed`]) until it reports
/// [`FeedResult::Done`] or fails. It never allocates, blocks or panics, and its whole state is this
/// fixed-size value, so any executor or a plain loop can drive it. The audio payload is skipped by
/// arithmetic, never requested.
///
/// ```
/// # use deluge_audio_format::{HeaderParser, FeedResult};
/// # fn read_exact_at(_f: &[u8], _s: u32, _l: u32) -> &'static [u8] { &[] }
/// # let file_size = 0u32;
/// let mut parser = HeaderParser::new(file_size);
/// while let Some((start, len)) = parser.pending() {
///     // `start + len` is inside the file, so this always reads real bytes.
///     // Handing back more than `len` (a whole cluster, say) is better still.
///     let window = read_exact_at(&[], start, len);
///     match parser.feed(window, start) {
///         Ok(FeedResult::Done(header)) => { let _ = header; break }
///         Ok(FeedResult::Need { .. }) => continue,
///         Err(_e) => break,
///     }
/// }
/// ```
#[derive(Clone, Debug)]
pub struct HeaderParser {
    /// The file's size, which bounds every request and so makes the walk finite. `None` is prefix
    /// mode ([`HeaderParser::for_prefix`]), where running out of bytes means "ask for more".
    file_size: Option<u32>,
    goal: WalkGoal,
    phase: Phase,
    container: Container,
    /// Bytes 4..8 of the container header: how many bytes the file claims follow them. Only used
    /// to tell an empty container from one truncated before its first chunk header.
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
    /// AIFF `MARK` table, resolved against `INST`'s sustain-loop IDs in `finish`. Marker IDs are
    /// signed 16-bit (the wire `u16` reinterpreted).
    marker_ids: [i16; MAX_NUM_MARKERS],
    marker_positions: [u32; MAX_NUM_MARKERS],
    /// Entries of the marker arrays actually read, not the declared count.
    num_markers: usize,
    /// `INST` sustain-loop marker IDs; -1 until an `INST` chunk is read. (The AIFF spec requires
    /// positive IDs, so -1 never names a real marker.)
    sustain_loop_begin_marker_id: i16,
    sustain_loop_end_marker_id: i16,
}

impl HeaderParser {
    /// Creates a [`WalkGoal::WholeHeader`] parser for a `file_size`-byte file.
    ///
    /// `file_size` must be the file's real size: every request is clamped to it, which is what
    /// bounds the walk.
    #[must_use]
    pub const fn new(file_size: u32) -> Self {
        Self::with_size(Some(file_size), WalkGoal::WholeHeader)
    }

    /// Creates a parser for a `file_size`-byte file that reads only what `goal` calls for.
    #[must_use]
    pub const fn new_with_goal(file_size: u32, goal: WalkGoal) -> Self {
        Self::with_size(Some(file_size), goal)
    }

    /// Creates a parser in prefix mode, where running out of bytes means "supply more".
    ///
    /// Private: only [`parse`] needs it, and it is the one mode whose walk is not bounded by a
    /// size.
    const fn for_prefix() -> Self {
        Self::with_size(None, WalkGoal::WholeHeader)
    }

    /// The window a fresh parser wants: the container header.
    const FIRST_REQUEST: (u32, u32) = (0, TOP_HEADER_LEN);

    const fn with_size(file_size: Option<u32>, goal: WalkGoal) -> Self {
        Self {
            file_size,
            goal,
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

    /// Returns the window `feed` is waiting for as `(start, len)`, or `None` once the walk has
    /// finished (successfully or not).
    ///
    /// `start + len` never exceeds the file size, so the bytes always exist. `len` may be 0 (an
    /// empty file); feed an empty window, since the parser only advances inside
    /// [`HeaderParser::feed`].
    #[must_use]
    pub fn pending(&self) -> Option<(u32, u32)> {
        match self.phase {
            Phase::Done(_) | Phase::Failed(_) => None,
            _ => Some(self.request()),
        }
    }

    /// Returns the container the file declares, available after the first
    /// [`HeaderParser::feed`].
    ///
    /// For verdicts that depend on the container alone: a wavetable load rejects AIFF before
    /// reading any chunk. Waiting for the decode would turn a malformed AIFF into a decode error,
    /// which the browser treats differently (it then does not fall back to loading the file as a
    /// sample).
    ///
    /// `None` before the top header has been read, and on a walk that failed without identifying
    /// a container.
    #[must_use]
    pub fn container(&self) -> Option<Container> {
        match self.phase {
            Phase::Top => None,
            // `step_top` fails only with these two errors. `Truncated` can also come from a later
            // step, so it counts only if no chunk was walked — conservative for a file whose
            // first chunk header is truncated, which fails the decode anyway.
            Phase::Failed(HeaderError::NotAudio) => None,
            Phase::Failed(HeaderError::Truncated { .. }) if !self.saw_a_chunk => None,
            _ => Some(self.container),
        }
    }

    /// Returns whether a `data`/`SSND` chunk has been read, so
    /// [`AudioHeader::audio_data_start_pos_bytes`] and [`AudioHeader::audio_data_length_bytes`]
    /// are final.
    ///
    /// A wavetable load pairs this with [`HeaderParser::finish_here`]: what follows the audio chunk
    /// is metadata it does not use, and walking it would move the caller's reader past the audio
    /// it is about to read. For AIFF this stays false until the `SSND` payload (which carries the
    /// audio offset) has been read, not just its header.
    #[must_use]
    pub fn located_audio_data(&self) -> bool {
        self.found_data_chunk
            && !matches!(
                self.phase,
                Phase::Payload {
                    kind: PayloadKind::Ssnd,
                    ..
                }
            )
    }

    /// Ends the walk now and resolves the header from what has been read so far.
    ///
    /// Runs the same finalisation as reaching the end of the file; fields later chunks would have
    /// set keep their defaults (no loop, no root note). Idempotent: on a finished walk it
    /// re-reports that outcome.
    ///
    /// # Errors
    ///
    /// The same as reaching the end of the file: [`Reason::NoDataChunk`] or [`Reason::NoFmtChunk`]
    /// if those chunks have not been read, so call it once
    /// [`HeaderParser::located_audio_data`] is true. A walk that already failed returns its error.
    pub fn finish_here(&mut self) -> Result<AudioHeader, HeaderError> {
        match self.phase {
            Phase::Done(header) => return Ok(header),
            Phase::Failed(error) => return Err(error),
            _ => {}
        }
        // `finish` uses `eof` only to tell an empty container from one truncated before its first
        // chunk header. The file size is the right answer; prefix mode has none.
        self.try_step_finish(self.file_size.unwrap_or(0))?;
        match self.phase {
            Phase::Done(header) => Ok(header),
            // Unreachable: `finish` either sets `Done` or returned an error above.
            _ => Err(HeaderError::Malformed(Reason::NoDataChunk)),
        }
    }

    /// [`HeaderParser::finish`], recording any error on the parser as every other step does.
    fn try_step_finish(&mut self, eof: u32) -> Result<(), HeaderError> {
        if let Err(error) = self.finish(eof) {
            self.phase = Phase::Failed(error);
            return Err(error);
        }
        Ok(())
    }

    /// Hands the parser the bytes it asked for.
    ///
    /// `window_start` is the absolute file offset `window` begins at, normally the pending
    /// request's `start`. Supplying more than was asked for is encouraged (see
    /// [`MAX_WINDOW_BYTES`]): the parser satisfies every further request the window fully covers
    /// before returning [`FeedResult::Need`]. The end of the file needs no answer; the parser
    /// knows where it is and finishes the walk itself.
    ///
    /// # Errors
    ///
    /// Any decode failure. Supplying less than was asked for is a failed read (the request was
    /// clamped to the file, so the bytes exist) and returns [`HeaderError::Truncated`]; do not pad.
    /// Once failed, every later call returns the same error.
    pub fn feed(&mut self, window: &[u8], window_start: u32) -> Result<FeedResult, HeaderError> {
        loop {
            match self.phase {
                Phase::Done(header) => return Ok(FeedResult::Done(header)),
                Phase::Failed(error) => return Err(error),
                _ => {}
            }

            let (start, len) = self.request();
            if self.past_end(start) {
                // End of file: finish without a round trip to the caller.
                self.try_step(&[])?;
                continue;
            }

            let available = window_at(window, window_start, start);
            if self.file_size.is_some() && (available.len() as u32) < len {
                // The request was clamped to the file, so a short answer is a failed read.
                let error = HeaderError::Truncated {
                    needs_at_least: start.saturating_add(len),
                };
                self.phase = Phase::Failed(error);
                return Err(error);
            }
            let usable = &available[..available.len().min(len as usize)];
            self.try_step(usable)?;

            if let Some((next_start, next_len)) = self.pending() {
                if !self.past_end(next_start) && !covers(window, window_start, next_start, next_len)
                {
                    return Ok(FeedResult::Need {
                        start: next_start,
                        len: next_len,
                    });
                }
            }
        }
    }

    /// Returns whether the file is known to hold nothing at `start`.
    fn past_end(&self, start: u32) -> bool {
        self.file_size.is_some_and(|size| start >= size)
    }

    fn try_step(&mut self, win: &[u8]) -> Result<(), HeaderError> {
        if let Err(error) = self.step(win) {
            self.phase = Phase::Failed(error);
            return Err(error);
        }
        Ok(())
    }

    /// The pending window for a non-terminal phase, clamped to the file.
    fn request(&self) -> (u32, u32) {
        let (start, len) = self.unclamped_request();
        match self.file_size {
            // For `start >= size` this yields len 0; `feed` resolves such a request itself, so
            // only an empty file's first request reaches a caller that way.
            Some(size) => (start, len.min(size.saturating_sub(start))),
            None => (start, len),
        }
    }

    fn unclamped_request(&self) -> (u32, u32) {
        match self.phase {
            Phase::Top => (0, TOP_HEADER_LEN),
            Phase::Header { at, pad } => (at, pad + CHUNK_HEADER_LEN),
            Phase::Payload { kind, chunk } => {
                (chunk.data_start, self.payload_window_len(kind, chunk))
            }
            Phase::MarkCount { chunk, .. } => (chunk.data_start, MARK_COUNT_LEN),
            Phase::MarkEntry { cursor, .. } => (cursor, MARK_ENTRY_LEN),
            // Filtered out by `pending`/`feed`; a zero-length request is inert regardless.
            Phase::Done(_) | Phase::Failed(_) => (0, 0),
        }
    }

    /// Returns how much of a chunk's payload to ask for.
    ///
    /// A whole-header walk uses [`PayloadKind::window_len`] as is, over-reads included; nothing
    /// downstream reuses its reader position.
    ///
    /// An audio-only walk hands its reader on to the code that reads the audio (see
    /// [`WalkGoal::AudioOnly`]), so its window is capped at this chunk's payload plus one chunk
    /// header — the earliest the next chunk's payload, and so the audio, can begin. That makes
    /// "the walk never reads past `audio_data_start_pos_bytes`" hold by arithmetic. The cap only
    /// bites on a chunk declaring less than `window_len - 8` bytes, in practice a malformed `fmt `
    /// under 8 bytes, which then fails the decode. The `clm ` over-read (7 bytes from a chunk
    /// declaring 4) is within the cap and preserved.
    fn payload_window_len(&self, kind: PayloadKind, chunk: Chunk) -> u32 {
        let extent = kind.window_len();
        match self.goal {
            WalkGoal::WholeHeader => extent,
            WalkGoal::AudioOnly => extent.min(chunk.raw_size.saturating_add(CHUNK_HEADER_LEN)),
        }
    }

    fn step(&mut self, win: &[u8]) -> Result<(), HeaderError> {
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

    fn step_top(&mut self, win: &[u8]) -> Result<(), HeaderError> {
        let Some(top) = win.first_chunk::<{ TOP_HEADER_LEN as usize }>() else {
            return Err(HeaderError::Truncated {
                needs_at_least: TOP_HEADER_LEN,
            });
        };
        self.container = match (&top[0..4], &top[8..12]) {
            (b"RIFF", b"WAVE") => Container::Wav,
            (b"FORM", b"AIFF") => Container::Aiff,
            _ => return Err(HeaderError::NotAudio),
        };
        // Bytes 4..8 declare how many bytes follow them: the 4-byte form type plus every chunk.
        // `finish` uses it to tell an empty container (end == 8 + declared) from one truncated
        // before its first chunk header.
        let declared = [top[4], top[5], top[6], top[7]];
        self.declared_after_top = match self.container {
            Container::Wav => u32::from_le_bytes(declared),
            Container::Aiff => u32::from_be_bytes(declared),
        };
        self.phase = Phase::Header {
            at: TOP_HEADER_LEN,
            pad: 0,
        };
        Ok(())
    }

    fn step_header(&mut self, win: &[u8], at: u32, pad: u32) -> Result<(), HeaderError> {
        // The previous chunk's pad byte is optional: real files routinely omit it when the
        // odd-sized chunk is the last in the file, and such files must load.
        let header = win.get(pad as usize..).unwrap_or(&[]);
        if header.is_empty() {
            // Nothing at this offset: the file ends here, and so does the walk.
            let eof = at.saturating_add(win.len() as u32);
            return self.finish(eof);
        }
        let header_start = at.saturating_add(pad);
        if header.len() < CHUNK_HEADER_LEN as usize {
            // With a known size the request was clamped to the file, so fewer than 8 bytes means
            // the file ends here: trailing slop (DAW padding, an interrupted copy, a card that
            // filled mid-write) ends the walk rather than failing it. In prefix mode running out
            // may just mean the caller stopped reading, so it stays "supply more".
            if self.file_size.is_some() {
                let eof = at.saturating_add(win.len() as u32);
                return self.finish(eof);
            }
            return Err(HeaderError::Truncated {
                needs_at_least: header_start.saturating_add(CHUNK_HEADER_LEN),
            });
        }

        let tag: [u8; 4] = [header[0], header[1], header[2], header[3]];
        // Chunk sizes follow the container's endianness too.
        let raw_size = match self.container {
            Container::Wav => read_u32_le(header, 4),
            Container::Aiff => read_u32_be(header, 4),
        }
        .unwrap_or(0); // Always Some: the length check above guarantees it.
        self.saw_a_chunk = true;

        let data_start = match header_start.checked_add(CHUNK_HEADER_LEN) {
            Some(v) => v,
            // A payload past the end of the `u32` offset space can never exist.
            None => {
                return Err(HeaderError::Truncated {
                    needs_at_least: u32::MAX,
                })
            }
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
                // Keep walking: `smpl`/`inst`/`clm ` may follow the audio. The payload is stepped
                // over, never read.
                self.advance_past(chunk)
            }
            // Per-note metadata. An audio-only walk skips these unread: their windows are a fixed
            // extent, so a `smpl` declaring less than 52 bytes (36 with no loops) would move the
            // caller's reader past the audio chunk header behind it.
            (Container::Wav, b"smpl") if self.reads_note_metadata() => {
                self.wait_for(PayloadKind::Smpl, chunk)
            }
            (Container::Wav, b"inst") if self.reads_note_metadata() => {
                self.wait_for(PayloadKind::Inst, chunk)
            }
            (Container::Aiff, b"INST") if self.reads_note_metadata() => {
                self.wait_for(PayloadKind::AiffInst, chunk)
            }
            (Container::Wav, b"clm ") => self.wait_for(PayloadKind::Clm, chunk),
            (Container::Aiff, b"COMM") => {
                // COMM must declare exactly 18 bytes; any other length is rejected before the
                // payload is requested.
                if raw_size != PayloadKind::Comm.window_len() {
                    return Err(HeaderError::Unsupported(Reason::UnsupportedFormatTag));
                }
                self.wait_for(PayloadKind::Comm, chunk)
            }
            (Container::Aiff, b"SSND") => {
                self.found_data_chunk = true;
                self.wait_for(PayloadKind::Ssnd, chunk)
            }
            (Container::Aiff, b"MARK") => {
                // Marker reads are bounded to this chunk's declared payload, so a count too high
                // for the payload stops at the chunk boundary instead of reading the next chunk.
                let chunk_end = data_start.saturating_add(raw_size);
                self.phase = Phase::MarkCount { chunk, chunk_end };
                Ok(())
            }
            // Everything else, including per-note metadata on an audio-only walk, is skipped
            // unread.
            _ => self.advance_past(chunk),
        }
    }

    /// Returns whether this walk reads per-note metadata (`smpl`, `inst`, AIFF `INST`). See
    /// [`WalkGoal::AudioOnly`].
    const fn reads_note_metadata(&self) -> bool {
        matches!(self.goal, WalkGoal::WholeHeader)
    }

    /// Returns whether the sample rate must be playable (`5000..=96000` Hz). Only a Sample load
    /// plays the file at its own rate; a wavetable ignores it.
    const fn insists_on_a_playable_rate(&self) -> bool {
        matches!(self.goal, WalkGoal::WholeHeader)
    }

    fn wait_for(&mut self, kind: PayloadKind, chunk: Chunk) -> Result<(), HeaderError> {
        self.phase = Phase::Payload { kind, chunk };
        Ok(())
    }

    /// Steps over the rest of `chunk`, payload unread, and lines up the next chunk header.
    /// RIFF/IFF chunks are word-aligned, so an odd payload's pad byte is folded into the next
    /// request.
    fn advance_past(&mut self, chunk: Chunk) -> Result<(), HeaderError> {
        let unpadded_end = match chunk.data_start.checked_add(chunk.raw_size) {
            Some(v) => v,
            // A file-declared size that overflows the `u32` offset space can never be satisfied.
            None => {
                return Err(HeaderError::Truncated {
                    needs_at_least: u32::MAX,
                })
            }
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
    ) -> Result<(), HeaderError> {
        // Arms that require their fields take a fixed-size array, so later reads are
        // bounds-checked by the type. Optional metadata arms read field by field and leave a field
        // at its default when the window is short.
        let short = |needed: u32| HeaderError::Truncated {
            needs_at_least: chunk.data_start.saturating_add(needed),
        };

        match kind {
            PayloadKind::Fmt => {
                // `fmt ` (little-endian): wFormatTag:u16, wChannels:u16, dwSampleRate:u32,
                // dwByteRate:u32, wBlockAlign:u16, wBitsPerSample:u16.
                let fields = win
                    .first_chunk::<16>()
                    .ok_or_else(|| short(PayloadKind::Fmt.window_len()))?;

                let format_tag = u16::from_le_bytes([fields[0], fields[1]]);
                let channels = u16::from_le_bytes([fields[2], fields[3]]);
                let sample_rate = u32::from_le_bytes([fields[4], fields[5], fields[6], fields[7]]);
                let bits = u16::from_le_bytes([fields[14], fields[15]]);

                let byte_depth: u8 = match bits {
                    8 => {
                        self.raw_data_format = RawDataFormat::Unsigned8;
                        1
                    }
                    16 | 24 | 32 => (bits / 8) as u8,
                    _ => return Err(HeaderError::Unsupported(Reason::UnsupportedBitDepth)),
                };

                if format_tag == 1 {
                    // PCM: keep the format set above (Native, or Unsigned8 for 8-bit).
                } else if format_tag == 3 && byte_depth == 4 {
                    self.raw_data_format = RawDataFormat::Float;
                } else {
                    return Err(HeaderError::Unsupported(Reason::UnsupportedFormatTag));
                }

                if channels != 1 && channels != 2 {
                    return Err(HeaderError::Unsupported(Reason::UnsupportedChannelCount));
                }

                if self.insists_on_a_playable_rate() && !(5000..=96000).contains(&sample_rate) {
                    return Err(HeaderError::Unsupported(Reason::SampleRateOutOfRange));
                }

                self.byte_depth = Some(byte_depth);
                self.num_channels = Some(channels as u8);
                // Recorded on every walk, so the field never depends on the goal.
                self.sample_rate = sample_rate;
            }
            PayloadKind::Smpl => {
                // `smpl`: manufacturer, product, samplePeriod, midiUnityNote, midiPitchFraction,
                // smpteFormat, smpteOffset, numSampleLoops, samplerData — nine little-endian u32
                // (36 bytes) — then the loop records. A short chunk leaves the note and loop
                // points unset rather than failing.
                if let (Some(midi_unity_note), Some(pitch_fraction)) =
                    (read_u32_le(win, 12), read_u32_le(win, 16))
                {
                    if (midi_unity_note != 0 || pitch_fraction != 0) && midi_unity_note < 128 {
                        // midiPitchFraction is in units of 2^-32 semitone.
                        self.midi_note =
                            Some(pitch_fraction as f32 / 4_294_967_296.0 + midi_unity_note as f32);
                    }
                }

                // Loop points are read only when exactly one loop is declared, so the declared
                // count never drives iteration.
                if read_u32_le(win, 28) == Some(1) {
                    // Loop record at 36: cuePointId, type, start, end, fraction, playCount (u32).
                    if let (Some(loop_start), Some(loop_end)) =
                        (read_u32_le(win, 44), read_u32_le(win, 48))
                    {
                        self.file_loop_start_samples = loop_start;
                        self.file_loop_end_samples = loop_end;
                    }
                }
            }
            PayloadKind::Inst => {
                // `inst`: unshiftedNote:u8, fineTune:i8 (cents), lowNote, highNote, lowVelocity,
                // highVelocity, gain; only the first two are used. Whichever of `smpl`/`inst`
                // comes later in the file sets the note.
                if let (Some(&midi_note_byte), Some(&fine_tune_byte)) = (win.first(), win.get(1)) {
                    if midi_note_byte < 128 {
                        let fine_tune = fine_tune_byte as i8;
                        self.midi_note = Some(midi_note_byte as f32 - (fine_tune as f32) * 0.01);
                    }
                }
            }
            PayloadKind::Clm => {
                // Serum wavetable hint: "<!>" then the cycle size as ASCII decimal, e.g.
                // "<!>2048". Always 7 bytes from the payload start, whatever the chunk declares.
                // This is Serum/Deluge convention with no spec; the C++ decoder's behaviour,
                // over-read included, is the reference.
                if let Some(raw) = win.get(0..7) {
                    if raw[0] == b'<' && raw[1] == b'!' && raw[2] == b'>' {
                        self.wavetable_explicitly_specifies_self = true;
                        // Exactly the four bytes after the tag must all be ASCII digits; anything
                        // else, or a value of 0, keeps the default. Checked arithmetic guards
                        // against a wider window later.
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
                // `COMM` (big-endian): numChannels:u16, numSampleFrames:u32, sampleSize:u16,
                // sampleRate:extended80 — 18 bytes.
                let fields = win
                    .first_chunk::<18>()
                    .ok_or_else(|| short(PayloadKind::Comm.window_len()))?;

                let channels = u16::from_be_bytes([fields[0], fields[1]]);
                let bits = u16::from_be_bytes([fields[6], fields[7]]);
                let extended: [u8; 10] = [
                    fields[8], fields[9], fields[10], fields[11], fields[12], fields[13],
                    fields[14], fields[15], fields[16], fields[17],
                ];

                if channels != 1 && channels != 2 {
                    return Err(HeaderError::Unsupported(Reason::UnsupportedChannelCount));
                }

                let byte_depth: u8 = match bits {
                    8 | 16 | 24 | 32 => (bits / 8) as u8,
                    _ => return Err(HeaderError::Unsupported(Reason::UnsupportedBitDepth)),
                };

                // AIFF PCM is big-endian, so wider than 8 bits needs a byte-swap on this
                // little-endian target. 8-bit AIFF is signed (unlike WAV's unsigned 8-bit), so it
                // stays `Native`.
                self.raw_data_format = match byte_depth {
                    2 => RawDataFormat::EndiannessWrong16,
                    3 => RawDataFormat::EndiannessWrong24,
                    4 => RawDataFormat::EndiannessWrong32,
                    _ => RawDataFormat::Native,
                };

                let sample_rate = decode_aiff_extended_rate(&extended);
                if self.insists_on_a_playable_rate() && !(5000..=96000).contains(&sample_rate) {
                    return Err(HeaderError::Unsupported(Reason::SampleRateOutOfRange));
                }

                self.byte_depth = Some(byte_depth);
                self.num_channels = Some(channels as u8);
                self.sample_rate = sample_rate;
            }
            PayloadKind::Ssnd => {
                // `SSND`: offset:u32, blockSize:u32 (big-endian), then the audio. Audio starts
                // `offset` bytes after those 8, and its length is the chunk size minus both.
                const SSND_PREFIX_LEN: u32 = 8;
                let fields = win
                    .first_chunk::<{ SSND_PREFIX_LEN as usize }>()
                    .ok_or_else(|| short(SSND_PREFIX_LEN))?;
                let offset = u32::from_be_bytes([fields[0], fields[1], fields[2], fields[3]]);

                // Saturates to 0 when `offset` exceeds the chunk size.
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
                // AIFF `INST` (big-endian): baseNote:u8, detune:i8 (cents), lowNote, highNote,
                // lowVelocity, highVelocity, gain:i16 (8 bytes), then the sustain loop:
                // playMode:u16, beginID:u16, endID:u16. The release loop after it is not read.
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
    ) -> Result<(), HeaderError> {
        // `MARK` (big-endian): numMarkers:u16, then that many records back to back. Like every
        // marker read, the count must lie within the chunk's declared end.
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
            // Past the count field; the `checked_add` above proved this cannot overflow.
            cursor: chunk.data_start.saturating_add(MARK_COUNT_LEN),
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
    ) -> Result<(), HeaderError> {
        // One record: id:u16 (reinterpreted as the spec's signed ID), position:u32, then a pstring
        // name (u8 length + that many bytes, padded so the whole pstring is even).
        let id = match cursor
            .checked_add(2)
            .filter(|&e| e <= chunk_end)
            .and_then(|_| read_u16_be(win, 0))
        {
            Some(v) => v as i16,
            None => return self.advance_past(chunk),
        };
        let cursor = cursor.saturating_add(2);

        let position = match cursor
            .checked_add(4)
            .filter(|&e| e <= chunk_end)
            .and_then(|_| read_u32_be(win, 2))
        {
            Some(v) => v,
            None => return self.advance_past(chunk),
        };
        let cursor = cursor.saturating_add(4);

        // Record the marker even if its name (read only to find the next record) is truncated.
        self.marker_ids[index] = id;
        self.marker_positions[index] = position;
        self.num_markers = index + 1;

        let str_len = match win.get(6) {
            Some(&b) if cursor < chunk_end => b,
            _ => return self.advance_past(chunk),
        };
        let cursor = cursor.saturating_add(1);

        // Skip the rest of the pstring so the whole thing (length byte + name) has even length:
        // name + 1 bytes for an even name length, name bytes for an odd one.
        //
        // This deliberately differs from the original C++ decoder, which skipped
        // `(stringLength + 1) & ~1` — rounding only the name — and so misread every marker after
        // the first by one byte, leaving AIFF sustain loops unresolved. It is the one intended
        // behaviour difference from that decoder, and only AIFF files are affected.
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

    /// Resolves what the chunks said into a header once the walk ends at `eof`, or reports why
    /// they don't add up.
    fn finish(&mut self, eof: u32) -> Result<(), HeaderError> {
        // With no chunk walked, only the container's size field tells an empty container from one
        // truncated before its first chunk header. After a chunk, ending on a chunk boundary is
        // the end of the container.
        if !self.saw_a_chunk && 8u32.checked_add(self.declared_after_top) != Some(eof) {
            return Err(HeaderError::Truncated {
                needs_at_least: eof.saturating_add(CHUNK_HEADER_LEN),
            });
        }

        if !self.found_data_chunk {
            return Err(HeaderError::Malformed(Reason::NoDataChunk));
        }
        let num_channels = match self.num_channels {
            Some(c) => c,
            None => return Err(HeaderError::Malformed(Reason::NoFmtChunk)),
        };
        let byte_depth = match self.byte_depth {
            Some(d) => d,
            None => return Err(HeaderError::Malformed(Reason::NoFmtChunk)),
        };

        // Resolve AIFF `INST` sustain-loop marker IDs through the `MARK` table. Done here because
        // `MARK` may come before or after `INST`. An ID with no matching marker leaves that loop
        // point at 0 (no loop).
        //
        // Inert on a [`WalkGoal::AudioOnly`] walk only because the gated `INST` arm is the sole
        // writer of `sustain_loop_*_marker_id`; `MARK` is read on every walk. Anything else that
        // writes those IDs must be gated too, or audio-only walks gain loop points.
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

/// Returns the part of `window` from absolute offset `start` on, or an empty slice if the window
/// does not cover `start`.
fn window_at(window: &[u8], window_start: u32, start: u32) -> &[u8] {
    match start.checked_sub(window_start) {
        Some(offset) => window.get(offset as usize..).unwrap_or(&[]),
        // The window begins after `start`.
        None => &[],
    }
}

/// Returns whether `window` fully contains the `len` bytes at absolute offset `start`.
fn covers(window: &[u8], window_start: u32, start: u32, len: u32) -> bool {
    match start.checked_sub(window_start) {
        Some(offset) => window.len() as u64 >= u64::from(offset) + u64::from(len),
        None => false,
    }
}

/// Decodes the header from a prefix of a file held in memory.
///
/// For callers that already hold the file (tests, tools). Streaming callers should drive
/// [`HeaderParser`], which never needs more than [`MAX_WINDOW_BYTES`] at once.
///
/// # Errors
///
/// [`ParseError::NeedAtLeast`] when the walk needs bytes beyond `bytes`; otherwise why the file
/// could not be decoded.
pub fn parse(bytes: &[u8]) -> Result<AudioHeader, ParseError> {
    let mut parser = HeaderParser::for_prefix();
    let mut request = HeaderParser::FIRST_REQUEST;
    loop {
        // Past the prefix we cannot tell end of file from end of input, so ask for more.
        let (start, len) = request;
        if start as usize > bytes.len() {
            return Err(ParseError::NeedAtLeast(start));
        }
        let from = start as usize;
        let to = from.saturating_add(len as usize).min(bytes.len());
        match parser.feed(&bytes[from..to], start)? {
            FeedResult::Done(header) => return Ok(header),
            FeedResult::Need { start, len } => request = (start, len),
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
        // Malformed (no fmt/data), not NotAudio: the container was recognised.
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

    /// Drives a size-aware walk over `file` the way a device caller does: ask, read exactly what
    /// was asked, feed.
    fn decode_streamed(file: &[u8]) -> Result<AudioHeader, HeaderError> {
        let mut parser = HeaderParser::new(file.len() as u32);
        loop {
            let Some((start, len)) = parser.pending() else {
                unreachable!("the walk only ever ends inside `feed`")
            };
            let from = (start as usize).min(file.len());
            let to = from.saturating_add(len as usize).min(file.len());
            match parser.feed(&file[from..to], start)? {
                FeedResult::Done(header) => return Ok(header),
                FeedResult::Need { .. } => {}
            }
        }
    }

    #[test]
    fn a_tail_too_short_for_a_chunk_header_ends_the_walk() {
        // Trailing slop (DAW padding, an interrupted copy, a card that filled mid-write) of 1 to 7
        // bytes must decode to the same header as the clean file.
        let clean = riff_fmt_data(2, 44100, 16, 8);
        let expected = decode_streamed(&clean).expect("the file itself must decode");
        for tail in 1..CHUNK_HEADER_LEN {
            let mut slopped = clean.clone();
            slopped.extend(core::iter::repeat_n(0xAA, tail as usize));
            assert_eq!(
                decode_streamed(&slopped),
                Ok(expected),
                "{tail} trailing bytes must not change the header"
            );
        }
        // 8 trailing bytes read as a chunk header whose length runs off the end, which also ends
        // the walk with the same header.
        let mut chunk_sized = clean.clone();
        chunk_sized.extend(core::iter::repeat_n(0xAA, CHUNK_HEADER_LEN as usize));
        assert_eq!(decode_streamed(&chunk_sized), Ok(expected));
    }

    /// [`decode_streamed`] with a goal, also returning how far into the file the caller read.
    fn decode_streamed_with_goal(
        file: &[u8],
        goal: WalkGoal,
    ) -> (Result<AudioHeader, HeaderError>, u32) {
        let mut parser = HeaderParser::new_with_goal(file.len() as u32, goal);
        let mut read_to = 0;
        loop {
            let Some((start, len)) = parser.pending() else {
                unreachable!("the walk only ever ends inside `feed`")
            };
            let from = (start as usize).min(file.len());
            let to = from.saturating_add(len as usize).min(file.len());
            read_to = read_to.max(to as u32);
            if parser.located_audio_data() && goal == WalkGoal::AudioOnly {
                return (parser.finish_here(), read_to);
            }
            match parser.feed(&file[from..to], start) {
                Ok(FeedResult::Done(header)) => return (Ok(header), read_to),
                Ok(FeedResult::Need { .. }) => {}
                Err(error) => return (Err(error), read_to),
            }
        }
    }

    #[test]
    fn an_audio_only_walk_leaves_the_reader_behind_the_audio() {
        // A 36-byte `smpl` (no loops) between `fmt ` and `data`. Its fixed 52-byte window would
        // swallow the `data` header behind it and put the reader past the audio start.
        let mut smpl = std::vec::Vec::new();
        smpl.extend_from_slice(b"smpl");
        smpl.extend_from_slice(&36u32.to_le_bytes());
        for word in [0u32; 9] {
            smpl.extend_from_slice(&word.to_le_bytes());
        }
        let mut file = riff_fmt(1, 44100, 16, 1);
        file.extend_from_slice(&smpl);
        file.extend_from_slice(b"data");
        file.extend_from_slice(&2048u32.to_le_bytes());
        file.resize(file.len() + 2048, 0);
        // Patch the RIFF size now the body is complete.
        let riff_size = (file.len() - 8) as u32;
        file[4..8].copy_from_slice(&riff_size.to_le_bytes());

        let (header, read_to) = decode_streamed_with_goal(&file, WalkGoal::AudioOnly);
        let header = header.expect("the file must decode");
        assert!(
            read_to <= header.audio_data_start_pos_bytes,
            "the walk read to {read_to}, past the audio at {}",
            header.audio_data_start_pos_bytes
        );

        // The whole-header walk reads it (root note and loop points live there) and overshoots.
        let (whole, whole_read_to) = decode_streamed_with_goal(&file, WalkGoal::WholeHeader);
        let whole = whole.expect("the file must decode");
        assert_eq!(
            whole.audio_data_start_pos_bytes,
            header.audio_data_start_pos_bytes
        );
        assert!(whole_read_to > whole.audio_data_start_pos_bytes);
    }

    #[test]
    fn a_short_fmt_cannot_pull_an_audio_only_walk_into_the_audio() {
        // `fmt `'s extent is 16, so a `fmt ` declaring 4 bytes with `data` right behind it puts
        // the audio inside the window. The audio is chosen so `wBitsPerSample` would read as a
        // plausible 16 (0x0010 LE), so nothing else stops the walk.
        let mut file = std::vec::Vec::new();
        file.extend_from_slice(b"RIFF");
        file.extend_from_slice(&0u32.to_le_bytes()); // patched below
        file.extend_from_slice(b"WAVE");
        file.extend_from_slice(b"fmt ");
        file.extend_from_slice(&4u32.to_le_bytes()); // declares 4 of the 16 read
        file.extend_from_slice(&1u16.to_le_bytes()); // wFormatTag = PCM
        file.extend_from_slice(&1u16.to_le_bytes()); // wChannels = 1
        file.extend_from_slice(b"data");
        file.extend_from_slice(&8u32.to_le_bytes());
        file.extend_from_slice(&[0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00]);
        let riff_size = (file.len() - 8) as u32;
        file[4..8].copy_from_slice(&riff_size.to_le_bytes());
        // 12 container + 8 `fmt ` header + 4 payload + 8 `data` header.
        let audio_start = 32;

        // Whatever the decode's outcome, the reader must not have entered the audio.
        let (_, read_to) = decode_streamed_with_goal(&file, WalkGoal::AudioOnly);
        assert!(
            read_to <= audio_start,
            "an audio-only walk read to {read_to}, into the audio at {audio_start}"
        );

        // The whole-header walk keeps the fixed 16-byte over-read.
        let (_, whole_read_to) = decode_streamed_with_goal(&file, WalkGoal::WholeHeader);
        assert_eq!(whole_read_to, 36);
    }

    #[test]
    fn only_a_whole_header_walk_insists_on_a_playable_rate() {
        // 192 kHz mono, a whole number of 2048-sample cycles: unplayable as a sample, a valid
        // wavetable.
        let file = riff_fmt_data(1, 192_000, 16, 2048 * 2);
        assert_eq!(
            decode_streamed(&file),
            Err(HeaderError::Unsupported(Reason::SampleRateOutOfRange))
        );
        let (header, _) = decode_streamed_with_goal(&file, WalkGoal::AudioOnly);
        let header = header.expect("an audio-only walk must not judge the rate");
        assert_eq!(header.sample_rate, 192_000);
        assert_eq!(header.num_channels, 1);
        assert_eq!(header.audio_data_length_bytes, 4096);
    }

    #[test]
    fn a_short_tail_still_asks_for_more_in_prefix_mode() {
        // `parse`'s slice is a prefix, so a short tail means "supply more", not end of file.
        let clean = riff_fmt_data(2, 44100, 16, 8);
        let mut slopped = clean.clone();
        slopped.extend_from_slice(&[0xAA, 0xAA, 0xAA]);
        assert_eq!(
            parse(&slopped),
            Err(ParseError::NeedAtLeast(
                clean.len() as u32 + CHUNK_HEADER_LEN
            ))
        );
    }

    #[test]
    fn the_container_is_known_after_the_first_window() {
        let file = form_empty();
        let mut parser = HeaderParser::new(file.len() as u32);
        assert_eq!(parser.container(), None, "nothing has been read yet");
        let (start, len) = parser
            .pending()
            .expect("a fresh parser wants the top header");
        let window = &file[start as usize..(start + len) as usize];
        // The walk fails (an empty container has no data chunk), but the container is known.
        let _ = parser.feed(window, start);
        assert_eq!(parser.container(), Some(Container::Aiff));

        let mut parser = HeaderParser::new(4);
        let _ = parser.feed(b"junk", 0);
        assert_eq!(parser.container(), None, "no container was identified");
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
        // midiNote - fineTune * 0.01.
        assert_eq!(h.midi_note, Some(60.0 - (-50.0) * 0.01));
    }

    #[test]
    fn inst_note_out_of_range_is_ignored() {
        let h = parse(&riff_with_inst(200, 0)).expect("should parse");
        // Notes >= 128 are ignored.
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
        // 2048 by default, even with no clm chunk.
        assert_eq!(h.wavetable.cycle_size, 2048);
    }

    #[test]
    fn clm_digit_overflow_does_not_panic() {
        // A run of digits far longer than fits a u32, in case the digit parse ever reads past
        // the fixed four. Asserts only that `parse` returns.
        let mut payload = std::string::String::from("<!>");
        payload.push_str(&"9".repeat(200));
        let _ = parse(&riff_with_clm(&payload));
    }

    #[test]
    fn later_chunk_wins_when_smpl_and_inst_both_set_the_note() {
        // smpl then inst: inst, later in the file, sets the note.
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
        // A `smpl` declaring 0xFFFF_FFFF loops with no loop records: only a count of exactly 1
        // is honoured, so the count must never drive iteration.
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
        // A COMM sample rate with an all-ones exponent (the extended infinity/NaN encoding) and a
        // zero mantissa. The decode must return without an out-of-range shift.
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
        // INST names IDs 7/8, MARK defines only 1/2, so both loop points stay 0. Built by hand
        // because `form_with_markers` uses the same IDs for MARK and INST.
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
        // Nine markers against a table of eight. INST names marker 8 (the last inside the bound)
        // as loop start and marker 9 (the first past it) as loop end: 8 must resolve and 9 must
        // not. A missing or off-by-one clamp either cuts the table short or panics on the 9th
        // write.
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
        // MARK declares 0xFFFF markers but holds none: the count is clamped and every marker read
        // is bounded by the chunk.
        let mut v = form_comm_ssnd(1, 44100, 16, 8);
        v.extend_from_slice(b"MARK");
        v.extend_from_slice(&2u32.to_be_bytes()); // declared chunk payload: just the count
        v.extend_from_slice(&0xFFFFu16.to_be_bytes()); // hostile numMarkers
        let _ = parse(&v); // Must return, not panic and not hang.
    }

    #[test]
    fn aiff_hostile_mark_pstring_overruns_chunk_does_not_panic() {
        // One marker whose name length runs past the MARK chunk's end; its id and position are
        // intact.
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
        // Chunk sizes near u32::MAX must not overflow when stepped over and padded. Any `Err` is
        // fine; the point is that `parse` returns.
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
