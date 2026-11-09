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

#include "dsp-next/core/pack_f32.h"
#include "dsp-next/core/sample.h"
#include "dsp-next/float/math/detail.h"

#include <algorithm>
#include <cstdint>

/// @file
/// @brief fp::Drift: per-voice slow noise at block rate, for wandering a voice's cutoff and pitch.
namespace deluge::dsp::fp {

/// @brief A seeded noise stream smoothed by a one-pole, stepped once per block: one per voice.
/// @details Each @ref next draws a uniform u ∈ [−1, 1] from a 32-bit xorshift (shifts 13, 17, 5) and moves the value
/// a fraction a of the way towards it, a = 1 − e^(−1/time_blocks): a one-pole on white noise, its time constant
/// time_blocks blocks. Deterministic and libm-free, so a seed gives the same stream on every target and fusion
/// policy aside from @ref mul_add. The owner applies the value, linearised: a cutoff fc·(1 + d·depth), a phase
/// increment inc·(1 + d·cents·ln2/1200).
class Drift {
public:
	/// @brief What the owner sets.
	struct Settings {
		f32 time_blocks = 64.F; ///< The smoothing's time constant in blocks, at least 1; NaN takes 1.
		uint32_t seed = 1;      ///< The stream's seed; 0 is taken as 1 (xorshift's one fixed point).
	};

	/// @brief Sets the time constant and restarts the stream from @p settings.seed, the value at 0.
	void configure(const Settings& settings) {
		const f32 time = settings.time_blocks >= 1.F ? settings.time_blocks : 1.F;
		a_ = static_cast<f32>(1.0 - detail::exp_negative(1.0 / static_cast<double>(time)));
		reseed(settings.seed);
	}

	/// @brief Restarts the stream from @p seed (0 taken as 1), the value at 0; the time constant is kept.
	void reseed(uint32_t seed) {
		x_ = seed == 0 ? 1U : seed;
		value_ = 0.F;
	}

	/// @brief Steps one block: returns the new value, in [−1, 1].
	f32 next() {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		x_ ^= x_ << 13U;
		x_ ^= x_ >> 17U;
		x_ ^= x_ << 5U;
		const f32 u = static_cast<f32>(static_cast<int32_t>(x_)) * 0x1p-31F;
		value_ = mul_add(value_, a_, u - value_);
		return value_;
	}

	/// @brief The value the last @ref next returned (0 after configure or reseed).
	[[nodiscard]] f32 value() const { return value_; }

	/// @brief The smoothing coefficient a = 1 − e^(−1/time_blocks), rounded once.
	[[nodiscard]] f32 coefficient() const { return a_; }

private:
	f32 a_ = static_cast<f32>(1.0 - detail::exp_negative(1.0 / 64.0));
	uint32_t x_ = 1;
	f32 value_ = 0.F;
};

} // namespace deluge::dsp::fp
