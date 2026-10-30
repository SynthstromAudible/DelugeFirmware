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
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"

#include <cstdint>
#include <limits>
#include <type_traits>

/// @file
/// @brief BitDepth: the bitcrush's bit-depth reduction, a mask over stereo frames, as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief Masks off a frame's low bits: more of them at each of the bitcrush param's eight presets.
/// @details The param crushes from -2113929216 up; its preset is (param + 2^31) ≫ 29, 0 to 7. Alone the mask clears
/// the low 19 + preset bits; on the sample-rate reducer's grabs it clears one fewer (@ref kGrabMaskBase). Above preset
/// 4 the owner's post-FX volume is shifted down by @ref volume_shift. Stateless.
struct BitDepth {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	static constexpr q31 kOnFrom = -2113929216;   ///< The param's lowest value that crushes.
	static constexpr int kPresetShift = 29;       ///< (param + 2^31) ≫ this is the preset.
	static constexpr int kMaskBase = 19;          ///< Low bits cleared at preset 0, alone.
	static constexpr int kGrabMaskBase = 18;      ///< Low bits cleared at preset 0 on the reducer's grabs.
	static constexpr uint32_t kLoudestPreset = 4; ///< The highest preset that leaves the volume alone.

	/// @brief The param.
	struct Settings {
		q31 bitcrush = std::numeric_limits<q31>::min(); ///< The bitcrush param, Q31.
	};
	/// @brief The mask.
	struct Coefficients {
		uint32_t mask = 0xFFFFFFFFu; ///< The bits kept.
	};
	/// @brief No state: each frame's mask is its own.
	struct State {};

	/// @brief Whether @p bitcrush crushes.
	[[nodiscard]] static constexpr bool on(q31 bitcrush) { return bitcrush >= kOnFrom; }

	/// @brief The preset of @p bitcrush, 0 to 7.
	[[nodiscard]] static constexpr uint32_t preset(q31 bitcrush) {
		return static_cast<uint32_t>((static_cast<int64_t>(bitcrush) + 2147483648LL) >> kPresetShift);
	}

	/// @brief The mask for @p bitcrush at @p base low bits cleared at preset 0; every bit while off.
	[[nodiscard]] static constexpr uint32_t mask(q31 bitcrush, int base) {
		return on(bitcrush) ? 0xFFFFFFFFu << (base + static_cast<int>(preset(bitcrush))) : 0xFFFFFFFFu;
	}

	/// @brief How far the owner's post-FX volume is shifted down: preset − 4 above preset 4, else 0.
	[[nodiscard]] static constexpr int volume_shift(q31 bitcrush) {
		return on(bitcrush) && preset(bitcrush) > kLoudestPreset ? static_cast<int>(preset(bitcrush) - kLoudestPreset)
		                                                         : 0;
	}

	/// @brief The mask alone, for @p s.
	[[nodiscard]] static constexpr Coefficients coefficients(const Settings& s) {
		return {.mask = mask(s.bitcrush, kMaskBase)};
	}

	/// @brief One frame, masked.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& c, const EmptyBlock& /*b*/,
	                                                              State& /*s*/, StereoSample x) {
		return {.l = static_cast<q31>(static_cast<uint32_t>(x.l) & c.mask),
		        .r = static_cast<q31>(static_cast<uint32_t>(x.r) & c.mask)};
	}
};

static_assert(processor_kernel<BitDepth>);
static_assert(!std::is_polymorphic_v<BitDepth>);

} // namespace deluge::dsp::fixed
