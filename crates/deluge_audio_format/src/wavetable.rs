//! Wavetable *acceptance*: whether a caller may use an already-parsed
//! [`AudioHeader`] as a wavetable. Separate from parsing on purpose (see the
//! crate root docs) — this module reads no bytes, only the facts `parse`
//! already extracted, and decides a policy question over them: mono, a known
//! bit depth, and — unless the caller insists — a `clm ` tag or a
//! wavetable-looking length; AIFF is rejected outright unless insisted on.
//!
//! Mirrors the wavetable-acceptance checks the C++ parser this replaced
//! applied, plus the early AIFF rejection `parseWaveTableHeader` still runs
//! before the walk starts. This is Deluge/Serum convention with no
//! governing spec, so the C++ is the oracle: every check below, and their
//! order, matches it exactly.

use crate::{AudioHeader, Container};

/// A header cleared for use as a wavetable, and the cycle length (in samples)
/// wavetable rendering should use.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WavetableUse {
    pub cycle_size: u32,
}

/// Why a header may not be used as a wavetable.
///
/// The C++ caller (`wavetableError` in `audio_file_format.cpp`) maps each
/// variant onto an `Error` value, and that identity is user-visible:
/// `sample_browser.cpp` treats `Error::FILE_NOT_LOADABLE_AS_WAVETABLE` and
/// `Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO` as "not a
/// wavetable, fall back to loading it as a Sample" — unless the caller
/// explicitly insisted on a wavetable, has already tried more than one
/// `OscType`, or the synth is in `SynthMode::RINGMOD`, in which case
/// `sample_browser.cpp` gives up instead of falling back — and any other
/// error as "give up and show it". See each variant below for which bucket
/// it falls in; the fallback caveat above applies wherever a variant's docs
/// describe falling back to loading the file as a Sample.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Reject {
    /// More than one channel. Rejected unconditionally, even when the caller
    /// insists — the C++ parser this replaced's stereo check did not consult
    /// `makeWaveTableWorkAtAllCosts`.
    ///
    /// Maps to `Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO`, which
    /// `sample_browser.cpp` treats the same as [`NotAWavetable`] and
    /// [`AiffNotSupported`] below: fall back to loading the file as a
    /// Sample.
    ///
    /// [`NotAWavetable`]: Reject::NotAWavetable
    /// [`AiffNotSupported`]: Reject::AiffNotSupported
    Stereo,
    /// Mirrors the C++ parser this replaced's "haven't found the fmt tag
    /// yet, so we don't know the bit depth" branch. Not reachable through
    /// [`crate::parse`] — a successful parse always saw a format chunk — so
    /// this only fires on a hand-built `AudioHeader` with `byte_depth == 0`.
    /// Kept so the length-in-samples division below is guarded rather than
    /// assumed safe.
    ///
    /// Maps to `Error::FILE_UNSUPPORTED`, not one of the two
    /// wavetable-specific errors below ([`NotAWavetable`],
    /// [`AiffNotSupported`]) — a caller does not fall back to Sample for it.
    /// Moot in practice, since this variant can't be produced by a real
    /// parse.
    ///
    /// [`NotAWavetable`]: Reject::NotAWavetable
    /// [`AiffNotSupported`]: Reject::AiffNotSupported
    UnknownBitDepth,
    /// No `clm ` tag and the audio data length isn't a whole multiple of
    /// 2048 samples. Only checked when the caller isn't insisting.
    ///
    /// Maps to `Error::FILE_NOT_LOADABLE_AS_WAVETABLE`; `sample_browser.cpp`
    /// reads that as "not a wavetable, load it as a Sample instead" rather
    /// than abandoning the load.
    NotAWavetable,
    /// AIFF containers are never usable as wavetables unless the caller
    /// insists — checked before anything else, including channel count or
    /// bit depth.
    ///
    /// Maps to the same `Error::FILE_NOT_LOADABLE_AS_WAVETABLE` as
    /// [`NotAWavetable`], deliberately: to `sample_browser.cpp` an
    /// unsupported AIFF is just another reason to fall back to Sample, not a
    /// distinct case.
    ///
    /// [`NotAWavetable`]: Reject::NotAWavetable
    AiffNotSupported,
}

/// Decide whether `h` may be used as a wavetable. `insist` mirrors the C++'s
/// `makeWaveTableWorkAtAllCosts`: it waives the `clm `/length-shape check and
/// the AIFF rejection, but never the mono requirement.
pub fn accept_as_wavetable(h: &AudioHeader, insist: bool) -> Result<WavetableUse, Reject> {
    // 1. AIFF: rejected outright unless insisted on. This runs before the C++
    // even scans a chunk (see `parseWaveTableHeader`), so it takes priority
    // over every other check regardless of what the rest of the header says.
    if h.container == Container::Aiff && !insist {
        return Err(Reject::AiffNotSupported);
    }

    // 2. Bit depth must be known. See `Reject::UnknownBitDepth` above for why
    // this is unreachable via `parse`; guarding it here keeps the division
    // below panic-free regardless.
    if h.byte_depth == 0 {
        return Err(Reject::UnknownBitDepth);
    }

    // 3. Stereo files are never usable, insist or not.
    if h.num_channels != 1 {
        return Err(Reject::Stereo);
    }

    // 4. Without an explicit `clm ` tag, an insisting caller is exempt;
    // otherwise the audio data must be an exact multiple of 2048 samples.
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

    // The tests above each violate exactly one rule, so none of them can
    // notice if the check order were changed — every rejection would come
    // out the same either way. The three below each violate two rules at
    // once, so each one FAILS if its pair of checks were swapped: they pin
    // precedence, not just "a rejection happens."

    #[test]
    fn aiff_check_precedes_stereo_check() {
        // Stereo AND AIFF: the AIFF container check runs before the parser
        // would even know the channel count (`parseWaveTableHeader` rejects
        // before the chunk walk starts), so it must win over Stereo when
        // not insisting.
        let h = header(Container::Aiff, 2, 2, 2048 * 2, true);
        assert_eq!(
            accept_as_wavetable(&h, false),
            Err(Reject::AiffNotSupported),
            "AIFF must be checked, and must reject, before stereo is ever considered"
        );
        // Insisting waives the AIFF check specifically, not the stereo
        // check — so the same header now falls through to Stereo instead of
        // being accepted.
        assert_eq!(
            accept_as_wavetable(&h, true),
            Err(Reject::Stereo),
            "insist waives the AIFF check, not the (unconditional) stereo check"
        );
    }

    #[test]
    fn stereo_check_precedes_length_shape_check() {
        // Stereo AND a non-wavetable-looking length, no clm tag: the C++
        // parser this replaced checked numChannels before it ever computed
        // audioDataLengthSamples, so Stereo must win over NotAWavetable.
        let h = header(Container::Wav, 2, 2, 1234, false);
        assert_eq!(
            accept_as_wavetable(&h, false),
            Err(Reject::Stereo),
            "stereo must be checked, and must reject, before the length/clm shape check runs"
        );
    }

    #[test]
    fn bit_depth_check_precedes_stereo_check() {
        // byte_depth == 0 can't come from `parse` (see Reject::UnknownBitDepth's
        // docs), but accept_as_wavetable takes any AudioHeader, and the C++
        // parser this replaced's own first line checked byte depth before it
        // looked at numChannels. Manufacturing this header isn't testing that the
        // dead branch is reachable in practice (it still isn't) — it's the
        // only way to observe that the two checks around it run in this
        // order rather than the other.
        let mut h = header(Container::Wav, 2, 2, 2048 * 2, true);
        h.byte_depth = 0;
        assert_eq!(
            accept_as_wavetable(&h, false),
            Err(Reject::UnknownBitDepth),
            "bit depth must be checked, and must reject, before stereo is considered"
        );
    }
}
