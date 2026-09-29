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

/// @brief The shared windowed-sinc interpolation kernel — one table, two ways to pick a variant.
///
/// Reconstructing a signal at an arbitrary sub-sample phase (resampling a wavetable cycle, or
/// playing a sample back off-pitch) is a band-limited interpolation: each output sample is a
/// windowed-sinc-weighted sum of @ref kSincKernelTaps neighbouring stored samples. The kernel comes
/// in @ref kSincKernelVariants sharpness variants — the further the play rate sits *below* the
/// Nyquist limit, the more harmonics are safely below Nyquist, so a sharper (less rolled-off)
/// variant is used. Each variant is a table of @ref kSincKernelPhaseSteps rows by @ref
/// kSincKernelTaps int16 taps; a consumer offsets into the variant by the current sub-sample phase
/// and blends toward the next row, then applies it with @ref windowed_sinc_apply.
///
/// This is the single source of the table for **both** the wavetable renderer (which picks the
/// variant from a band's oversampling ratio via @ref select) and sample playback / live pitch-shift
/// (which pick it from the absolute phase increment, the legacy `getWhichKernel` index, via
/// @ref variant). The table values are pinned end-to-end by the wavetable/ and sample/ golden
/// fixtures; @c windowed_sinc_kernel_spec pins the two variant picks.
inline constexpr int32_t kSincKernelVariants = 7;    ///< Oversampling-ratio sharpness variants.
inline constexpr int32_t kSincKernelPhaseSteps = 17; ///< 16 sub-sample phase rows + 1 wrap row.
inline constexpr int32_t kSincKernelTaps = 16;       ///< Taps per sample (== kInterpolationMaxNumSamples).

class WindowedSincKernel {
public:
	/// @brief Pick the kernel variant for a play rate against the chosen band's pitch limit
	/// (the wavetable renderer's band-limited selection).
	///
	/// Walks down an octave at a time while the (halving) rate still meets the band limit, then
	/// nudges to the adjacent half-octave variant past the √½ midpoint — the exact legacy
	/// `getKernel` ladder.
	///
	/// @param phase_increment the oscillator's per-sample phase step, in the same units as
	/// @ref deluge::dsp::Phasor "Phasor"'s increment: **UQ0.32** (unsigned 0.32 fixed-point, where
	/// the full 2^32 range is one cycle), i.e. `frequency * 2^32 / sampleRate`. So
	/// `phase_increment / 2^32` is cycles advanced per sample. Higher ⇒ higher pitch ⇒ more aliasing
	/// risk, which is what drives the variant choice.
	/// @param band_max_phase_increment the highest @p phase_increment the chosen band is
	/// band-limited for — same UQ0.32 units. The ratio `phase_increment / band_max_phase_increment`
	/// is the oversampling factor the ladder keys off.
	///
	/// @note Both are genuinely unsigned (UQ0.32). The legacy `getKernel` declared them `int32_t`,
	/// and that signed reading is *not* cosmetic for the band limit: a band whose limit has the top
	/// bit set (≥ 2^31, i.e. above Nyquist) reads as negative at the √½ midpoint test, which then
	/// always fires — so that band always takes the sharper variant. The implementation keeps the
	/// honest `uint32_t` type and reproduces that one narrowing at exactly that comparison, so the
	/// pick is bit-identical.
	///
	/// @returns the base of the selected variant's `[kSincKernelPhaseSteps][kSincKernelTaps]` block;
	/// the consumer offsets into it by sub-sample phase.
	[[nodiscard]] const int16_t* select(uint32_t phase_increment, uint32_t band_max_phase_increment) const;

	/// @brief The base of a variant by index (sample playback's `getWhichKernel`-selected variant).
	///
	/// @param which the variant index in `[0, kSincKernelVariants)` — sample playback derives it
	/// directly from the absolute phase increment (`getWhichKernel`) rather than a band ratio.
	/// @returns the base of that variant's `[kSincKernelPhaseSteps][kSincKernelTaps]` block; the
	/// consumer offsets in by `phaseStep * kSincKernelTaps` to reach the row.
	[[nodiscard]] const int16_t* variant(int32_t which) const;
};

/// The single shared windowed-sinc kernel table (stateless; both DSP consumers use this instance).
inline constexpr WindowedSincKernel windowed_sinc_kernel{};

} // namespace deluge::dsp::fixed
