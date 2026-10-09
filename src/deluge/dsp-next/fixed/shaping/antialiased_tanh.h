/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "dsp-next/fixed/shaping/tanh.h" // kTanhCentre
#include "dsp-next/fixed/shaping/tanh_table.h"
#include "util/param_functions.h" // lshiftAndSaturateUnknown

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

/// @file
/// @brief AntialiasedTanh: tanh saturation interpolated against the previous sample's position, as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief Tanh saturation, anti-aliased: each output is read from @ref kTanhTable between this sample's position on
/// the curve and the previous sample's, so a jump between samples is smoothed rather than aliased.
/// @details A sample x (Q31) at drive d sits at the position p = sat(x · 2^d) + @ref kTanhCentre, unsigned across the
/// table's 32 bits, where sat saturates to q31 (`lshiftAndSaturateUnknown`). A step reads the table at (p, p_prev),
/// sets p_prev ← p, and shifts the result down by d + 1 (@ref kLevelShift): the output's level falls as the drive
/// rises, and callers scale it back as their home needs.
///
/// **The drive's saturation.** sat saturates x to 32 − d bits before the shift for drives 1 to 20, and to 12 bits for
/// drive 0 and for drives past 20 (`lshiftAndSaturateUnknown`'s narrowest width, which a zero shift also takes). So
/// at drive 0 every position lies within 2^11 of the centre and the curve barely moves; past 20 the shift wraps. The
/// callers' drives are 1 (the high-pass ladder), 3 (the compressor), and 3 to 20 (the saturation effect).
///
/// **A drive change** between steps changes only the coefficients: the position stays, so the next step
/// interpolates from where the previous drive left it.
///
/// **Starting mid-signal.** State{} starts at the centre, as silence would leave it. @ref seed and @ref place set the
/// position instead, for a caller whose signal was already running.
struct AntialiasedTanh {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.

	/// @brief The width of a position, in bits: the table spans the whole unsigned 32-bit range on both axes.
	static constexpr int32_t kPositionBits = 32;
	/// @brief The output is shifted down by the drive plus this.
	static constexpr uint32_t kLevelShift = 1;

	/// @brief What the owner sets.
	struct Settings {
		uint32_t drive = 0; ///< The drive as a power of two: the input is scaled by 2^drive. 0 to 30: drive + 1 < 32.
	};

	/// @brief What a step reads.
	struct Coefficients {
		uint32_t drive = 0; ///< The drive as a power of two. 0 to 30.
	};

	/// @brief The previous sample's position. State{} is silence: the centre.
	struct State {
		uint32_t position = kTanhCentre; ///< The last step's position on the curve, unsigned over 32 bits.
	};

	/// @brief The coefficients for @p s: the drive, unchanged.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		return {.drive = s.drive};
	}

	/// @brief One sample through the curve, interpolated from the previous position, which it then replaces.
	/// @param c The drive.
	/// @param s The previous position, set to this sample's.
	/// @param x The input, Q31.
	/// @return The saturated sample, Q31 shifted down by drive + 1.
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& s,
	                                                     q31 x) {
		const uint32_t position = position_of(c, x);
		const q31 out =
		    bilinear_table_interpolator.interpolateSigned(position, s.position, kPositionBits, kPositionBits,
		                                                  std::span<const std::array<int16_t, 129>>{kTanhTable})
		    >> (c.drive + kLevelShift);
		s.position = position;
		return out;
	}

	/// @brief Places the position where @ref step would put it for @p sample: a caller that starts mid-signal seeds
	/// from its first sample, so that sample interpolates against itself rather than against silence.
	/// @param c The drive the position is taken at.
	/// @param s The state whose position is set.
	/// @param sample The sample, Q31.
	[[gnu::always_inline]] static void seed(const Coefficients& c, State& s, q31 sample) {
		s.position = position_of(c, sample);
	}

	/// @brief Sets a raw table position: for a caller that tracks the position itself while the curve is out of its
	/// path, so that switching it back in interpolates from there.
	/// @param s The state whose position is set.
	/// @param position The position, unsigned over 32 bits, @ref kTanhCentre for silence.
	[[gnu::always_inline]] static void place(State& s, uint32_t position) { s.position = position; }

private:
	/// @brief @p x's position at the coefficients' drive: sat(x · 2^drive) + @ref kTanhCentre, wrapping unsigned.
	[[nodiscard]] [[gnu::always_inline]] static uint32_t position_of(const Coefficients& c, q31 x) {
		return static_cast<uint32_t>(lshiftAndSaturateUnknown(x, static_cast<uint8_t>(c.drive))) + kTanhCentre;
	}
};

static_assert(processor_kernel<AntialiasedTanh>);
static_assert(!std::is_polymorphic_v<AntialiasedTanh>);

} // namespace deluge::dsp::fixed
