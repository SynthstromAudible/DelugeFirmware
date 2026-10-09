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

#include "dsp-next/core/types.h"
#include "dsp-next/fixed/mix/dry_wet.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <cstdint>
#include <span>

namespace deluge::dsp::fixed {

/// @brief The scratch a filter needs while it fades between dry and wet: the caller's, at least the block's samples
/// (two per frame in stereo).
using FadeScratch = std::span<q31>;

/// @brief The dry/wet fade a filter runs after being switched on from off: over about 500 samples the output moves
/// from the dry input to the filtered signal.
///
/// The dry level is a float stepped by ×0.99 per sample and the fade ends once it falls below 0.001 (a double
/// comparison); the wet level is derived from it at Q31 each step.
class DryWetFade {
public:
	/// @brief Starts fading in from dry. A fade already under way restarts; with no call, any fade carries on.
	void begin() {
		dry_ = 1;
		wet_ = 0;
	}

	/// @brief Whether a fade is under way: dry not below 0.001 (the float compared as a double).
	[[nodiscard]] bool active() const { return !(dry_ < 0.001); }

	/// @brief Copies the dry @p block into @p scratch before the filter runs over it.
	void keepDry(std::span<const q31> block, FadeScratch scratch) const { std::ranges::copy(block, scratch.begin()); }

	/// @brief Blends the filtered @p block with the dry copy in @p scratch by @ref dry_wet_blend at the wet level,
	/// stepping the fade once per sample (each channel's sample in an interleaved stereo block steps it).
	void blend(std::span<q31> block, FadeScratch scratch) {
		for (std::size_t i = 0; i < block.size(); i++) {
			block[i] = dry_wet_blend(scratch[i], block[i], wet_);
			step();
		}
	}

	/// @brief The state after the filter set's zeroing reset: no fade (dry 0) and the wet level 0.
	void zero() {
		dry_ = 0;
		wet_ = 0;
	}

private:
	/// @brief One sample's step: dry × 0.99 (in double, stored as float), wet = ONE_Q31 × (1 - dry) in float.
	void step() {
		dry_ = static_cast<float>(dry_ * 0.99);
		wet_ = static_cast<q31>(static_cast<float>(ONE_Q31) * (1.0F - dry_));
	}

	/// The dry level, 1 at the start of a fade.
	float dry_{1.0F};
	/// The wet level, Q31.
	q31 wet_{ONE_Q31};
};

} // namespace deluge::dsp::fixed
