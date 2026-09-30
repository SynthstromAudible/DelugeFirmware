//! Wavetable acceptance: whether a decoded [`AudioHeader`] may be used as a wavetable.
//!
//! A policy decision over the header's facts, reading no bytes. Requires mono and a known bit
//! depth; unless the caller insists, also a `clm ` tag or a length that is a whole number of
//! 2048-sample cycles, and a WAV container. This is Deluge/Serum convention with no governing
//! spec: the checks and their order match the original C++ wavetable loader, and the order is
//! observable through which [`Reject`] a file gets.

use crate::{AudioHeader, Container};

/// A header cleared for use as a wavetable.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WavetableUse {
    /// Samples per cycle for wavetable rendering.
    pub cycle_size: u32,
}

/// Why a header may not be used as a wavetable.
///
/// The C++ side (`wavetableError` in `audio_file_format.cpp`) maps each variant to an `Error`,
/// and the mapping is user-visible: on `FILE_NOT_LOADABLE_AS_WAVETABLE[_BECAUSE_STEREO]` the
/// sample browser (`sample_claim.cpp`) falls back to loading the file as a Sample, unless the user
/// insisted on a wavetable, more than one `OscType` has been tried, or the synth is in
/// `SynthMode::RINGMOD`. Any other error aborts the load.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Reject {
    /// More than one channel. Rejected even when the caller insists. Maps to
    /// `FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO` (falls back to Sample).
    Stereo,
    /// `byte_depth == 0`. Unreachable through [`crate::parse`], which always sees a format chunk;
    /// only a hand-built header gets here. Guards the length division. Maps to `FILE_UNSUPPORTED`
    /// (no fallback).
    UnknownBitDepth,
    /// No `clm ` tag and the audio length is not a whole multiple of 2048 samples. Checked only
    /// when the caller does not insist. Maps to `FILE_NOT_LOADABLE_AS_WAVETABLE` (falls back to
    /// Sample).
    NotAWavetable,
    /// An AIFF container, unless the caller insists. Checked before everything else. Maps to
    /// `FILE_NOT_LOADABLE_AS_WAVETABLE`, like [`Reject::NotAWavetable`], so it falls back to
    /// Sample.
    AiffNotSupported,
}

/// Decides whether `h` may be used as a wavetable.
///
/// `insist` (the C++ `makeWaveTableWorkAtAllCosts`) waives the `clm `/length check and the AIFF
/// rejection, never the mono requirement.
///
/// # Errors
///
/// The first [`Reject`] that applies, checked in the order AIFF, bit depth, channels, length.
pub fn accept_as_wavetable(h: &AudioHeader, insist: bool) -> Result<WavetableUse, Reject> {
    // AIFF first: the C++ loader rejects it before reading any chunk.
    if h.container == Container::Aiff && !insist {
        return Err(Reject::AiffNotSupported);
    }

    // Unreachable via `parse`; keeps the division below panic-free.
    if h.byte_depth == 0 {
        return Err(Reject::UnknownBitDepth);
    }

    if h.num_channels != 1 {
        return Err(Reject::Stereo);
    }

    // Without a `clm ` tag, the audio must be whole 2048-sample cycles unless the caller insists.
    if !h.wavetable.explicitly_specifies_self && !insist {
        let length_samples = h.audio_data_length_bytes / u32::from(h.byte_depth);
        if !length_samples.is_multiple_of(2048) {
            return Err(Reject::NotAWavetable);
        }
    }

    Ok(WavetableUse {
        cycle_size: h.wavetable.cycle_size,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::*;

    fn header(container: Container, channels: u8, depth: u8, len: u32, clm: bool) -> AudioHeader {
        AudioHeader {
            container,
            num_channels: channels,
            byte_depth: depth,
            raw_data_format: RawDataFormat::Native,
            sample_rate: 44100,
            audio_data_start_pos_bytes: 44,
            audio_data_length_bytes: len,
            file_loop_start_samples: 0,
            file_loop_end_samples: 0,
            midi_note: None,
            wavetable: WavetableHints {
                cycle_size: 2048,
                explicitly_specifies_self: clm,
            },
        }
    }

    #[test]
    fn stereo_is_never_a_wavetable() {
        let h = header(Container::Wav, 2, 2, 2048 * 2, true);
        assert_eq!(accept_as_wavetable(&h, false), Err(Reject::Stereo));
        assert_eq!(accept_as_wavetable(&h, true), Err(Reject::Stereo));
    }

    #[test]
    fn clm_tag_accepts_regardless_of_length() {
        let h = header(Container::Wav, 1, 2, 1234, true);
        assert_eq!(
            accept_as_wavetable(&h, false),
            Ok(WavetableUse { cycle_size: 2048 })
        );
    }

    #[test]
    fn without_clm_length_must_look_like_a_wavetable() {
        // 2048-sample multiple at 2 bytes/sample = accepted.
        let ok = header(Container::Wav, 1, 2, 2048 * 2, false);
        assert_eq!(
            accept_as_wavetable(&ok, false),
            Ok(WavetableUse { cycle_size: 2048 })
        );
        // Not a multiple of 2048 samples = rejected unless insisted on.
        let bad = header(Container::Wav, 1, 2, 1234, false);
        assert_eq!(accept_as_wavetable(&bad, false), Err(Reject::NotAWavetable));
        assert_eq!(
            accept_as_wavetable(&bad, true),
            Ok(WavetableUse { cycle_size: 2048 })
        );
    }

    #[test]
    fn aiff_rejected_unless_insisted_on() {
        let h = header(Container::Aiff, 1, 2, 2048 * 2, true);
        assert_eq!(
            accept_as_wavetable(&h, false),
            Err(Reject::AiffNotSupported)
        );
        assert_eq!(
            accept_as_wavetable(&h, true),
            Ok(WavetableUse { cycle_size: 2048 })
        );
    }

    // The tests below each break two rules at once, so each fails if its pair of checks were
    // swapped: they pin check order.

    #[test]
    fn aiff_check_precedes_stereo_check() {
        // Stereo and AIFF: without insisting, AIFF wins.
        let h = header(Container::Aiff, 2, 2, 2048 * 2, true);
        assert_eq!(
            accept_as_wavetable(&h, false),
            Err(Reject::AiffNotSupported),
            "AIFF must be checked, and must reject, before stereo is ever considered"
        );
        // Insisting waives the AIFF check but not the stereo one.
        assert_eq!(
            accept_as_wavetable(&h, true),
            Err(Reject::Stereo),
            "insist waives the AIFF check, not the (unconditional) stereo check"
        );
    }

    #[test]
    fn stereo_check_precedes_length_shape_check() {
        // Stereo and a non-wavetable length, no clm tag: Stereo wins.
        let h = header(Container::Wav, 2, 2, 1234, false);
        assert_eq!(
            accept_as_wavetable(&h, false),
            Err(Reject::Stereo),
            "stereo must be checked, and must reject, before the length/clm shape check runs"
        );
    }

    #[test]
    fn bit_depth_check_precedes_stereo_check() {
        // Unknown bit depth and stereo: bit depth wins. Such a header cannot come from `parse`;
        // it is built by hand only to observe the order.
        let mut h = header(Container::Wav, 2, 2, 2048 * 2, true);
        h.byte_depth = 0;
        assert_eq!(
            accept_as_wavetable(&h, false),
            Err(Reject::UnknownBitDepth),
            "bit depth must be checked, and must reject, before stereo is considered"
        );
    }
}
