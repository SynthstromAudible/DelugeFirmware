//! The C ABI over [`HeaderParser`] (`libdeluge/audio_format.h`), and the only module allowed
//! `unsafe`.
//!
//! It exposes the windowed protocol, not a one-shot parse: `init`, then `pending` → `feed` until
//! done, with the caller owning every read. Peak caller memory is one window of at most
//! [`MAX_WINDOW_BYTES`] bytes.
//!
//! Two rules a caller must keep, or the walk never ends:
//!
//! * A zero-length request (an empty file) is still a request: the parser only advances inside
//!   `feed`, so feed the empty window rather than skipping the call.
//! * [`DELUGE_AUDIO_PARSE_TRUNCATED`] is terminal, not "read more and retry": the parser never
//!   asks past the file size it was given.
//!
//! Everything crossing the boundary is fixed-width, and enumerations cross as `uint8_t` constants
//! rather than Rust enums: `arm-none-eabi` uses short enums, so a Rust `#[repr(u8)]` enum and a C
//! `enum` differ in width unless both are pinned.

// The crate root denies `unsafe_code`; this module is the one exception.
#![allow(unsafe_code)]

use core::mem::{align_of, size_of};
use core::slice;

use crate::wavetable::{accept_as_wavetable, Reject};
use crate::{
    AudioHeader, Container, FeedResult, HeaderError, HeaderParser, RawDataFormat, WalkGoal,
    WavetableHints, MAX_WINDOW_BYTES,
};

// ---------------------------------------------------------------------------
// Status codes
// ---------------------------------------------------------------------------
//
// Mirrored by hand from `libdeluge/types.h` and `libdeluge/audio_format.h` (this crate has no
// bindgen); the BSP's `abi::pins` checks them against the headers.

/// `libdeluge/types.h`'s `DelugeStatus`, returned by `init`.
pub type DelugeStatus = i8;
/// `DelugeStatus`'s success value.
pub const DELUGE_OK: DelugeStatus = 0;
/// `DelugeStatus`'s invalid-argument value.
pub const DELUGE_ERR_PARAM: DelugeStatus = -2;

/// `libdeluge/audio_format.h`'s `DelugeAudioParseStatus` (`uint8_t`), returned by `feed` and
/// `finish_here`.
pub type DelugeAudioParseStatus = u8;

/// `libdeluge/audio_format.h`'s `DelugeWavetableAccept` (`uint8_t`), returned by
/// `accept_as_wavetable`.
pub type DelugeWavetableAccept = u8;

/// The parser wants the window `pending` reports.
pub const DELUGE_AUDIO_PARSE_NEED: DelugeAudioParseStatus = 0;
/// The header is decoded and has been written to the caller's out-parameter.
pub const DELUGE_AUDIO_PARSE_DONE: DelugeAudioParseStatus = 1;
/// No container we recognise — not an audio file at all.
pub const DELUGE_AUDIO_PARSE_NOT_AUDIO: DelugeAudioParseStatus = 2;
/// The file contradicts itself (no `fmt `/`COMM`, no `data`/`SSND`).
pub const DELUGE_AUDIO_PARSE_MALFORMED: DelugeAudioParseStatus = 3;
/// Valid, but says something we cannot play (bit depth, format tag, channel
/// count, sample rate).
pub const DELUGE_AUDIO_PARSE_UNSUPPORTED: DelugeAudioParseStatus = 4;
/// The file ends before the header does, or a read came back short. Terminal.
pub const DELUGE_AUDIO_PARSE_TRUNCATED: DelugeAudioParseStatus = 5;
/// A null pointer was passed: a caller bug, never a property of the file.
pub const DELUGE_AUDIO_PARSE_BAD_CALL: DelugeAudioParseStatus = 6;

/// Read everything the header says. What a Sample load wants.
pub const DELUGE_AUDIO_WALK_WHOLE_HEADER: u8 = 0;
/// Read only what locates and shapes the audio. What a WaveTable load wants; see
/// [`WalkGoal::AudioOnly`].
pub const DELUGE_AUDIO_WALK_AUDIO_ONLY: u8 = 1;

/// The header may be used as a wavetable; the cycle size has been written out.
pub const DELUGE_WAVETABLE_ACCEPTED: DelugeWavetableAccept = 0;
/// More than one channel. Rejected even when the caller insists.
pub const DELUGE_WAVETABLE_REJECT_STEREO: DelugeWavetableAccept = 1;
/// Bit depth unknown, so the length-in-samples check can't be made.
pub const DELUGE_WAVETABLE_REJECT_UNKNOWN_BIT_DEPTH: DelugeWavetableAccept = 2;
/// No `clm ` tag and a length that isn't a whole number of 2048-sample cycles.
pub const DELUGE_WAVETABLE_REJECT_NOT_A_WAVETABLE: DelugeWavetableAccept = 3;
/// An AIFF container, which is only ever a wavetable if the caller insists.
pub const DELUGE_WAVETABLE_REJECT_AIFF: DelugeWavetableAccept = 4;
/// A null pointer was passed: a caller bug, never a property of the file.
pub const DELUGE_WAVETABLE_BAD_CALL: DelugeWavetableAccept = 5;

/// `DELUGE_AUDIO_MAX_WINDOW_BYTES`: the largest window a caller is ever asked for, so the
/// smallest buffer it can use. Serving more is encouraged; see [`MAX_WINDOW_BYTES`].
pub const DELUGE_AUDIO_MAX_WINDOW_BYTES: u32 = MAX_WINDOW_BYTES;

// ---------------------------------------------------------------------------
// Plain data crossing the boundary
// ---------------------------------------------------------------------------

/// `Option<f32>` for C, as an explicit flag rather than a NaN sentinel.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DelugeOptionalF32 {
    /// 0 if the file specified no value; `value` is then meaningless.
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

/// [`AudioHeader`] flattened for C: every field fixed-width, every enumeration a `uint8_t`
/// holding the Rust enum's discriminant.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DelugeAudioHeader {
    /// [`Container`]: 0 = WAV, 1 = AIFF.
    pub container: u8,
    pub num_channels: u8,
    pub byte_depth: u8,
    /// [`RawDataFormat`]; the C++ `RawDataFormat` enum has the same values.
    pub raw_data_format: u8,
    pub sample_rate: u32,
    /// Offset from the start of the file to the first audio byte.
    pub audio_data_start_pos_bytes: u32,
    pub audio_data_length_bytes: u32,
    /// Loop start in sample frames; 0 means no loop.
    pub file_loop_start_samples: u32,
    pub file_loop_end_samples: u32,
    pub midi_note: DelugeOptionalF32,
    pub wavetable_cycle_size: u32,
    /// 1 if a `clm ` tag declared the file a wavetable, else 0.
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
    /// Converts a header coming back from C (see [`deluge_audio_header_accept_as_wavetable`]).
    ///
    /// Unknown discriminants, which only a header this ABI did not produce can hold, map rather
    /// than fail: `container` to WAV (so the AIFF rejection does not fire) and `raw_data_format`
    /// to `Native` (which acceptance never reads).
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

/// One window request: `len` bytes at absolute file offset `start`, wholly inside the file.
///
/// `len` is 0 for an empty file; see the module docs.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DelugeAudioWindow {
    pub start: u32,
    pub len: u32,
}

// ---------------------------------------------------------------------------
// Caller-owned parser storage
// ---------------------------------------------------------------------------

/// Caller-owned storage for a [`HeaderParser`], typically a stack local.
///
/// An opaque blob, not a mirror of the parser's layout. `deluge_audio_header_parser_init` takes
/// the caller's `sizeof` and refuses storage too small for the parser, so the two sides cannot
/// silently drift.
#[repr(C, align(8))]
pub struct DelugeAudioHeaderParser {
    _storage: [u64; DELUGE_AUDIO_HEADER_PARSER_WORDS],
}

/// `sizeof(DelugeAudioHeaderParser)` in 64-bit words. Must equal
/// `DELUGE_AUDIO_HEADER_PARSER_WORDS` in `libdeluge/audio_format.h`; the assert below keeps it
/// large enough for the parser, and `init`'s size check catches a C side that disagrees.
pub const DELUGE_AUDIO_HEADER_PARSER_WORDS: usize = 24;

// The parser must fit its storage and need no stricter alignment. A compile-time check, so
// growing the parser past the C buffer fails the build.
const _: () = {
    assert!(size_of::<HeaderParser>() <= size_of::<DelugeAudioHeaderParser>());
    assert!(align_of::<HeaderParser>() <= align_of::<DelugeAudioHeaderParser>());
};

/// Views the caller's storage as the parser in it.
///
/// # Safety
///
/// `parser` must be non-null, aligned, and point at storage that
/// `deluge_audio_header_parser_init` wrote a `HeaderParser` into, kept alive for `'a`.
/// `HeaderParser` is plain data (no `Drop`, no pointers), so the blob stays valid.
unsafe fn as_parser<'a>(parser: *const DelugeAudioHeaderParser) -> &'a HeaderParser {
    unsafe { &*parser.cast::<HeaderParser>() }
}

/// Mutable counterpart of [`as_parser`].
///
/// # Safety
///
/// As for [`as_parser`], and the parser must not be aliased for `'a`.
unsafe fn as_parser_mut<'a>(parser: *mut DelugeAudioHeaderParser) -> &'a mut HeaderParser {
    unsafe { &mut *parser.cast::<HeaderParser>() }
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

/// Places a fresh parser for a `file_size`-byte file in the caller's storage.
///
/// `storage_bytes` is the caller's `sizeof(DelugeAudioHeaderParser)`. `file_size` must be the
/// file's real size; it bounds the walk. `goal` is a `DELUGE_AUDIO_WALK_*` constant.
///
/// Returns [`DELUGE_OK`], or [`DELUGE_ERR_PARAM`] (writing nothing) if `parser` is null, the
/// storage is too small, or `goal` is unrecognised. An unknown goal is refused rather than
/// defaulted: defaulting to the whole-header walk would hand a wavetable caller the walk that
/// reads past the audio start.
///
/// # Safety
///
/// `parser` must be null or point at `storage_bytes` writable, 8-aligned bytes.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_parser_init(
    parser: *mut DelugeAudioHeaderParser,
    storage_bytes: u32,
    file_size: u32,
    goal: u8,
) -> DelugeStatus {
    if parser.is_null() || (storage_bytes as usize) < size_of::<HeaderParser>() {
        return DELUGE_ERR_PARAM;
    }
    let goal = match goal {
        DELUGE_AUDIO_WALK_WHOLE_HEADER => WalkGoal::WholeHeader,
        DELUGE_AUDIO_WALK_AUDIO_ONLY => WalkGoal::AudioOnly,
        _ => return DELUGE_ERR_PARAM,
    };
    // SAFETY: non-null and large enough (checked above), 8-aligned per the contract. Overwriting
    // without dropping is sound: `HeaderParser` owns nothing.
    unsafe {
        parser
            .cast::<HeaderParser>()
            .write(HeaderParser::new_with_goal(file_size, goal))
    };
    DELUGE_OK
}

/// Writes the window the parser is waiting for to `out`, or returns `false` once the walk is over
/// (done or failed) or a pointer is null.
///
/// `out->len` may be 0 (an empty file): feed an empty window anyway, or the walk stalls.
///
/// # Safety
///
/// Non-null pointers must be valid: `parser` initialised by `deluge_audio_header_parser_init`,
/// `out` writable.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_parser_pending(
    parser: *const DelugeAudioHeaderParser,
    out: *mut DelugeAudioWindow,
) -> bool {
    if parser.is_null() || out.is_null() {
        return false;
    }
    // SAFETY: non-null and initialised, per the contract.
    let Some((start, len)) = (unsafe { as_parser(parser) }).pending() else {
        return false;
    };
    // SAFETY: non-null and writable, per the contract.
    unsafe { out.write(DelugeAudioWindow { start, len }) };
    true
}

/// Hands the parser `window_len` bytes read from absolute file offset `window_start`.
///
/// Returns a `DELUGE_AUDIO_PARSE_*` code. On [`DELUGE_AUDIO_PARSE_DONE`] the header is written to
/// `header_out`; on [`DELUGE_AUDIO_PARSE_NEED`] call `pending` for the next window. Every other
/// code is terminal.
///
/// Supplying more than was asked for is encouraged: the parser satisfies every request the window
/// covers before asking again. Supplying less is a failed read (the bytes exist) and ends the walk
/// with [`DELUGE_AUDIO_PARSE_TRUNCATED`]; pass the true count, never padding.
///
/// # Safety
///
/// Non-null pointers must be valid: `parser` initialised by `deluge_audio_header_parser_init`,
/// `window` readable for `window_len` bytes (it may be null only when `window_len` is 0),
/// `header_out` writable.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_parser_feed(
    parser: *mut DelugeAudioHeaderParser,
    window: *const u8,
    window_len: u32,
    window_start: u32,
    header_out: *mut DelugeAudioHeader,
) -> DelugeAudioParseStatus {
    if parser.is_null() || header_out.is_null() || (window.is_null() && window_len != 0) {
        return DELUGE_AUDIO_PARSE_BAD_CALL;
    }
    // SAFETY: readable for `window_len` bytes per the contract. The empty case is handled
    // separately because `from_raw_parts` needs a non-null pointer even for length 0.
    let bytes: &[u8] = if window_len == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(window, window_len as usize) }
    };

    // SAFETY: non-null and initialised, per the contract.
    let parser = unsafe { as_parser_mut(parser) };
    match parser.feed(bytes, window_start) {
        Ok(FeedResult::Done(header)) => {
            // SAFETY: non-null and writable, per the contract.
            unsafe { header_out.write(header.into()) };
            DELUGE_AUDIO_PARSE_DONE
        }
        Ok(FeedResult::Need { .. }) => DELUGE_AUDIO_PARSE_NEED,
        Err(error) => error_code(error),
    }
}

/// Returns whether `data`/`SSND` has been read, so the header's audio start and length are
/// final; `false` for a null `parser`.
///
/// A wavetable load then stops with `deluge_audio_header_parser_finish_here`: what follows is
/// metadata it does not use, and reading it would move the caller's reader past the audio.
///
/// # Safety
///
/// A non-null `parser` must have been initialised by `deluge_audio_header_parser_init`.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_parser_located_audio_data(
    parser: *const DelugeAudioHeaderParser,
) -> bool {
    if parser.is_null() {
        return false;
    }
    // SAFETY: non-null and initialised, per the contract.
    unsafe { as_parser(parser) }.located_audio_data()
}

/// Writes the file's container (a [`Container`] discriminant) to `out` once the first `feed` has
/// read the top header.
///
/// For verdicts that depend on the container alone; see [`HeaderParser::container`]. Returns
/// `false`, leaving `out` untouched, before the top header is read, on a walk that failed
/// without identifying a container, or for a null pointer.
///
/// # Safety
///
/// Non-null pointers must be valid: `parser` initialised by `deluge_audio_header_parser_init`,
/// `out` writable.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_parser_container(
    parser: *const DelugeAudioHeaderParser,
    out: *mut u8,
) -> bool {
    if parser.is_null() || out.is_null() {
        return false;
    }
    // SAFETY: non-null and initialised, per the contract.
    let Some(container) = (unsafe { as_parser(parser) }).container() else {
        return false;
    };
    // SAFETY: non-null and writable, per the contract.
    unsafe { out.write(container as u8) };
    true
}

/// Ends the walk now and resolves the header from what has been read so far (see
/// [`HeaderParser::finish_here`]).
///
/// Call once `deluge_audio_header_parser_located_audio_data` is true. Returns the same codes as
/// `feed` except [`DELUGE_AUDIO_PARSE_NEED`]: the walk always ends.
///
/// # Safety
///
/// Non-null pointers must be valid: `parser` initialised by `deluge_audio_header_parser_init`,
/// `header_out` writable.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_parser_finish_here(
    parser: *mut DelugeAudioHeaderParser,
    header_out: *mut DelugeAudioHeader,
) -> DelugeAudioParseStatus {
    if parser.is_null() || header_out.is_null() {
        return DELUGE_AUDIO_PARSE_BAD_CALL;
    }
    // SAFETY: non-null and initialised, per the contract.
    match unsafe { as_parser_mut(parser) }.finish_here() {
        Ok(header) => {
            // SAFETY: non-null and writable, per the contract.
            unsafe { header_out.write(header.into()) };
            DELUGE_AUDIO_PARSE_DONE
        }
        Err(error) => error_code(error),
    }
}

/// Decides whether a decoded header may be used as a wavetable (see
/// [`accept_as_wavetable`]).
///
/// `insist` is the C++ `makeWaveTableWorkAtAllCosts`: it waives the `clm `/length check and the
/// AIFF rejection, never the mono requirement. On [`DELUGE_WAVETABLE_ACCEPTED`] the cycle size is
/// written to `cycle_size_out`; otherwise nothing is written.
///
/// # Safety
///
/// Non-null pointers must be valid: `header` readable, `cycle_size_out` writable.
#[no_mangle]
pub unsafe extern "C" fn deluge_audio_header_accept_as_wavetable(
    header: *const DelugeAudioHeader,
    insist: bool,
    cycle_size_out: *mut u32,
) -> DelugeWavetableAccept {
    if header.is_null() || cycle_size_out.is_null() {
        return DELUGE_WAVETABLE_BAD_CALL;
    }
    // SAFETY: non-null and readable, per the contract.
    let header: AudioHeader = (unsafe { *header }).into();
    match accept_as_wavetable(&header, insist) {
        Ok(use_as) => {
            // SAFETY: non-null and writable, per the contract.
            unsafe { cycle_size_out.write(use_as.cycle_size) };
            DELUGE_WAVETABLE_ACCEPTED
        }
        Err(Reject::Stereo) => DELUGE_WAVETABLE_REJECT_STEREO,
        Err(Reject::UnknownBitDepth) => DELUGE_WAVETABLE_REJECT_UNKNOWN_BIT_DEPTH,
        Err(Reject::NotAWavetable) => DELUGE_WAVETABLE_REJECT_NOT_A_WAVETABLE,
        Err(Reject::AiffNotSupported) => DELUGE_WAVETABLE_REJECT_AIFF,
    }
}

/// Maps a decode error to its status code. The [`Reason`](crate::Reason) is dropped: the C++
/// caller only distinguishes corrupt from unsupported.
fn error_code(error: HeaderError) -> DelugeAudioParseStatus {
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

    /// Drives the ABI as a C caller must: ask, read, feed, repeat, feeding zero-length windows
    /// rather than skipping them.
    fn parse_through_abi(
        file: &[u8],
        stop_at_audio_data: bool,
    ) -> Result<DelugeAudioHeader, DelugeAudioParseStatus> {
        let mut storage = DelugeAudioHeaderParser {
            _storage: [0; DELUGE_AUDIO_HEADER_PARSER_WORDS],
        };
        let status = unsafe {
            deluge_audio_header_parser_init(
                &mut storage,
                size_of::<DelugeAudioHeaderParser>() as u32,
                file.len() as u32,
                if stop_at_audio_data {
                    DELUGE_AUDIO_WALK_AUDIO_ONLY
                } else {
                    DELUGE_AUDIO_WALK_WHOLE_HEADER
                },
            )
        };
        assert_eq!(
            status, DELUGE_OK,
            "storage must be big enough for the parser"
        );

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
        // Generous bound so a runaway walk fails the test instead of hanging it.
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
        // `pending` reports (0, 0); a caller that skipped feeding the empty window would spin.
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
        // `smpl` follows `data`, so the two goals must differ on loop points only.
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
    fn the_container_crosses_as_soon_as_the_top_header_is_read() {
        let mut storage = DelugeAudioHeaderParser {
            _storage: [0; DELUGE_AUDIO_HEADER_PARSER_WORDS],
        };
        let file = wav(&fmt_chunk(1, 44100, 16));
        assert_eq!(
            unsafe {
                deluge_audio_header_parser_init(
                    &mut storage,
                    size_of::<DelugeAudioHeaderParser>() as u32,
                    file.len() as u32,
                    DELUGE_AUDIO_WALK_WHOLE_HEADER,
                )
            },
            DELUGE_OK
        );

        let mut container = 0xFFu8;
        assert!(
            !unsafe { deluge_audio_header_parser_container(&storage, &mut container) },
            "nothing has been read yet"
        );

        let mut want = DelugeAudioWindow { start: 0, len: 0 };
        assert!(unsafe { deluge_audio_header_parser_pending(&storage, &mut want) });
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
        let window = &file[want.start as usize..(want.start + want.len) as usize];
        unsafe {
            deluge_audio_header_parser_feed(
                &mut storage,
                window.as_ptr(),
                window.len() as u32,
                want.start,
                &mut header,
            )
        };
        assert!(unsafe { deluge_audio_header_parser_container(&storage, &mut container) });
        assert_eq!(container, Container::Wav as u8);
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
        assert_eq!(
            unsafe {
                deluge_audio_header_parser_init(
                    &mut storage,
                    8,
                    1024,
                    DELUGE_AUDIO_WALK_WHOLE_HEADER,
                )
            },
            DELUGE_ERR_PARAM
        );
        assert_eq!(
            unsafe {
                deluge_audio_header_parser_init(
                    &mut storage,
                    size_of::<DelugeAudioHeaderParser>() as u32,
                    1024,
                    DELUGE_AUDIO_WALK_WHOLE_HEADER,
                )
            },
            DELUGE_OK
        );
    }

    #[test]
    fn an_unrecognised_goal_is_refused() {
        let mut storage = DelugeAudioHeaderParser {
            _storage: [0; DELUGE_AUDIO_HEADER_PARSER_WORDS],
        };
        let bytes = size_of::<DelugeAudioHeaderParser>() as u32;
        for goal in [DELUGE_AUDIO_WALK_WHOLE_HEADER, DELUGE_AUDIO_WALK_AUDIO_ONLY] {
            assert_eq!(
                unsafe { deluge_audio_header_parser_init(&mut storage, bytes, 1024, goal) },
                DELUGE_OK
            );
        }
        // Refused, not defaulted to the whole-header walk.
        for goal in [2u8, 0xFF] {
            assert_eq!(
                unsafe { deluge_audio_header_parser_init(&mut storage, bytes, 1024, goal) },
                DELUGE_ERR_PARAM
            );
        }
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
