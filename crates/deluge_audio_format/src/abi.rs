//! The C ABI over [`HeaderParser`] — the only place in this crate `unsafe` is
//! allowed, and the only place that knows C exists.
//!
//! **It exposes the windowed protocol, not a one-shot parse.** [`crate::parse`]
//! wants the whole file in one slice, which on the device would mean a 183 MB
//! buffer for the largest file in the golden fixtures against 8 MB of SDRAM (see
//! the crate root docs). So the shape here is the state machine's own:
//! `init` → `pending` → `feed` → … → done, with the caller owning every read.
//! Peak memory is one window, and a window is at most
//! [`MAX_WINDOW_BYTES`](crate::MAX_WINDOW_BYTES) bytes.
//!
//! Two rules a caller must not get wrong, both of which stall the walk forever
//! if broken:
//!
//! * **A zero-length request is a real request.** `pending` legitimately reports
//!   `len == 0` (an empty file, or a walk that reached the end), and the parser
//!   only ever advances inside `feed` — so "nothing to read, skip the feed"
//!   spins on the same request for eternity. Feed the empty window.
//! * **There is no "supply more" retry.** [`HeaderError::Truncated`] is a fact
//!   about the file, not an instruction: the parser is told the size up front and
//!   never asks for a byte past it, so a caller that loops on it is re-reading a
//!   file that has no more bytes to give.
//!
//! Everything crossing this boundary is fixed-width, and every enumeration
//! crosses as a `uint8_t` constant rather than a Rust enum's native layout:
//! `arm-none-eabi` defaults to short enums, so a Rust `#[repr(u8)]` enum and a
//! C `enum` are not the same width unless both are pinned by hand.

// The crate root denies `unsafe_code`; this module is the sanctioned exception
// (see the crate docs). It is scoped to this file, and every `unsafe` block
// below carries the contract that makes it sound.
#![allow(unsafe_code)]

use core::mem::{align_of, size_of};
use core::slice;

use crate::wavetable::{accept_as_wavetable, Reject};
use crate::{
    AudioHeader, Container, FeedResult, HeaderError, HeaderParser, RawDataFormat, WavetableHints,
    MAX_WINDOW_BYTES,
};

// ---------------------------------------------------------------------------
// Status codes
// ---------------------------------------------------------------------------

/// The parser wants the window `pending` reports.
pub const DELUGE_AUDIO_PARSE_NEED: u8 = 0;
/// The header is decoded and has been written to the caller's out-parameter.
pub const DELUGE_AUDIO_PARSE_DONE: u8 = 1;
/// No container we recognise — not an audio file at all.
pub const DELUGE_AUDIO_PARSE_NOT_AUDIO: u8 = 2;
/// The file contradicts itself (no `fmt `/`COMM`, no `data`/`SSND`).
pub const DELUGE_AUDIO_PARSE_MALFORMED: u8 = 3;
/// Valid, but says something we cannot play (bit depth, format tag, channel
/// count, sample rate).
pub const DELUGE_AUDIO_PARSE_UNSUPPORTED: u8 = 4;
/// The file ends before the header does. Not a request for more bytes.
pub const DELUGE_AUDIO_PARSE_TRUNCATED: u8 = 5;
/// The call itself was malformed — a null pointer, or storage too small for the
/// parser. A programming error on the C side, never a property of a file.
pub const DELUGE_AUDIO_PARSE_BAD_CALL: u8 = 6;

/// The header may be used as a wavetable; the cycle size has been written out.
pub const DELUGE_WAVETABLE_ACCEPTED: u8 = 0;
/// More than one channel. Rejected even when the caller insists.
pub const DELUGE_WAVETABLE_REJECT_STEREO: u8 = 1;
/// Bit depth unknown, so the length-in-samples check can't be made.
pub const DELUGE_WAVETABLE_REJECT_UNKNOWN_BIT_DEPTH: u8 = 2;
/// No `clm ` tag and a length that isn't a whole number of 2048-sample cycles.
pub const DELUGE_WAVETABLE_REJECT_NOT_A_WAVETABLE: u8 = 3;
/// An AIFF container, which is only ever a wavetable if the caller insists.
pub const DELUGE_WAVETABLE_REJECT_AIFF: u8 = 4;
/// A null pointer was passed. A programming error, not a file's fault.
pub const DELUGE_WAVETABLE_BAD_CALL: u8 = 5;

/// Mirror of `DELUGE_AUDIO_MAX_WINDOW_BYTES` in `libdeluge/audio_format.h`: the
/// largest window a caller can ever be asked for, and so the smallest buffer it
/// can get away with. Serving more than this is not just allowed but wanted —
/// see [`MAX_WINDOW_BYTES`].
pub const DELUGE_AUDIO_MAX_WINDOW_BYTES: u32 = MAX_WINDOW_BYTES;

// ---------------------------------------------------------------------------
// Plain data crossing the boundary
// ---------------------------------------------------------------------------

/// `Option<f32>` as two fields rather than a NaN sentinel: "the file said
/// nothing" is a fact about the file, and a sentinel would make every consumer
/// responsible for remembering which bit pattern encodes it.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DelugeOptionalF32 {
    /// 0 == the file specified no value; `value` is then meaningless.
    pub has: u8,
    pub value: f32,
}

impl From<Option<f32>> for DelugeOptionalF32 {
    fn from(value: Option<f32>) -> Self {
        match value {
            Some(v) => Self { has: 1, value: v },
            None => Self { has: 0, value: 0.0 },
        }
    }
}

impl From<DelugeOptionalF32> for Option<f32> {
    fn from(value: DelugeOptionalF32) -> Self {
        (value.has != 0).then_some(value.value)
    }
}

/// [`AudioHeader`] flattened for C: every field fixed-width, every enumeration a
/// `uint8_t` holding the Rust enum's own discriminant.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DelugeAudioHeader {
    /// [`Container`]: 0 = WAV, 1 = AIFF.
    pub container: u8,
    pub num_channels: u8,
    pub byte_depth: u8,
    /// [`RawDataFormat`], which the C++ `RawDataFormat` enum matches value for
    /// value.
    pub raw_data_format: u8,
    pub sample_rate: u32,
    /// Offset from the start of the file to the first audio byte.
    pub audio_data_start_pos_bytes: u32,
    pub audio_data_length_bytes: u32,
    /// 0 == no loop — the WAV/AIFF convention, not a sentinel of ours.
    pub file_loop_start_samples: u32,
    pub file_loop_end_samples: u32,
    pub midi_note: DelugeOptionalF32,
    pub wavetable_cycle_size: u32,
    /// 0/1: whether a `clm ` tag declared the file a wavetable.
    pub wavetable_explicitly_specifies_self: u8,
}

impl From<AudioHeader> for DelugeAudioHeader {
    fn from(h: AudioHeader) -> Self {
        Self {
            container: h.container as u8,
            num_channels: h.num_channels,
            byte_depth: h.byte_depth,
            raw_data_format: h.raw_data_format as u8,
            sample_rate: h.sample_rate,
            audio_data_start_pos_bytes: h.audio_data_start_pos_bytes,
            audio_data_length_bytes: h.audio_data_length_bytes,
            file_loop_start_samples: h.file_loop_start_samples,
            file_loop_end_samples: h.file_loop_end_samples,
            midi_note: h.midi_note.into(),
            wavetable_cycle_size: h.wavetable.cycle_size,
            wavetable_explicitly_specifies_self: u8::from(h.wavetable.explicitly_specifies_self),
        }
    }
}

impl From<DelugeAudioHeader> for AudioHeader {
    /// The inverse, for the one call that takes a header back
    /// ([`deluge_audio_header_accept_as_wavetable`]).
    ///
    /// Unknown discriminants can only arise from a header this ABI did not
    /// produce, so they are mapped rather than rejected: `container` falls back
    /// to WAV (the only consumer of it is the AIFF check, which would then
    /// simply not fire) and `raw_data_format` to `Native` (which acceptance
    /// never reads at all).
    fn from(h: DelugeAudioHeader) -> Self {
        Self {
            container: match h.container {
                1 => Container::Aiff,
                _ => Container::Wav,
            },
            num_channels: h.num_channels,
            byte_depth: h.byte_depth,
            raw_data_format: match h.raw_data_format {
                1 => RawDataFormat::Float,
                2 => RawDataFormat::Unsigned8,
                3 => RawDataFormat::EndiannessWrong16,
                4 => RawDataFormat::EndiannessWrong24,
                5 => RawDataFormat::EndiannessWrong32,
                _ => RawDataFormat::Native,
            },
            sample_rate: h.sample_rate,
            audio_data_start_pos_bytes: h.audio_data_start_pos_bytes,
            audio_data_length_bytes: h.audio_data_length_bytes,
            file_loop_start_samples: h.file_loop_start_samples,
            file_loop_end_samples: h.file_loop_end_samples,
            midi_note: h.midi_note.into(),
            wavetable: WavetableHints {
                cycle_size: h.wavetable_cycle_size,
                explicitly_specifies_self: h.wavetable_explicitly_specifies_self != 0,
            },
        }
    }
}

/// One window request: `len` bytes at absolute file offset `start`.
///
/// `len` can legitimately be 0 — see the module docs. Both halves are inside the
/// file the parser was built for.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DelugeAudioWindow {
    pub start: u32,
    pub len: u32,
}

// ---------------------------------------------------------------------------
// Caller-owned parser storage
// ---------------------------------------------------------------------------

/// Storage for a [`HeaderParser`], owned by the C caller.
///
/// The parser's layout stays Rust-side: this is an opaque blob, deliberately not
/// a mirror of the struct. The crate neither allocates nor has an allocator, so
/// the memory has to come from the caller — a local, on a stack that measures in
/// tens of KiB.
///
/// `deluge_audio_header_parser_init` is handed the caller's `sizeof` and refuses
/// storage too small for the parser, so the two sides can never silently drift.
#[repr(C, align(8))]
pub struct DelugeAudioHeaderParser {
    _storage: [u64; DELUGE_AUDIO_HEADER_PARSER_WORDS],
}

/// `sizeof(DelugeAudioHeaderParser)` in 64-bit words. Must equal
/// `DELUGE_AUDIO_HEADER_PARSER_WORDS` in `libdeluge/audio_format.h`; the assert
/// below keeps it ahead of the parser, and the runtime check in `init` keeps it
/// level with C.
pub const DELUGE_AUDIO_HEADER_PARSER_WORDS: usize = 24;

/// The parser must fit its storage, and must not want stricter alignment than
/// the storage promises. Checked here rather than in a test: a caller's buffer
/// is a promise about memory, and growing the parser past it should fail the
/// build, not a test run.
const _: () = {
    assert!(size_of::<HeaderParser>() <= size_of::<DelugeAudioHeaderParser>());
    assert!(align_of::<HeaderParser>() <= align_of::<DelugeAudioHeaderParser>());
};

/// View the caller's storage as the parser living in it.
///
/// # Safety
/// `parser` must be non-null, aligned, and point at storage a previous
/// `deluge_audio_header_parser_init` wrote a `HeaderParser` into. `HeaderParser`
/// is `Copy`-like plain data (no `Drop`, no interior pointers), so the blob is a
/// valid parser for as long as the caller keeps the memory alive.
unsafe fn as_parser<'a>(parser: *const DelugeAudioHeaderParser) -> &'a HeaderParser {
    unsafe { &*parser.cast::<HeaderParser>() }
}

/// Mutable sibling of [`as_parser`], with the same contract.
///
/// # Safety
/// See [`as_parser`]. The caller must additionally not alias the parser.
unsafe fn as_parser_mut<'a>(parser: *mut DelugeAudioHeaderParser) -> &'a mut HeaderParser {
    unsafe { &mut *parser.cast::<HeaderParser>() }
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

/// Place a fresh parser for a `file_size`-byte file in the caller's `storage`.
///
/// `storage_bytes` is the caller's own `sizeof(DelugeAudioHeaderParser)`.
/// Returns `false` — having written nothing — if that is too small for the
/// parser or the pointer is null; nothing else can fail.
///
/// `file_size` must be the filesystem's own size for the file. It is what bounds
/// the walk: no request can name a byte past it, so the end of the file is not
/// something the caller has to signal, and cannot get wrong.
///
/// # Safety
/// `parser` must be non-null and point at `storage_bytes` writable, 8-aligned
/// bytes.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_parser_init(
    parser: *mut DelugeAudioHeaderParser,
    storage_bytes: u32,
    file_size: u32,
) -> bool {
    if parser.is_null() || (storage_bytes as usize) < size_of::<HeaderParser>() {
        return false;
    }
    // SAFETY: non-null, big enough (checked above), and 8-aligned by the type's
    // own `repr(align(8))` on both sides. Writing a fresh value over whatever
    // was there is sound because `HeaderParser` owns nothing that needs dropping.
    unsafe {
        parser
            .cast::<HeaderParser>()
            .write(HeaderParser::new(file_size))
    };
    true
}

/// The window the parser is waiting for, or `false` once the walk is over
/// (either way — done or failed).
///
/// `out->len` may be 0. That is a request like any other: feed it an empty
/// window. The parser only advances inside `feed`, so skipping the call because
/// there is "nothing to read" stalls the walk forever.
///
/// # Safety
/// `parser` must have been initialised by `deluge_audio_header_parser_init`, and
/// `out` must be non-null and writable.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_parser_pending(
    parser: *const DelugeAudioHeaderParser,
    out: *mut DelugeAudioWindow,
) -> bool {
    if parser.is_null() || out.is_null() {
        return false;
    }
    // SAFETY: initialised by the caller's `init` (see this function's contract).
    let Some((start, len)) = (unsafe { as_parser(parser) }).pending() else {
        return false;
    };
    // SAFETY: non-null and writable per this function's contract.
    unsafe { out.write(DelugeAudioWindow { start, len }) };
    true
}

/// Hand the parser `window_len` bytes read from absolute file offset
/// `window_start`.
///
/// Returns one of the `DELUGE_AUDIO_PARSE_*` codes. On
/// [`DELUGE_AUDIO_PARSE_DONE`] the decoded header is written to `header_out`; on
/// [`DELUGE_AUDIO_PARSE_NEED`] call `pending` for the next window. Every other
/// code is terminal — in particular [`DELUGE_AUDIO_PARSE_TRUNCATED`] means the
/// file ends inside its own header, not "read further and try again".
///
/// Supplying **more** than was asked for is wanted, not merely tolerated: the
/// parser satisfies as many subsequent requests from the same window as it fully
/// covers before asking again.
///
/// # Safety
/// `parser` must have been initialised by `deluge_audio_header_parser_init`;
/// `window` must point at `window_len` readable bytes (or be null when
/// `window_len` is 0); `header_out` must be non-null and writable.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_parser_feed(
    parser: *mut DelugeAudioHeaderParser,
    window: *const u8,
    window_len: u32,
    window_start: u32,
    header_out: *mut DelugeAudioHeader,
) -> u8 {
    if parser.is_null() || header_out.is_null() || (window.is_null() && window_len != 0) {
        return DELUGE_AUDIO_PARSE_BAD_CALL;
    }
    // SAFETY: `window` points at `window_len` readable bytes per this function's
    // contract; an empty window needs no pointer at all, and `from_raw_parts`
    // wants a non-null one even for a zero length.
    let bytes: &[u8] = if window_len == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(window, window_len as usize) }
    };

    // SAFETY: initialised by the caller's `init` (see this function's contract).
    let parser = unsafe { as_parser_mut(parser) };
    match parser.feed(bytes, window_start) {
        Ok(FeedResult::Done(header)) => {
            // SAFETY: non-null and writable per this function's contract.
            unsafe { header_out.write(header.into()) };
            DELUGE_AUDIO_PARSE_DONE
        }
        Ok(FeedResult::Need { .. }) => DELUGE_AUDIO_PARSE_NEED,
        Err(error) => error_code(error),
    }
}

/// Whether the walk has located the audio data — i.e. `data`/`SSND` has been
/// read and the header's audio start/length are final.
///
/// This is what lets a caller stop the walk early, the way the C++ parser's
/// WaveTable mode did: everything after the audio-data chunk is metadata a
/// wavetable does not use, and reading it would drag the caller's byte source
/// past the audio it is about to read.
///
/// # Safety
/// `parser` must have been initialised by `deluge_audio_header_parser_init`.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_parser_located_audio_data(
    parser: *const DelugeAudioHeaderParser,
) -> bool {
    if parser.is_null() {
        return false;
    }
    // SAFETY: initialised by the caller's `init` (see this function's contract).
    unsafe { as_parser(parser) }.located_audio_data()
}

/// End the walk here and resolve the header from what has been read so far.
///
/// Meant to be paired with `deluge_audio_header_parser_located_audio_data`.
/// Returns the same `DELUGE_AUDIO_PARSE_*` codes as `feed`, except that
/// [`DELUGE_AUDIO_PARSE_NEED`] is not among them: this call always terminates
/// the walk.
///
/// # Safety
/// `parser` must have been initialised by `deluge_audio_header_parser_init`, and
/// `header_out` must be non-null and writable.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_parser_finish_here(
    parser: *mut DelugeAudioHeaderParser,
    header_out: *mut DelugeAudioHeader,
) -> u8 {
    if parser.is_null() || header_out.is_null() {
        return DELUGE_AUDIO_PARSE_BAD_CALL;
    }
    // SAFETY: initialised by the caller's `init` (see this function's contract).
    match unsafe { as_parser_mut(parser) }.finish_here() {
        Ok(header) => {
            // SAFETY: non-null and writable per this function's contract.
            unsafe { header_out.write(header.into()) };
            DELUGE_AUDIO_PARSE_DONE
        }
        Err(error) => error_code(error),
    }
}

/// Decide whether a decoded header may be used as a wavetable.
///
/// `insist` mirrors the C++ `makeWaveTableWorkAtAllCosts`: it waives the
/// `clm `/length-shape check and the AIFF rejection, but never the mono
/// requirement. On [`DELUGE_WAVETABLE_ACCEPTED`] the cycle size to render with is
/// written to `cycle_size_out`; on every other code nothing is written.
///
/// # Safety
/// `header` and `cycle_size_out` must be non-null and point at valid, writable
/// (for the latter) memory.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_accept_as_wavetable(
    header: *const DelugeAudioHeader,
    insist: bool,
    cycle_size_out: *mut u32,
) -> u8 {
    if header.is_null() || cycle_size_out.is_null() {
        return DELUGE_WAVETABLE_BAD_CALL;
    }
    // SAFETY: non-null and readable per this function's contract.
    let header: AudioHeader = (unsafe { *header }).into();
    match accept_as_wavetable(&header, insist) {
        Ok(use_as) => {
            // SAFETY: non-null and writable per this function's contract.
            unsafe { cycle_size_out.write(use_as.cycle_size) };
            DELUGE_WAVETABLE_ACCEPTED
        }
        Err(Reject::Stereo) => DELUGE_WAVETABLE_REJECT_STEREO,
        Err(Reject::UnknownBitDepth) => DELUGE_WAVETABLE_REJECT_UNKNOWN_BIT_DEPTH,
        Err(Reject::NotAWavetable) => DELUGE_WAVETABLE_REJECT_NOT_A_WAVETABLE,
        Err(Reject::AiffNotSupported) => DELUGE_WAVETABLE_REJECT_AIFF,
    }
}

/// The `Reason` a decode failed for is deliberately not carried across: the C++
/// caller has exactly two error values to map onto (corrupt vs unsupported), so
/// a finer split would cross the boundary only to be discarded.
fn error_code(error: HeaderError) -> u8 {
    match error {
        HeaderError::NotAudio => DELUGE_AUDIO_PARSE_NOT_AUDIO,
        HeaderError::Malformed(_) => DELUGE_AUDIO_PARSE_MALFORMED,
        HeaderError::Unsupported(_) => DELUGE_AUDIO_PARSE_UNSUPPORTED,
        HeaderError::Truncated { .. } => DELUGE_AUDIO_PARSE_TRUNCATED,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::ptr;

    /// Drive the ABI exactly as a C caller must: ask, read, feed, repeat —
    /// including feeding zero-length windows rather than skipping them.
    fn parse_through_abi(file: &[u8], stop_at_audio_data: bool) -> Result<DelugeAudioHeader, u8> {
        let mut storage = DelugeAudioHeaderParser {
            _storage: [0; DELUGE_AUDIO_HEADER_PARSER_WORDS],
        };
        let ok = unsafe {
            deluge_audio_header_parser_init(
                &mut storage,
                size_of::<DelugeAudioHeaderParser>() as u32,
                file.len() as u32,
            )
        };
        assert!(ok, "storage must be big enough for the parser");

        let mut header = DelugeAudioHeader {
            container: 0xFF,
            num_channels: 0,
            byte_depth: 0,
            raw_data_format: 0,
            sample_rate: 0,
            audio_data_start_pos_bytes: 0,
            audio_data_length_bytes: 0,
            file_loop_start_samples: 0,
            file_loop_end_samples: 0,
            midi_note: DelugeOptionalF32 { has: 0, value: 0.0 },
            wavetable_cycle_size: 0,
            wavetable_explicitly_specifies_self: 0,
        };
        let mut want = DelugeAudioWindow { start: 0, len: 0 };
        // A file's header cannot need more requests than it has bytes, plus the
        // handful of zero-length steps at the very end; the bound only exists so
        // a bug shows up as a failed assert rather than a hung test.
        for _ in 0..(file.len() * 2 + 64) {
            if stop_at_audio_data
                && unsafe { deluge_audio_header_parser_located_audio_data(&storage) }
            {
                return match unsafe {
                    deluge_audio_header_parser_finish_here(&mut storage, &mut header)
                } {
                    DELUGE_AUDIO_PARSE_DONE => Ok(header),
                    code => Err(code),
                };
            }
            if !unsafe { deluge_audio_header_parser_pending(&storage, &mut want) } {
                panic!("the walk went terminal without `feed` saying so");
            }
            let from = (want.start as usize).min(file.len());
            let to = (from + want.len as usize).min(file.len());
            let window = &file[from..to];
            let status = unsafe {
                deluge_audio_header_parser_feed(
                    &mut storage,
                    if window.is_empty() {
                        ptr::null()
                    } else {
                        window.as_ptr()
                    },
                    window.len() as u32,
                    want.start,
                    &mut header,
                )
            };
            match status {
                DELUGE_AUDIO_PARSE_NEED => continue,
                DELUGE_AUDIO_PARSE_DONE => return Ok(header),
                code => return Err(code),
            }
        }
        panic!("the walk did not terminate");
    }

    /// RIFF/WAVE around a body of chunks.
    fn wav(body: &[u8]) -> std::vec::Vec<u8> {
        let mut file = std::vec::Vec::new();
        file.extend_from_slice(b"RIFF");
        file.extend_from_slice(&((4 + body.len()) as u32).to_le_bytes());
        file.extend_from_slice(b"WAVE");
        file.extend_from_slice(body);
        file
    }

    fn fmt_chunk(channels: u16, rate: u32, bits: u16) -> std::vec::Vec<u8> {
        let mut c = std::vec::Vec::new();
        c.extend_from_slice(b"fmt ");
        c.extend_from_slice(&16u32.to_le_bytes());
        c.extend_from_slice(&1u16.to_le_bytes());
        c.extend_from_slice(&channels.to_le_bytes());
        c.extend_from_slice(&rate.to_le_bytes());
        c.extend_from_slice(&0u32.to_le_bytes());
        c.extend_from_slice(&0u16.to_le_bytes());
        c.extend_from_slice(&bits.to_le_bytes());
        c
    }

    fn data_chunk(len: usize) -> std::vec::Vec<u8> {
        let mut c = std::vec::Vec::new();
        c.extend_from_slice(b"data");
        c.extend_from_slice(&(len as u32).to_le_bytes());
        c.extend(core::iter::repeat_n(0u8, len));
        c
    }

    fn smpl_chunk(note: u32, loop_start: u32, loop_end: u32) -> std::vec::Vec<u8> {
        let mut c = std::vec::Vec::new();
        c.extend_from_slice(b"smpl");
        c.extend_from_slice(&60u32.to_le_bytes());
        for word in [
            0u32, 0, 0, note, 0, 0, 0, 1, 0, 0, 0, loop_start, loop_end, 0, 0,
        ] {
            c.extend_from_slice(&word.to_le_bytes());
        }
        c
    }

    #[test]
    fn decodes_a_wav_through_the_c_boundary() {
        let mut body = fmt_chunk(2, 48000, 24);
        body.extend(data_chunk(216));
        body.extend(smpl_chunk(57, 1234, 5678));
        let header = parse_through_abi(&wav(&body), false).expect("a valid WAV must decode");

        assert_eq!(header.container, Container::Wav as u8);
        assert_eq!(header.num_channels, 2);
        assert_eq!(header.byte_depth, 3);
        assert_eq!(header.raw_data_format, RawDataFormat::Native as u8);
        assert_eq!(header.sample_rate, 48000);
        assert_eq!(header.audio_data_start_pos_bytes, 44);
        assert_eq!(header.audio_data_length_bytes, 216);
        assert_eq!(header.file_loop_start_samples, 1234);
        assert_eq!(header.file_loop_end_samples, 5678);
        assert_eq!(header.midi_note.has, 1);
        assert_eq!(header.midi_note.value, 57.0);
        assert_eq!(header.wavetable_cycle_size, 2048);
        assert_eq!(header.wavetable_explicitly_specifies_self, 0);
    }

    #[test]
    fn an_empty_file_is_a_zero_length_request_not_a_stall() {
        // `pending` reports (0, 0) here — the request the caller must feed an
        // empty window for. `parse_through_abi` does exactly that; a wrapper
        // that skipped it would spin until the loop bound and panic.
        assert_eq!(
            parse_through_abi(&[], false),
            Err(DELUGE_AUDIO_PARSE_TRUNCATED)
        );
    }

    #[test]
    fn a_non_audio_file_is_told_apart_from_a_broken_one() {
        assert_eq!(
            parse_through_abi(b"not an audio file at all", false),
            Err(DELUGE_AUDIO_PARSE_NOT_AUDIO)
        );
        // A real container with no chunks in it: recognised, but it says nothing.
        assert_eq!(
            parse_through_abi(&wav(&[]), false),
            Err(DELUGE_AUDIO_PARSE_MALFORMED)
        );
    }

    #[test]
    fn stopping_at_the_audio_data_skips_the_metadata_behind_it() {
        // `smpl` sits AFTER `data`, so the two modes must disagree about the
        // loop points and agree about everything else — which is exactly the
        // C++ Sample/WaveTable split this reproduces.
        let mut body = fmt_chunk(1, 44100, 16);
        body.extend(data_chunk(4096));
        body.extend(smpl_chunk(57, 1234, 5678));
        let file = wav(&body);

        let whole = parse_through_abi(&file, false).expect("a valid WAV must decode");
        let early = parse_through_abi(&file, true).expect("a valid WAV must decode");

        assert_eq!(whole.file_loop_end_samples, 5678);
        assert_eq!(early.file_loop_end_samples, 0);
        assert_eq!(
            early.audio_data_start_pos_bytes,
            whole.audio_data_start_pos_bytes
        );
        assert_eq!(early.audio_data_length_bytes, whole.audio_data_length_bytes);
        assert_eq!(early.byte_depth, whole.byte_depth);
        assert_eq!(early.num_channels, whole.num_channels);
    }

    #[test]
    fn wavetable_acceptance_crosses_as_its_own_verdict() {
        let mut body = fmt_chunk(1, 44100, 16);
        body.extend(data_chunk(4096));
        let mono = parse_through_abi(&wav(&body), true).expect("a valid WAV must decode");

        let mut cycle_size = 0u32;
        assert_eq!(
            unsafe { deluge_audio_header_accept_as_wavetable(&mono, false, &mut cycle_size) },
            DELUGE_WAVETABLE_ACCEPTED
        );
        assert_eq!(cycle_size, 2048);

        let mut body = fmt_chunk(2, 44100, 16);
        body.extend(data_chunk(4096));
        let stereo = parse_through_abi(&wav(&body), true).expect("a valid WAV must decode");
        assert_eq!(
            unsafe { deluge_audio_header_accept_as_wavetable(&stereo, true, &mut cycle_size) },
            DELUGE_WAVETABLE_REJECT_STEREO
        );
    }

    #[test]
    fn init_refuses_storage_too_small_for_the_parser() {
        let mut storage = DelugeAudioHeaderParser {
            _storage: [0; DELUGE_AUDIO_HEADER_PARSER_WORDS],
        };
        assert!(!unsafe { deluge_audio_header_parser_init(&mut storage, 8, 1024) });
        assert!(unsafe {
            deluge_audio_header_parser_init(
                &mut storage,
                size_of::<DelugeAudioHeaderParser>() as u32,
                1024,
            )
        });
    }

    #[test]
    fn a_header_survives_the_round_trip_through_c() {
        let mut body = fmt_chunk(1, 44100, 16);
        body.extend(data_chunk(4096));
        body.extend(smpl_chunk(57, 1234, 5678));
        let file = wav(&body);
        let crossed = parse_through_abi(&file, false).expect("a valid WAV must decode");
        let native = crate::parse(&file).expect("a valid WAV must decode");
        assert_eq!(AudioHeader::from(crossed), native);
        assert_eq!(DelugeAudioHeader::from(native), crossed);
    }
}
