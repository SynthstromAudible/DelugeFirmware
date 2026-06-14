/*
 * Copyright © 2021-2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief Windowed-sinc interpolation kernels for the wavetable renderer.
///
/// Reconstructing a wavetable cycle at an arbitrary phase is a band-limited resample, so each
/// output sample is a windowed-sinc-weighted sum of @ref kSincKernelTaps neighbouring stored
/// samples. The kernel comes in @ref kSincKernelVariants sharpness variants: the further the play
/// rate (phase increment) sits *below* the chosen band's limit, the more of that band's harmonics
/// are safely below Nyquist, so a sharper (less rolled-off) kernel is used. Each variant is a
/// table of @ref kSincKernelPhaseSteps rows by @ref kSincKernelTaps int16 taps; the renderer reads
/// the row for the current sub-sample phase (and lerps toward the next).
///
/// This is the dsp-next-resident, self-contained kernel for the band-data-ported wavetable
/// renderer — a private copy of the legacy `dsp/interpolate` `windowedSincKernel` (kept for sample
/// playback / live pitch-shift, which interpolate differently). A @c sinc_kernel_spec asserts the
/// copy and the variant pick are bit-identical to the legacy table + `getKernel`.
inline constexpr int32_t kSincKernelVariants = 7;    ///< Oversampling-ratio sharpness variants.
inline constexpr int32_t kSincKernelPhaseSteps = 17; ///< 16 sub-sample phase rows + 1 wrap row.
inline constexpr int32_t kSincKernelTaps = 16;       ///< Taps per sample (== kInterpolationMaxNumSamples).

/// @brief Pick the kernel variant for a play rate against the chosen band's pitch limit.
///
/// Walks down an octave at a time while the (halving) rate still meets the band limit, then nudges
/// to the adjacent half-octave variant past the √½ midpoint — the exact legacy `getKernel` ladder.
///
/// @param phase_increment the oscillator's per-sample phase step, in the same units as
/// @ref deluge::dsp::Phasor "Phasor"'s increment: **UQ0.32** (unsigned 0.32 fixed-point,
/// where the full 2^32 range is one cycle), i.e. `frequency * 2^32 / sampleRate`. So
/// `phase_increment / 2^32` is cycles advanced per sample (the normalised frequency). Higher value
/// ⇒ higher pitch ⇒ more aliasing risk, which is what drives the kernel choice.
/// @param band_max_phase_increment the highest @p phase_increment the chosen band is band-limited
/// for — same UQ0.32 units. The ratio `phase_increment / band_max_phase_increment` is the
/// oversampling factor the ladder keys off.
///
/// @note Both are genuinely unsigned (UQ0.32). The legacy `getKernel` declared them `int32_t`, and
/// that signed reading is *not* cosmetic for the band limit: a band whose limit has the top bit set
/// (≥ 2^31, i.e. above Nyquist) reads as negative at the √½ midpoint test, which then always fires —
/// so that band always takes the sharper variant. The implementation keeps the honest `uint32_t`
/// type and reproduces that one narrowing at exactly that comparison, so the pick is bit-identical.
///
/// @returns the base of the selected variant's `[kSincKernelPhaseSteps][kSincKernelTaps]` block;
/// the renderer offsets into it by sub-sample phase.
[[nodiscard]] const int16_t* select_sinc_kernel(uint32_t phase_increment, uint32_t band_max_phase_increment);

} // namespace deluge::dsp::fixed
