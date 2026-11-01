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
#include "dsp-next/core/mode.h"
#include "dsp-next/core/q31_ops.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief The dry/wet blend: the arithmetic the filters' fade runs and the compressor writes out by hand, and
/// DryWetBlend, a mixer kernel running it at a level constant per block.
namespace deluge::dsp::fixed {

/// @brief The shift from the blend's rounded high word, Q30 (each product keeps the high word of a Q31 × Q31), back to
/// Q31.
inline constexpr int kDryWetBlendToQ31Shift = 1;

/// @brief dry × (1 − level) + wet × level, Q31, in the rounding the filters' fade and the compressor use.
/// @details The wet product keeps its high word truncated; the dry product is accumulated onto it rounded; the sum,
/// Q30, is doubled back to Q31, so the result's lowest bit is always clear. At @p level kOneQ31 the dry level is 0,
/// but the wet product is (wet × (2^31 − 1)) ≫ 32 doubled: a negative wet sample with its lowest bit cleared, a
/// positive one 1 or 2 below itself. The blend is not an identity there, and a caller wanting the wet signal
/// unchanged skips it (DryWetBlend's FULL_WET).
///
/// **Wrapping.** The dry level kOneQ31 − @p level and the doubling wrap modulo 2^32, as the target's `sub` and `lsl`
/// do: a negative level, outside the callers' range, gives the wrapped value rather than undefined behaviour.
/// @param dry The dry sample, Q31.
/// @param wet The wet sample, Q31.
/// @param level The wet level, Q31: 0 is fully dry, kOneQ31 fully wet.
/// @return The blend, Q31, its lowest bit clear.
[[nodiscard]] [[gnu::always_inline]] constexpr q31 dry_wet_blend(q31 dry, q31 wet, q31 level) {
	const q31 dry_level = wrapping_sub(kOneQ31, level);
	const q31 sum = multiply_accumulate_32x32_rshift32_rounded(multiply_32x32_rshift32(wet, level), dry, dry_level);
	return sum << kDryWetBlendToQ31Shift;
}

/// @brief The dry/wet blend as a mixer kernel: inputs dry and wet, at a wet level constant per block.
/// @details BLEND runs @ref dry_wet_blend. FULL_WET returns the wet sample unchanged: the blend at kOneQ31 is not an
/// identity, so the skip the compressor makes at full wet is a mode of its own, chosen by @ref coefficients at a level
/// of exactly kOneQ31. Stateless.
struct DryWetBlend {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.

	/// @brief What a step returns.
	enum class Mode : uint8_t {
		BLEND,    ///< The blend, @ref dry_wet_blend.
		FULL_WET, ///< The wet sample, unchanged.
	};
	using Modes = ModeList<Mode::BLEND, Mode::FULL_WET>; ///< Both stateless.

	/// @brief What the owner sets.
	struct Settings {
		q31 level = 0; ///< The wet level, Q31: 0 is fully dry, kOneQ31 fully wet.
	};

	/// @brief What a step reads: the level and the mode the wrapper dispatches on once per block.
	struct Coefficients {
		q31 level = 0;           ///< The wet level, Q31.
		Mode mode = Mode::BLEND; ///< FULL_WET when level == kOneQ31.
	};

	/// @brief No state: each sample's blend is its own.
	struct State {};

	/// @brief The coefficients for @p s: the level, and FULL_WET at exactly kOneQ31, BLEND at every other level.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		return {.level = s.level, .mode = s.level == kOneQ31 ? Mode::FULL_WET : Mode::BLEND};
	}

	/// @brief One sample: the blend of @p dry and @p wet, or @p wet unchanged in FULL_WET.
	/// @tparam M The block's mode.
	/// @param c The level.
	/// @param dry The dry sample, Q31.
	/// @param wet The wet sample, Q31.
	/// @return dry_wet_blend(dry, wet, level) (BLEND) or @p wet (FULL_WET), Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& /*s*/,
	                                                     q31 dry, q31 wet) {
		if constexpr (M == Mode::FULL_WET) {
			return wet;
		}
		else {
			return dry_wet_blend(dry, wet, c.level);
		}
	}
};

static_assert(mixer_kernel<DryWetBlend>);
static_assert(!std::is_polymorphic_v<DryWetBlend>);

} // namespace deluge::dsp::fixed
