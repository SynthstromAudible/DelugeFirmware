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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/types.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief The dry/wet blend: the arithmetic the filters' fade runs and the compressor writes out by hand, and
/// DryWetBlend, a mixer kernel running it at a level constant per block.
namespace deluge::dsp::fixed {

/// @brief dry × (1 − level) + wet × level, Q31, in the rounding the filters' fade and the compressor use.
/// @details The wet product keeps its high word truncated; the dry product is accumulated onto it rounded; the sum,
/// Q30, is doubled back to Q31, so the result's lowest bit is always clear. At @p level `Q<31>::max()` the dry level is
/// 0, but the wet product is (wet × (2^31 − 1)) ≫ 32 doubled: a negative wet sample with its lowest bit cleared, a
/// positive one 1 or 2 below itself. The blend is not an identity there, and a caller wanting the wet signal
/// unchanged skips it (DryWetBlend's FULL_WET).
///
/// **Wrapping.** The dry level `Q<31>::max()` − @p level and the doubling wrap modulo 2^32, as the target's `sub` and
/// `lsl` do: a negative level, outside the callers' range, gives the wrapped value rather than undefined behaviour.
/// @param dry The dry sample, Q31.
/// @param wet The wet sample, Q31.
/// @param level The wet level, Q31: 0 is fully dry, `Q<31>::max()` fully wet.
/// @return The blend, Q31, its lowest bit clear.
[[nodiscard]] [[gnu::always_inline]] constexpr Q<31> dry_wet_blend(Q<31> dry, Q<31> wet, Q<31> level) {
	const Q<31> dry_level = sub_wrap(Q<31>::max(), level);
	// Each product of two Q31 values is Q30
	const Q<30> sum = mla_r(mul(wet, level), dry, dry_level);
	return as_wrap<Q<31>>(sum);
}

/// @brief The dry/wet blend as a mixer kernel: inputs dry and wet, at a wet level constant per block.
/// @details BLEND runs @ref dry_wet_blend. FULL_WET returns the wet sample unchanged: the blend at `Q<31>::max()` is
/// not an identity, so the skip the compressor makes at full wet is a mode of its own, chosen by @ref coefficients at a
/// level of exactly `Q<31>::max()`. Stateless.
struct DryWetBlend {
	using value_type = Q<31>; ///< A sample.
	using step_type = Q<31>;  ///< A sample: one step per sample.

	/// @brief What a step returns.
	enum class Mode : uint8_t {
		BLEND,    ///< The blend, @ref dry_wet_blend.
		FULL_WET, ///< The wet sample, unchanged.
	};
	using Modes = ModeList<Mode::BLEND, Mode::FULL_WET>; ///< Both stateless.

	/// @brief What the owner sets.
	struct Settings {
		Q<31> level{}; ///< The wet level: 0 is fully dry, Q<31>::max() fully wet.
	};

	/// @brief What a step reads: the level and the mode the wrapper dispatches on once per block.
	struct Coefficients {
		Q<31> level{};           ///< The wet level.
		Mode mode = Mode::BLEND; ///< FULL_WET when level is Q<31>::max().
	};

	/// @brief No state: each sample's blend is its own.
	struct State {};

	/// @brief The coefficients for @p s: the level, and FULL_WET at exactly `Q<31>::max()`, BLEND at every other level.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		return {.level = s.level, .mode = s.level == Q<31>::max() ? Mode::FULL_WET : Mode::BLEND};
	}

	/// @brief One sample: the blend of @p dry and @p wet, or @p wet unchanged in FULL_WET.
	/// @tparam M The block's mode.
	/// @param c The level.
	/// @param dry The dry sample, Q31.
	/// @param wet The wet sample, Q31.
	/// @return dry_wet_blend(dry, wet, level) (BLEND) or @p wet (FULL_WET), Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static Q<31> step(const Coefficients& c, const EmptyBlock& /*b*/, State& /*s*/,
	                                                       Q<31> dry, Q<31> wet) {
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
