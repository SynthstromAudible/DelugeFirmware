/*
 * Derived from Dexed's neon_fm_kernel, under its licence:
 *
 * Copyright 2013 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
#include <cassert>
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
/// The step is one dependency chain, so `Mono` runs it three chains at a time through @ref step_chains, each stage
/// written for the three in turn (the A9 overlaps independent chains only when interleaved), and the rest of the
/// block one vector at a time.
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
		Input input = Input::NONE;      ///< NONE or BUS; a feedback op renders through Operator.
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

	/// @brief Three chains per chained step: twelve samples, as the device's NEON kernel computes them.
	static constexpr std::size_t kChains = 3;
	using Vectors = ChainVectors<Argon<q31>, kChains>; ///< One vector per chain.

	static constexpr float kPoly0 = -0.01880853017455781F; ///< The sine polynomial's u^3 coefficient (0xbc9a1459).
	static constexpr float kPoly1 = 0.25215252666796095F;  ///< Its u^2 coefficient (0x3e811a23).
	static constexpr float kPoly2 = -1.2333439964934032F;  ///< Its u coefficient (0xbf9dde37).
	static constexpr float kPoly3 = 1.0F;                  ///< Its constant (0x3f800000).

	/// @brief The routing as a mode, the rest as it is. A feedback op is the scalar Operator's, never this kernel's.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		assert(s.input != Input::FEEDBACK);
		const bool add = s.output == Output::ADD;
		const Mode mode =
		    s.input == Input::BUS ? (add ? Mode::BUS_ADD : Mode::BUS_SET) : (add ? Mode::NONE_ADD : Mode::NONE_SET);
		return {.mode = mode, .level_in = s.level_in, .freq = s.freq, .tables = s.tables};
	}

	/// @brief Starts the lanes at the held phase and gain, holds the block's end gain, and moves the held phase on by
	/// the block, as Operator's prepare does.
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const Coefficients& c, State& s, std::size_t n) {
		alignas(16) static constexpr int32_t kLanes[4]{0, 1, 2, 3}; // NOLINT(*-avoid-c-arrays): loaded without a call
		const OperatorState held = s.op;
		const Gains g = Operator::gains(
		    {.mode = Operator::Mode::MODERN_NONE_SET, .level_in = c.level_in, .freq = c.freq, .tables = c.tables}, held,
		    n);
		const Argon<int32_t> lanes = Argon<int32_t>::Load(kLanes);
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
		l.gain = in_order(l.gain + b.gain_step);
		if constexpr (M == Mode::NONE_ADD || M == Mode::BUS_ADD) {
			return y + accumulator;
		}
		return y;
	}

	/// @brief Three steps at mode @p M, their chains interleaved stage by stage: the same arithmetic as three calls
	/// to @ref step, the phase and the gain moving on in the same order, so the same result. Each stage is written for
	/// the three vectors in turn, as the device's NEON kernel does twelve samples at a time: the A9 overlaps
	/// independent chains only when their instructions are interleaved.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static Vectors step_chains(const Coefficients& /*c*/, const Block& b, Loop& l,
	                                                                const Vectors& modulation,
	                                                                const Vectors& accumulator) {
		const Argon<int32_t> mask{0x7fffff};
		const Argon<int32_t> half{0x400000};
		const Argon<int32_t> sign_bit{0x800000};
		const Argon<int32_t> p0 = l.phase;
		const Argon<int32_t> p1 = p0 + b.phase_step;
		const Argon<int32_t> p2 = p1 + b.phase_step;
		Argon<int32_t> a0 = p0;
		Argon<int32_t> a1 = p1;
		Argon<int32_t> a2 = p2;
		if constexpr (M == Mode::BUS_SET || M == Mode::BUS_ADD) {
			a0 = a0 + modulation.v[0];
			a1 = a1 + modulation.v[1];
			a2 = a2 + modulation.v[2];
		}
		const Argon<int32_t> s0 = a0.CompareTestNonzero(sign_bit).ToMask().template As<int32_t>();
		const Argon<int32_t> s1 = a1.CompareTestNonzero(sign_bit).ToMask().template As<int32_t>();
		const Argon<int32_t> s2 = a2.CompareTestNonzero(sign_bit).ToMask().template As<int32_t>();
		const Argon<float> x0 = ((a0 & mask) - half).template ConvertTo<float, 22>();
		const Argon<float> x1 = ((a1 & mask) - half).template ConvertTo<float, 22>();
		const Argon<float> x2 = ((a2 & mask) - half).template ConvertTo<float, 22>();
		const Argon<float> u0 = x0 * x0;
		const Argon<float> u1 = x1 * x1;
		const Argon<float> u2 = x2 * x2;
		Argon<float> q0 = Argon<float>{kPoly1}.MultiplyAdd(u0, kPoly0);
		Argon<float> q1 = Argon<float>{kPoly1}.MultiplyAdd(u1, kPoly0);
		Argon<float> q2 = Argon<float>{kPoly1}.MultiplyAdd(u2, kPoly0);
		q0 = Argon<float>{kPoly2}.MultiplyAdd(u0, q0);
		q1 = Argon<float>{kPoly2}.MultiplyAdd(u1, q1);
		q2 = Argon<float>{kPoly2}.MultiplyAdd(u2, q2);
		q0 = Argon<float>{kPoly3}.MultiplyAdd(u0, q0);
		q1 = Argon<float>{kPoly3}.MultiplyAdd(u1, q1);
		q2 = Argon<float>{kPoly3}.MultiplyAdd(u2, q2);
		const Argon<float> g0 = l.gain;
		const Argon<float> g1 = in_order(g0 + b.gain_step);
		const Argon<float> g2 = in_order(g1 + b.gain_step);
		Vectors y{{(q0 * g0).template ConvertTo<int32_t, 24>() ^ s0, (q1 * g1).template ConvertTo<int32_t, 24>() ^ s1,
		           (q2 * g2).template ConvertTo<int32_t, 24>() ^ s2}};
		if constexpr (M == Mode::NONE_ADD || M == Mode::BUS_ADD) {
			y.v[0] = y.v[0] + accumulator.v[0];
			y.v[1] = y.v[1] + accumulator.v[1];
			y.v[2] = y.v[2] + accumulator.v[2];
		}
		l.phase = p2 + b.phase_step;
		l.gain = in_order(g2 + b.gain_step);
		return y;
	}

	/// @brief Loads the op's held state before a block.
	[[gnu::always_inline]] static void resume(State& s, OperatorState op) { s.op = op; }

private:
	/// @brief @p v as it is, hidden from the optimiser, so a sum of it is not reassociated with what made it.
	/// @details The gain ramp adds the step once per vector, each sum rounding, and every update goes through this.
	/// Within one chained step the three sums are visible together, and an unrolled or rotated loop can show two
	/// steps' sums together; under -funsafe-math-optimizations (the device's flag) the compiler may then fold
	/// (g + d) + d into g + 2d, which rounds differently. The barrier costs nothing on ARM, where the value stays in
	/// its register.
	[[nodiscard]] [[gnu::always_inline]] static Argon<float> in_order(Argon<float> v) {
		float32x4_t r = v;
#if defined(__arm__) || defined(__aarch64__)
		asm("" : "+w"(r));
#else
		asm("" : "+m"(r));
#endif
		return r;
	}
};

static_assert(mixer_kernel<ModernVectorOperator>);
static_assert(chains_of<ModernVectorOperator> == 3);
static_assert(moded_kernel<ModernVectorOperator>);
static_assert(!std::is_polymorphic_v<ModernVectorOperator>);

} // namespace deluge::dsp::fixed::dx7
