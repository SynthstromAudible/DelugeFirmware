// Based on Dexed's NEON FM operator kernel (Copyright 2013 Google Inc., Apache License 2.0).

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
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/dx7/operator.h"
#include "dsp-next/fixed/dx7/tables.h"

#include <argon.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief The modern operator four samples at a time, by the device's polynomial sine, in Argon.
namespace deluge::dsp::fixed::dx7 {

/// @brief A modern operator with no feedback, four samples per step: the device's NEON operator, in Argon.
/// @details What a step computes, lane by lane (lane i of a block's first step is sample i):
/// 1. **Phase.** Integer and wrapping: sample i is at phase0 + i·freq; a step moves every lane on by freq << 2.
/// 2. **Gain.** Float: sample i is at float(gain1 + i·dgain) · 2^-24 (the 32-bit sum wraps, converts rounding to
///    nearest, then scales exactly), and a step adds float(dgain) · 2^-22 with one float add, so its rounding
///    accumulates. Sample 0 is at gain1, one sample behind the scalar operator's pre-incremented ramp.
/// 3. **Position.** x = ((phase + modulation) & 0x7fffff) − 0x400000, converted with 22 fraction bits: the
///    position in the half cycle, in [-1, 1), exactly. Bit 23 of phase + modulation is the half cycle's sign.
/// 4. **Sine.** u = x·x, then p = c3 + u·(c2 + u·(c1 + u·c0)) as three multiply-adds that are not fused: each
///    product rounds, then each sum rounds, as the A9's NEON computes them (it has no fused multiply-add).
/// 5. **Scale and convert.** p·gain, then to an integer with 24 fraction bits: × 2^24, truncated toward zero,
///    saturated, NaN to 0.
/// 6. **Sign.** Where bit 23 was set, the result is inverted bitwise: −y − 1, not −y, so zero becomes −1.
/// 7. **Mix.** The accumulator is added (ADD) with wrapping, or not (SET). With no input the modulation is zero.
///
/// It is not the scalar operator: the sine is a polynomial, not the table, so samples differ in their low bits.
/// No denormal arises, so flushing them (NEON) or not (SSE) changes nothing: x is 0 or at least 2^-22, u at least
/// 2^-44, the gain a multiple of 2^-24 or 0, and every product stays above 2^-126.
struct ModernVectorOperator {
	using value_type = q31;       ///< A sample, Q24.
	using step_type = Argon<q31>; ///< Four samples per step.

	/// @brief The input and output, one mode each.
	enum class Mode : uint8_t { NONE_SET, NONE_ADD, BUS_SET, BUS_ADD };
	using Modes = ModeList<Mode::NONE_SET, Mode::NONE_ADD, Mode::BUS_SET, Mode::BUS_ADD>;

	/// @brief The op's routing and its block's level and pitch.
	struct Settings {
		Input input = Input::NONE;      ///< NONE or BUS; feedback renders through Operator.
		Output output = Output::SET;    ///< Set or add.
		int32_t level_in = 0;           ///< The level, Q24 doublings.
		int32_t freq = 0;               ///< The phase increment, 2^24 per cycle.
		const Tables* tables = nullptr; ///< The engine's tables, for the gain's exp2.
	};

	/// @brief Settings with the routing folded into the mode.
	struct Coefficients {
		Mode mode = Mode::NONE_SET;     ///< The input and output.
		int32_t level_in = 0;           ///< As Settings::level_in.
		int32_t freq = 0;               ///< As Settings::freq.
		const Tables* tables = nullptr; ///< As Settings::tables.
	};

	/// @brief What the loop steps: four lanes of phase and gain.
	struct Loop {
		Argon<int32_t> phase{}; ///< Each lane's phase.
		Argon<float> gain{};    ///< Each lane's gain, 1.0 = 2^24.
	};

	/// @brief The op between blocks, and the loop within one.
	struct State {
		OperatorState op{}; ///< The held phase and gain, shared with Operator.
		Loop loop{};        ///< The loop's state.
	};
	static constexpr auto kLoopState = &State::loop; ///< The loop steps Loop only.

	/// @brief What a step adds to every lane.
	struct Block {
		Argon<int32_t> phase_step{}; ///< freq << 2, wrapping.
		Argon<float> gain_step{};    ///< float(dgain) · 2^-22: four samples' gain.
	};

	static constexpr float kPoly0 = -0.01880853017455781F; ///< The sine polynomial's u^3 coefficient (0xbc9a1459).
	static constexpr float kPoly1 = 0.25215252666796095F;  ///< Its u^2 coefficient (0x3e811a23).
	static constexpr float kPoly2 = -1.2333439964934032F;  ///< Its u coefficient (0xbf9dde37).
	static constexpr float kPoly3 = 1.0F;                  ///< Its constant (0x3f800000).

	/// @brief The routing as a mode, the rest as it is.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		const bool add = s.output == Output::ADD;
		const Mode mode =
		    s.input == Input::BUS ? (add ? Mode::BUS_ADD : Mode::BUS_SET) : (add ? Mode::NONE_ADD : Mode::NONE_SET);
		return {.mode = mode, .level_in = s.level_in, .freq = s.freq, .tables = s.tables};
	}

	/// @brief Starts the lanes at the held phase and gain, holds the block's end gain, and moves the held phase on by
	/// the block, as Operator's prepare does.
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const Coefficients& c, State& s, std::size_t n) {
		static constexpr std::array<int32_t, 4> kLanes{0, 1, 2, 3};
		const OperatorState held = s.op;
		const Gains g = Operator::gains(
		    {.mode = Operator::Mode::MODERN_NONE_SET, .level_in = c.level_in, .freq = c.freq, .tables = c.tables}, held,
		    n);
		const Argon<int32_t> lanes = Argon<int32_t>::Load(kLanes.data());
		s.loop.phase = Argon<int32_t>{held.phase}.MultiplyAdd(lanes, Argon<int32_t>{c.freq});
		s.loop.gain = Argon<int32_t>{g.gain1}.MultiplyAdd(lanes, Argon<int32_t>{g.dgain}).ConvertTo<float, 24>();
		s.op.gain_out = g.gain2;
		s.op.phase = Operator::advance_phase(held.phase, c.freq, n);
		return {.phase_step = Argon<int32_t>{c.freq}.ShiftLeft(2),
		        .gain_step = Argon<int32_t>{g.dgain}.ConvertTo<float, 22>()};
	}

	/// @brief Four samples at mode @p M.
	/// @param modulation The modulation bus's samples; read in the BUS modes only.
	/// @param accumulator What the output holds; read in the ADD modes only.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const Block& b, Loop& l,
	                                                            Argon<q31> modulation, Argon<q31> accumulator) {
		Argon<int32_t> at = l.phase;
		if constexpr (M == Mode::BUS_SET || M == Mode::BUS_ADD) {
			at = at + modulation;
		}
		const Argon<int32_t> sign = at.CompareTestNonzero(Argon<int32_t>{0x800000}).ToMask().template As<int32_t>();
		const Argon<float> x =
		    ((at & Argon<int32_t>{0x7fffff}) - Argon<int32_t>{0x400000}).template ConvertTo<float, 22>();
		const Argon<float> u = x * x;
		Argon<float> p = Argon<float>{kPoly1}.MultiplyAdd(u, kPoly0);
		p = Argon<float>{kPoly2}.MultiplyAdd(u, p);
		p = Argon<float>{kPoly3}.MultiplyAdd(u, p);
		const Argon<int32_t> y = (p * l.gain).template ConvertTo<int32_t, 24>() ^ sign;
		l.phase = l.phase + b.phase_step;
		l.gain = l.gain + b.gain_step;
		if constexpr (M == Mode::NONE_ADD || M == Mode::BUS_ADD) {
			return y + accumulator;
		}
		return y;
	}

	/// @brief Loads the op's held state before a block.
	[[gnu::always_inline]] static void resume(State& s, OperatorState op) { s.op = op; }
};

static_assert(mixer_kernel<ModernVectorOperator>);
static_assert(moded_kernel<ModernVectorOperator>);
static_assert(!std::is_polymorphic_v<ModernVectorOperator>);

} // namespace deluge::dsp::fixed::dx7
