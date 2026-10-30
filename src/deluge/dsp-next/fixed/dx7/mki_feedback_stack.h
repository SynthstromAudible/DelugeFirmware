/*
 * Derived from Dexed's EngineMkI, under its licence:
 *
 * Copyright (C) 2015-2017 Pascal Gauthier.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 * The code is based on ppplay https://github.com/stohrendorf/ppplay and opl3
 * math documentation :
 * https://github.com/gtaylormb/opl3_fpga/blob/master/docs/opl3math/opl3math.pdf
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
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/dx7/algorithms.h"
#include "dsp-next/fixed/dx7/lookup.h"
#include "dsp-next/fixed/dx7/operator.h"
#include "dsp-next/fixed/dx7/tables.h"
#include "util/misc.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief MkI's fused feedback stacks: algorithms 4 and 6 with feedback, as one generator kernel.
namespace deluge::dsp::fixed::dx7 {

/// @brief MkI's feedback stack of @p N operators (3 for algorithm 4, 2 for algorithm 6): op 6 reads the feedback
/// history, each op after it is modulated by the op before, and the last op's output is both the stack's output and
/// the next sample's feedback.
/// @details The DX7 loops the feedback from the bottom of the stack in these two algorithms, which an op-by-op render
/// cannot, so MkI renders the stack sample by sample here. The output always replaces what is in the bus. The
/// feedback is shifted down two steps more than elsewhere, by at most 17.
/// @tparam N The ops in the stack: 2 or 3. Op 0 is the patch's op 6.
template <std::size_t N>
requires(N == 2 || N == 3)
struct MkIFeedbackStack {
	using value_type = q31; ///< A sample, Q24.
	using step_type = q31;  ///< One sample per step.

	/// @brief Each op's level and pitch, and the voice's feedback setting.
	struct Settings {
		std::array<int32_t, N> level_in{}; ///< Each op's level, Q24 doublings.
		std::array<int32_t, N> freq{};     ///< Each op's phase increment, 2^24 per cycle.
		int32_t feedback_shift = 16;       ///< The voice's feedback shift, before the stack's two extra steps.
		const Tables* tables = nullptr;    ///< The engine's tables.
	};

	/// @brief Settings with the stack's feedback shift applied.
	struct Coefficients {
		std::array<int32_t, N> level_in{}; ///< As Settings::level_in.
		std::array<int32_t, N> freq{};     ///< As Settings::freq.
		int32_t feedback_shift = 16;       ///< The shift the history is read at: the voice's plus 2, at most 16.
		const Tables* tables = nullptr;    ///< As Settings::tables.
	};

	/// @brief What the sample loop steps: each op's phase and attenuation, and the feedback history.
	struct Loop {
		std::array<q31, N> phase{}; ///< Each op's phase at the next sample.
		std::array<q31, N> gain{};  ///< Each op's attenuation at the last sample, 1/1024 octave (16 bits used).
		FeedbackHistory feedback{}; ///< The voice's feedback history.
	};

	/// @brief The stack between blocks, and the loop within one.
	struct State {
		std::array<OperatorState, N> op{}; ///< Each op's held phase and attenuation.
		Loop loop{};                       ///< The loop's state.
	};
	static constexpr auto kLoopState = &State::loop; ///< The sample loop steps Loop only.

	/// @brief The block's attenuation steps.
	struct Block {
		std::array<int32_t, N> dgain{}; ///< Added to each op's attenuation before each sample.
	};

	/// @brief The settings, with the feedback shift two steps further, to at most 16.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		return {.level_in = s.level_in,
		        .freq = s.freq,
		        .feedback_shift = std::min(s.feedback_shift + 2, int32_t{16}),
		        .tables = s.tables};
	}

	/// @brief Starts the loop at every op's held phase and attenuation, holds the block's end attenuations, and moves
	/// every held phase on by the block.
	/// @details Op 0 ramps as any MkI op does. Quirk carried for bit-exactness: an op after it starts at its new
	/// attenuation and holds there, except that a new attenuation of 0 (full level) starts just below silence and
	/// falls by kEnvMax - 1 per sample, wrapping in the log-sine's 16 bits.
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const Coefficients& c, State& s, std::size_t n) {
		Block b{};
		const Gains g = Operator::gains(
		    {.mode = Operator::Mode::MKI_FEEDBACK_SET, .level_in = c.level_in[0], .tables = c.tables}, s.op[0], n);
		s.loop.gain[0] = g.gain1;
		b.dgain[0] = g.dgain;
		s.op[0].gain_out = g.gain2;
		for (std::size_t k = 1; k < N; ++k) {
			const int32_t gain_out = kEnvMax - (c.level_in[k] >> (28 - 14));
			const int32_t start = gain_out == 0 ? kEnvMax - 1 : gain_out;
			s.op[k].gain_out = gain_out;
			s.loop.gain[k] = start;
			b.dgain[k] = gain_out - start;
		}
		for (std::size_t k = 0; k < N; ++k) {
			s.loop.phase[k] = s.op[k].phase;
			s.op[k].phase = Operator::advance_phase(s.op[k].phase, c.freq[k], n);
		}
		return b;
	}

	/// @brief One sample down the stack.
	/// @return The last op's sample, which is also the next sample's feedback.
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& c, const Block& b, Loop& l) {
		const q31 scaled = wrapping_add(l.feedback.y0, l.feedback.y1) >> (c.feedback_shift + 1);
		l.gain[0] = wrapping_add(l.gain[0], b.dgain[0]);
		l.feedback.y0 = l.feedback.y1;
		q31 y = mki_sin(*c.tables, wrapping_add(l.phase[0], scaled), static_cast<uint16_t>(l.gain[0]));
		l.phase[0] = wrapping_add(l.phase[0], c.freq[0]);
		util::constexpr_for<1, N, 1>([&]<std::size_t K> [[gnu::always_inline]] () {
			l.gain[K] = wrapping_add(l.gain[K], b.dgain[K]);
			y = mki_sin(*c.tables, wrapping_add(l.phase[K], y), static_cast<uint16_t>(l.gain[K]));
			l.phase[K] = wrapping_add(l.phase[K], c.freq[K]);
		});
		l.feedback.y1 = y;
		return y;
	}

	/// @brief Loads the ops' held state and the voice's feedback history before a block.
	[[gnu::always_inline]] static void resume(State& s, const std::array<OperatorState, N>& op,
	                                          FeedbackHistory feedback) {
		s.op = op;
		s.loop.feedback = feedback;
	}
};

static_assert(generator_kernel<MkIFeedbackStack<2>>);
static_assert(generator_kernel<MkIFeedbackStack<3>>);
static_assert(!std::is_polymorphic_v<MkIFeedbackStack<2>>);
static_assert(!std::is_polymorphic_v<MkIFeedbackStack<3>>);

} // namespace deluge::dsp::fixed::dx7
