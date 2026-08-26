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
//! **The parser is told the file's size**, which is what makes the walk finite
//! by construction rather than by the caller behaving: no request can name a
//! byte past the end, so the end of the file is not something a caller has to
//! signal — and cannot get wrong. That is also why the two entry points return
//! different errors. [`HeaderParser`] fails with [`HeaderError`], which has no
//! "supply more and retry" variant to loop on; only [`parse`], whose slice
//! really is a prefix the caller can extend, has [`ParseError::NeedAtLeast`].
//!
//! [`parse`] remains as a convenience for callers that hold a whole file (tests,
//! tools, the corpus differential).
//!
//! Wavetable *acceptance* deliberately lives elsewhere ([`wavetable`]): this
//! module reports what a file says, not whether some caller may use it.

#![no_std]
// `deny`, not `forbid`: the C ABI in [`abi`] cannot hand a raw pointer back to
// safe Rust without it, and `forbid` cannot be lifted even for one module. That
// module is the only place the lint is relaxed, and it says so at its top; the
// decoder itself stays unsafe-free.
#![deny(unsafe_code)]

#[cfg(test)]
extern crate std;

pub mod abi;
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

/// Why a streaming decode stopped.
///
/// Deliberately has no "supply more and retry": a [`HeaderParser`] is told the
/// file's size at construction and never asks for a byte past it, so "the
/// caller must read further" is not a state this type can express. That is the
/// whole difference from [`ParseError`], whose [`ParseError::NeedAtLeast`]
/// means exactly that — and an adapter looping on the two as if they were the
/// same thing would re-read a file that has no more bytes to give, forever.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HeaderError {
    /// No container we recognise. Distinct from `Malformed` so a caller can
    /// tell "not an audio file" from "a broken audio file".
    NotAudio,
    Malformed(Reason),
    Unsupported(Reason),
    /// The file ends before the header does. `needs_at_least` is how long the
    /// file would have to be for the field being read to exist — a fact about
    /// the file, not an instruction to fetch more.
    Truncated {
        needs_at_least: u32,
    },
}

impl From<HeaderError> for ParseError {
    /// [`parse`] treats its slice as a *prefix* the caller can extend, so a
    /// file that ends too early is the one case that really is "supply more and
    /// call again" — the only place the two meanings legitimately meet.
    fn from(error: HeaderError) -> Self {
        match error {
            HeaderError::NotAudio => ParseError::NotAudio,
            HeaderError::Malformed(reason) => ParseError::Malformed(reason),
            HeaderError::Unsupported(reason) => ParseError::Unsupported(reason),
            HeaderError::Truncated { needs_at_least } => ParseError::NeedAtLeast(needs_at_least),
        }
    }
}

/// What a walk is FOR: which chunks it reads, and which of the file's claims it
/// insists on.
///
/// Not a style preference — the C++ parser this replaces branched on the same
/// distinction (`ParseMode`), and dropping it changed which files load. A
/// wavetable load uses only the audio's shape and location; it never asks what
/// rate the file plays back at, and it never renders a note. So the metadata
/// that answers those questions is neither read nor believed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum WalkGoal {
    /// Everything the header says. What a Sample load wants.
    WholeHeader = 0,
    /// Only what locates and shapes the audio. What a WaveTable load wants:
    ///
    /// * per-note playback metadata (`smpl`, `inst`, AIFF `INST`) is stepped
    ///   over unread, so a root note and loop points stay unset — and, because
    ///   those chunks are read at a fixed extent rather than at their declared
    ///   length, so does the caller's reader stay behind the audio it is about
    ///   to read;
    /// * the sample rate is still reported but no longer has to be one we can
    ///   play. A 192 kHz single-cycle file is a perfectly good wavetable;
    ///   wavetable rendering never looks at the rate.
    AudioOnly = 1,
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
/// this many bytes, not the file.
///
/// **It is a floor, not a serving size.** A caller should hand back whatever it
/// already holds — a whole SD cluster is ideal — and the parser will satisfy as
/// many requests from that one window as it covers. The difference is not
/// cosmetic. A walk costs one request per chunk header, and a file whose `data`
/// chunk *under-declares* its length (a recorder that never fixed up the header,
/// a card that filled mid-write — not hostile input, just common damage) makes
/// the parser read the audio itself as a stream of nonsense chunk headers. On a
/// 1 MB file of that shape the walk takes 131,075 feeds at exact windows,
/// 2,050 at 512 bytes, 258 at 4 KiB, and 34 at 32 KiB. The parse is bounded and
/// correct either way; only the number of round trips to the card changes, and
/// on a device those are what you are paying for.
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
            // 8, where the C++ oracle read only the 4-byte offset. Reading the
            // unused `blockSize` behind it costs nothing and reaches no further
            // than the audio does anyway: an SSND's own audio starts at
            // `data_start + 8 + offset`, so this window stops exactly where the
            // earliest possible audio byte is.
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
    Failed(HeaderError),
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
/// # fn read_exact_at(_f: &[u8], _s: u32, _l: u32) -> &'static [u8] { &[] }
/// # let file_size = 0u32;
/// let mut parser = HeaderParser::new(file_size);
/// while let Some((start, len)) = parser.pending() {
///     // `start + len` is inside the file, so this always reads real bytes.
///     // Handing back MORE than `len` — a whole cluster, say — is better still.
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
    /// The file's true size, when the caller knows it — which every real caller
    /// does: the filesystem has it, and the C++ parser this replaces bounds its
    /// own walk with `AudioByteSource::size()`. Knowing it is what makes
    /// [`HeaderParser::pending`] unable to name a byte past the end, and so
    /// makes the walk finite by construction.
    ///
    /// `None` is prefix mode, reachable only through [`parse`] (see
    /// [`HeaderParser::for_prefix`]), where the end of the file is not known
    /// and running out of bytes has to mean "ask for more" instead.
    file_size: Option<u32>,
    /// What this walk is for; see [`WalkGoal`]. Fixed at construction.
    goal: WalkGoal,
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

impl HeaderParser {
    /// A parser positioned at the start of a `file_size`-byte file, wanting its
    /// container header.
    ///
    /// Passing the size is not a convenience — it is what bounds the walk. Every
    /// request is clamped to what the file actually holds, so a request past the
    /// end cannot be expressed, and a caller cannot answer one by padding.
    #[must_use]
    pub const fn new(file_size: u32) -> Self {
        Self::with_size(Some(file_size), WalkGoal::WholeHeader)
    }

    /// A parser positioned at the start of a `file_size`-byte file, reading only
    /// what `goal` calls for.
    #[must_use]
    pub const fn new_with_goal(file_size: u32, goal: WalkGoal) -> Self {
        Self::with_size(Some(file_size), goal)
    }

    /// A parser for a caller that does not know where the file ends: running out
    /// of bytes means "supply more", not "the file stopped".
    ///
    /// Private on purpose. It is the shape [`parse`] needs — its slice is a
    /// prefix, not a file — and the one shape whose walk is not bounded by a
    /// size, so it is not something a caller can reach for by accident.
    const fn for_prefix() -> Self {
        Self::with_size(None, WalkGoal::WholeHeader)
    }

    /// The window a fresh parser wants: the container header. Exposed so
    /// [`parse`] can drive the walk without a "what if it's already finished"
    /// branch that could never be taken.
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

    /// The window `feed` is waiting for as `(start, len)`, or `None` once the
    /// parse has finished (successfully or not).
    ///
    /// Both halves are inside the file: `start + len` never exceeds the size
    /// this parser was built with, so `read_exact` into a `len`-sized buffer is
    /// always the right thing and there is never anything to pad.
    #[must_use]
    pub fn pending(&self) -> Option<(u32, u32)> {
        match self.phase {
            Phase::Done(_) | Phase::Failed(_) => None,
            _ => Some(self.request()),
        }
    }

    /// The container the file declares, as soon as the 12-byte top header has
    /// been read — which is after the very first [`HeaderParser::feed`], long
    /// before the header is decoded.
    ///
    /// It is exposed because a caller can have a verdict that depends only on
    /// the container and should not wait for the rest of the walk: loading a
    /// file as a wavetable rejects AIFF outright unless the user insists, and
    /// the C++ parser this replaces made that call straight after detecting the
    /// container, before reading a single chunk. Deciding it later instead would
    /// report a *decode* failure for a malformed AIFF, and the browser reads
    /// that error as "this file cannot be a wavetable, but try it as a sample"
    /// — so the difference is whether such a file still loads.
    ///
    /// `None` before the top header has been read, and on a walk that failed
    /// without identifying a container.
    #[must_use]
    pub fn container(&self) -> Option<Container> {
        match self.phase {
            // Not read yet.
            Phase::Top => None,
            // `step_top` is the only step that fails without a container in
            // hand, by way of these two errors. `Truncated` can also come from a
            // later step, so it is qualified by "no chunk was ever walked" —
            // conservative in the one case where the container is in fact known
            // (a file whose FIRST chunk header is truncated), which costs a
            // caller nothing but a decode error it would have reported anyway.
            Phase::Failed(HeaderError::NotAudio) => None,
            Phase::Failed(HeaderError::Truncated { .. }) if !self.saw_a_chunk => None,
            _ => Some(self.container),
        }
    }

    /// Whether the walk has located the audio data: a `data`/`SSND` chunk has
    /// been read and [`AudioHeader::audio_data_start_pos_bytes`] /
    /// [`AudioHeader::audio_data_length_bytes`] can no longer change.
    ///
    /// Pair it with [`HeaderParser::finish_here`] to stop the walk at that point.
    /// A caller reading a file as a *wavetable* wants exactly that: everything
    /// past the audio chunk is metadata a wavetable does not use, and walking it
    /// drags the caller's own reader past the audio it is about to read — which
    /// is why the C++ parser this replaces had a separate WaveTable mode that
    /// stopped here.
    ///
    /// AIFF's `SSND` carries an offset in its payload, so this stays false until
    /// that payload has been stepped, not merely until the chunk header has.
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

    /// End the walk here and resolve the header from what has been read so far.
    ///
    /// The same finalisation the end of the file would have run, just earlier:
    /// the fields the remaining chunks would have set keep the values they have
    /// (loop points and root note stay at their "the file said nothing"
    /// defaults). Fails for the same reasons the end of a file does — no
    /// `data`/`SSND` chunk, no `fmt `/`COMM` chunk — so it is only meaningful
    /// once [`HeaderParser::located_audio_data`] is true.
    ///
    /// Idempotent: on an already-finished walk it re-reports that outcome
    /// instead of finishing twice.
    pub fn finish_here(&mut self) -> Result<AudioHeader, HeaderError> {
        match self.phase {
            Phase::Done(header) => return Ok(header),
            Phase::Failed(error) => return Err(error),
            _ => {}
        }
        // `finish`'s only use of `eof` is telling a genuinely empty container
        // from one truncated before its first chunk header, which no walk that
        // has read a chunk can be. The file's own size is the honest answer
        // regardless, and prefix mode has none to give.
        self.try_step_finish(self.file_size.unwrap_or(0))?;
        match self.phase {
            Phase::Done(header) => Ok(header),
            // `finish` either sets `Done` or returns the error `try_step_finish`
            // has already turned into `Failed` above.
            _ => Err(HeaderError::Malformed(Reason::NoDataChunk)),
        }
    }

    /// [`HeaderParser::finish`] with the same failure bookkeeping every other
    /// step gets: an error is recorded on the parser, not just returned.
    fn try_step_finish(&mut self, eof: u32) -> Result<(), HeaderError> {
        if let Err(error) = self.finish(eof) {
            self.phase = Phase::Failed(error);
            return Err(error);
        }
        Ok(())
    }

    /// Hand the parser the bytes it asked for.
    ///
    /// `window_start` is the absolute file offset `window` begins at; it should
    /// be the `start` of the pending request. Supplying **more** than was asked
    /// for is not merely allowed but wanted — see [`MAX_WINDOW_BYTES`] — and the
    /// parser will satisfy as many further requests from the same window as it
    /// fully covers before asking again.
    ///
    /// Supplying **less** is a failed read: the request was already clamped to
    /// the file, so the bytes exist. It is reported as
    /// [`HeaderError::Truncated`] rather than quietly ending the walk with a
    /// half-built header. There is nothing to pad and no reason to.
    ///
    /// The end of the file needs no answer at all: the parser knows where it is
    /// and finishes the walk itself.
    pub fn feed(&mut self, window: &[u8], window_start: u32) -> Result<FeedResult, HeaderError> {
        loop {
            match self.phase {
                Phase::Done(header) => return Ok(FeedResult::Done(header)),
                Phase::Failed(error) => return Err(error),
                _ => {}
            }

            let (start, len) = self.request();
            if self.past_end(start) {
                // The file has nothing at this offset. That is the end of the
                // walk, and the parser already knows it — no round trip, and no
                // way for a caller to answer with padding instead.
                self.try_step(&[])?;
                continue;
            }

            let available = window_at(window, window_start, start);
            if self.file_size.is_some() && (available.len() as u32) < len {
                // Clamped above, so these bytes are inside the file: a short
                // answer is a read that failed, not an end of file.
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

    /// Whether the file is known to hold nothing at `start`.
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
            // `start < size` holds whenever this is reached: `feed` resolves a
            // request at or past the end itself, without asking the caller.
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
            // Terminal phases are filtered out by `pending`/`feed` before this
            // is reached; a zero-length request is inert if one ever isn't.
            Phase::Done(_) | Phase::Failed(_) => (0, 0),
        }
    }

    /// How much of a chunk's payload to ask for.
    ///
    /// [`PayloadKind::window_len`] is a fixed extent per chunk type, NOT the
    /// chunk's declared length — deliberately, because that is what the C++
    /// oracle read. On a whole-header walk it is used as is, over-reads and all:
    /// those over-reads are what the corpus differential was validated against,
    /// and nothing downstream cares where the walk left the caller's reader.
    ///
    /// An audio-only walk does owe the caller that, because a wavetable load
    /// hands the same reader to the code that reads the audio (see
    /// [`WalkGoal::AudioOnly`]). So the window is bounded to this chunk plus one
    /// chunk header — the earliest point at which the NEXT chunk's payload, and
    /// hence the audio, could begin. That makes "the walk never reads past
    /// `audio_data_start_pos_bytes`" true by arithmetic rather than by no real
    /// file having tripped it.
    ///
    /// It bites only where a chunk declares less than `window_len - 8`: in
    /// practice a `fmt ` claiming under 8 bytes, which is malformed either way
    /// and now fails the decode instead of reading its bit depth out of the
    /// audio. The `clm ` over-read (7 bytes from a chunk that declares 4) is
    /// untouched — 4 + 8 is still past 7 — so that quirk survives, as it must.
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
        // Bytes 4..8 declare how many bytes follow them: the 4-byte format id
        // plus every chunk after it. A truthful, fully-supplied empty container
        // is self-describing — the file's real end equals `8 + declared`.
        // That's the only signal available for telling a genuinely empty
        // container (no chunks at all) apart from a file truncated before its
        // first chunk header; once the walk has read a real chunk we no longer
        // need it (see `finish`).
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
            // A few bytes that cannot hold a chunk header are the end of the
            // walk, not a truncated one — when we know where the file ends.
            //
            // In size-aware mode the request was clamped to the file, so a short
            // window means the FILE stops here: there is no chunk at this offset
            // and no bytes are coming. The C++ parser this replaces reached the
            // same conclusion by different means — its 8-byte read failed, broke
            // the walk, and returned the header it had already built — and files
            // shaped like that load today. Trailing slop is mundane (DAW
            // padding, an interrupted copy, a card that filled mid-write), so
            // treating it as corruption would stop such a file loading.
            //
            // Prefix mode has no size to reason with: there, running out of
            // bytes really can mean the caller simply stopped reading, so it
            // stays "supply more" (see the type docs on [`HeaderError`] vs
            // [`ParseError`]).
            if self.file_size.is_some() {
                let eof = at.saturating_add(win.len() as u32);
                return self.finish(eof);
            }
            return Err(HeaderError::Truncated {
                needs_at_least: header_start.saturating_add(CHUNK_HEADER_LEN),
            });
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
                // A Sample parse keeps walking after the data chunk (to pick up
                // `smpl`/`inst`/`clm ` metadata, which legally follow it) rather
                // than stopping here — see audio_file_format.cpp:74-76. The
                // payload itself is stepped over, never read.
                self.advance_past(chunk)
            }
            // The three per-note metadata chunks. An audio-only walk steps over
            // them unread, exactly as the C++ oracle's `isSample` gate did
            // (audio_file_format.cpp:215, :252, :404). That is not merely saved
            // work: their windows are a fixed extent, NOT the chunk's declared
            // length, so reading a `smpl` that declares less than 52 bytes (a
            // no-loops sampler chunk declares 36) walks the CALLER's reader past
            // the audio chunk header behind it — and a wavetable load hands that
            // same reader, position and all, to the code that reads the audio.
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
                // COMM is exactly 18 bytes. The C++ oracle rejects any other
                // declared length outright rather than reading a partial or
                // extended COMM (audio_file_format.cpp:317-319) — and it does so
                // before looking at the payload at all, so this check precedes
                // the window request.
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
            // Chunks we don't inspect — including the three above on an
            // audio-only walk — are skipped by arithmetic alone.
            _ => self.advance_past(chunk),
        }
    }

    /// Whether this walk reads the chunks that describe playing a note back
    /// (`smpl`, `inst`, AIFF `INST`). See [`WalkGoal::AudioOnly`].
    const fn reads_note_metadata(&self) -> bool {
        matches!(self.goal, WalkGoal::WholeHeader)
    }

    /// Whether the file's sample rate has to be one we can play back.
    ///
    /// Only a Sample load has to be able to play the file at its own rate; a
    /// wavetable is rendered from the audio's shape, and never asks. The C++
    /// oracle gated the range check the same way (audio_file_format.cpp:204 for
    /// `fmt `, :347 for `COMM`), and a 192 kHz single-cycle file browsed as a
    /// wavetable loaded and played because of it.
    const fn insists_on_a_playable_rate(&self) -> bool {
        matches!(self.goal, WalkGoal::WholeHeader)
    }

    fn wait_for(&mut self, kind: PayloadKind, chunk: Chunk) -> Result<(), HeaderError> {
        self.phase = Phase::Payload { kind, chunk };
        Ok(())
    }

    /// Step over the rest of `chunk` — payload included, unread — and line up
    /// the next chunk header. RIFF/IFF chunks are word-aligned, so an odd
    /// payload is followed by a pad byte, folded into the next request.
    fn advance_past(&mut self, chunk: Chunk) -> Result<(), HeaderError> {
        let unpadded_end = match chunk.data_start.checked_add(chunk.raw_size) {
            Some(v) => v,
            // `raw_size` comes straight from the file: a hostile size that walks
            // the cursor past the end of the address space can never be
            // satisfied by any buffer.
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
        // Each arm below asks for exactly the bytes it reads and gets back a
        // fixed-size array, so every read after that is bounds-checked by the
        // type rather than by a rule kept in step with it by hand. The arms
        // that tolerate a short window (metadata whose absence just leaves a
        // field at its default, mirroring the C++ oracle's bounds-checked reads
        // that silently skip) simply never ask.
        let short = |needed: u32| HeaderError::Truncated {
            needs_at_least: chunk.data_start.saturating_add(needed),
        };

        match kind {
            PayloadKind::Fmt => {
                // fmt chunk layout: wFormatTag:u16, wChannels:u16, dwSampleRate:u32,
                // dwByteRate:u32 (unused), wBlockAlign:u16, wBitsPerSample:u16.
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
                    // PCM: RawDataFormat stays whatever the bit-depth arm above set
                    // (Native, or Unsigned8 for 8-bit).
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
                // Recorded even on a walk that will not insist on it (where the
                // C++ oracle left the field at its 44100 default). Nothing reads
                // it on that path — a `WaveTableHeader` has no sample rate — and
                // a field whose value depends on why it was read would be a trap
                // for whoever reads it next.
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
                if self.insists_on_a_playable_rate() && !(5000..=96000).contains(&sample_rate) {
                    return Err(HeaderError::Unsupported(Reason::SampleRateOutOfRange));
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
                let fields = win
                    .first_chunk::<{ SSND_PREFIX_LEN as usize }>()
                    .ok_or_else(|| short(SSND_PREFIX_LEN))?;
                let offset = u32::from_be_bytes([fields[0], fields[1], fields[2], fields[3]]);

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
    ) -> Result<(), HeaderError> {
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
            // The `+ 2` is the count field just read; `checked_add` above
            // proved it doesn't overflow, but say so in the same idiom the rest
            // of the walk uses rather than relying on the reader to remember.
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
        let cursor = cursor.saturating_add(1);

        // THE ONE DELIBERATE BEHAVIOUR CHANGE IN THIS PARSER, and this comment
        // is its record — nothing outside the source justifies it.
        //
        // The pstring-skip amount below is a correction, not a mirror, of the
        // C++ parser's `(stringLength + 1) & ~1` (audio_file_format.cpp:394-395
        // as it stood at 77bd4488e). That rounds only the *name* length up to
        // even and skips that many bytes after the already-consumed 1-byte
        // length prefix — but the AIFF pstring convention (and what real
        // encoders write) pads the *whole* record, length byte plus name, to an
        // even total. The two rules disagree whenever the name length is odd or
        // zero, which is the common case: 3-letter marker names like
        // "beg"/"end".
        //
        // Consequence of the C++ rule: every marker after the first in a
        // multi-marker file is read one byte off, so its ID and position are
        // garbage, and an AIFF's sustain loop resolves to nothing. Measured over
        // a 40-file AIFF corpus, 25 files' `file_loop_end_samples` came back 0
        // from the C++ and correct from this formula; the corpus differential
        // carries those 25 as its one adjudicated disagreement category. WAV is
        // untouched — it has no MARK chunk — and the golden corpus is all WAV,
        // so no golden can move on this.
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
    fn finish(&mut self, eof: u32) -> Result<(), HeaderError> {
        // If we never walked a real chunk, we can't tell a genuinely empty
        // container from a file truncated before its first chunk header — fall
        // back to the top-level size field. Once at least one chunk has been
        // walked, running out on a chunk boundary is unambiguously the end of
        // the container.
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

        // AIFF marker-resolved loop finalisation: `INST`'s sustain-loop marker
        // IDs only mean anything once the whole header has been scanned, since
        // `MARK` (which maps IDs to sample positions) can appear before or after
        // `INST`.
        //
        // This block is inert on a [`WalkGoal::AudioOnly`] walk, but only by
        // dataflow: the gated `INST` arm is the SOLE writer of
        // `sustain_loop_*_marker_id`, so they keep the -1 that means "no INST
        // seen" and the condition below never holds. `MARK` itself is NOT gated
        // (the C++ oracle read it in both modes), so `marker_ids` really is
        // populated on such a walk. An arm that starts writing a sustain-loop id
        // outside `INST` would silently give an audio-only walk loop points it
        // is not supposed to have — gate it, or gate this. `sustain_loop_end_marker_id != -1` gates on "an INST chunk was
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
    let mut parser = HeaderParser::for_prefix();
    let mut request = HeaderParser::FIRST_REQUEST;
    loop {
        // A request that starts past the prefix is the one thing this shape
        // can't answer: we don't know whether the file really ends here or the
        // caller simply stopped reading, so we ask for more rather than
        // silently truncating the walk. A `HeaderParser` has no such ambiguity —
        // it is told the size and never asks past it.
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

    /// Drive a size-aware walk over a whole file, the way a device caller does:
    /// ask, read exactly what was asked, feed — including the zero-length
    /// requests, which the parser needs in order to terminate.
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
        // Trailing slop is mundane — DAW padding, an interrupted copy, a card
        // that filled mid-write — and the C++ parser this replaces returned the
        // header it had built when its 8-byte chunk-header read ran off the end.
        // Such files load today, so every length from 1 to 7 (exactly where that
        // read gave up) must decode to the same header as the clean file.
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
        // 8 trailing bytes DO hold a chunk header, so they go through the
        // ordinary walk instead — as a chunk claiming a length that runs off the
        // end, which the walk also ends on. The two paths agree, which is the
        // point: the boundary at 8 is not a cliff.
        let mut chunk_sized = clean.clone();
        chunk_sized.extend(core::iter::repeat_n(0xAA, CHUNK_HEADER_LEN as usize));
        assert_eq!(decode_streamed(&chunk_sized), Ok(expected));
    }

    /// [`decode_streamed`] for a walk with a goal, reporting how far the caller
    /// had to read — which is the property an audio-only walk has to keep.
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
        // A `smpl` chunk declaring 36 bytes — the ordinary "sampler chunk, no
        // loops" shape — sits between `fmt ` and `data`. Reading it takes a
        // fixed 52-byte window, which swallows the `data` header behind it and
        // leaves the caller's reader PAST the audio start. A wavetable load
        // hands that same reader to the code that reads the audio, so this is
        // the invariant, not an optimisation.
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
        // Fix up the RIFF size now that the body is complete.
        let riff_size = (file.len() - 8) as u32;
        file[4..8].copy_from_slice(&riff_size.to_le_bytes());

        let (header, read_to) = decode_streamed_with_goal(&file, WalkGoal::AudioOnly);
        let header = header.expect("the file must decode");
        assert!(
            read_to <= header.audio_data_start_pos_bytes,
            "the walk read to {read_to}, past the audio at {}",
            header.audio_data_start_pos_bytes
        );

        // The whole-header walk reads it, as it must — that is where root notes
        // and loop points come from — and overshoots, which is why the two goals
        // exist.
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
        // The `smpl` case above is not the only way a fixed-extent window can
        // reach past the audio: `fmt `'s extent is 16, so a `fmt ` declaring
        // fewer than 8 bytes with `data` right behind it puts the audio inside
        // the window. This file decodes far enough to matter — the tag and
        // channel count are real, and `wBitsPerSample` lands on the second audio
        // frame, 0x0010 LE, which reads as a perfectly ordinary 16 — so nothing
        // upstream stops the walk before the reader is already past the audio.
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

        // Whichever way the decode goes, the reader must not have entered the
        // audio: that is the promise an audio-only walk makes to a caller that
        // is about to read it.
        let (_, read_to) = decode_streamed_with_goal(&file, WalkGoal::AudioOnly);
        assert!(
            read_to <= audio_start,
            "an audio-only walk read to {read_to}, into the audio at {audio_start}"
        );

        // The whole-header walk keeps the C++ oracle's over-read — 16 bytes from
        // a chunk that declares 4 — because that is what the corpus differential
        // was validated against, and nothing downstream reuses its reader.
        let (_, whole_read_to) = decode_streamed_with_goal(&file, WalkGoal::WholeHeader);
        assert_eq!(whole_read_to, 36);
    }

    #[test]
    fn only_a_whole_header_walk_insists_on_a_playable_rate() {
        // 192 kHz, mono, a whole number of 2048-sample cycles: unplayable as a
        // sample, a perfectly good wavetable. It loaded before the parse moved
        // here, because wavetable rendering never reads the rate.
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
        // The other half of the rule above: `parse`'s slice is a prefix, so
        // running out of bytes there really can mean the caller stopped reading.
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
        // The walk fails right after (an empty container has no data chunk), but
        // the container it identified is reportable all the same.
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
