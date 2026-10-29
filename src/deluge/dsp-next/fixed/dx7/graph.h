// Based on Dexed's fm_core (Copyright 2012 Google Inc., Apache License 2.0) and EngineMkI (Copyright (C) 2015-2017
// Pascal Gauthier, GPLv3).

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

#include "dsp-next/core/mono.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/dx7/algorithms.h"
#include "dsp-next/fixed/dx7/mki_feedback_stack.h"
#include "dsp-next/fixed/dx7/operator.h"
#include "dsp-next/fixed/dx7/tables.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

/// @file
/// @brief A DX7 voice's operators walked through its algorithm once per block, in either engine.
namespace deluge::dsp::fixed::dx7 {

/// @brief No vector modern operator: every operator renders through dx7::Operator.
struct NoModernVector {
	static constexpr bool kRenders = false; ///< Never used in place of the operator.
};

/// @brief The two intermediate buses operators render into. One instance, owned by the app and lent to every voice's
/// graph per block: voices render one at a time, so no two hold the buses at once.
struct Buses {
	alignas(16) std::array<std::array<q31, kMaxBlock>, 2> bus{}; ///< Bus one and bus two.
};

/// @brief Silence: the modulation of an operator with no input, and the mix of one that sets its output, for a vector
/// operator that always reads both.
alignas(16) inline constexpr std::array<q31, kMaxBlock> kZeros{};

/// @brief A DX7 voice's six operators, walked through its algorithm once per block.
/// @details A component, not a kernel: it holds the six operators' states and the voice's feedback history, and runs
/// each operator through its kernel per block. Its verbs are @ref renderBlock, @ref restart, @ref view, @ref resume and
/// @ref gain_out. It keeps no settings between blocks: each block's levels and pitches arrive with the block, so a
/// voice holds nothing here but what it held before (six phases and gains, and the history).
///
/// **The DX7 engine's rules**, each applied where @ref renderBlock walks the ops:
/// - *The gate.* An operator inaudible at both ends of the block (@ref Operator::audible) is skipped: its phase and
///   gain move on, it writes nothing, and a bus it would have set is marked empty.
/// - *Adding to an empty bus sets it.* An operator that adds into a bus nothing has written this block sets it.
/// - *An empty input is no input.* An operator whose input bus is empty renders a plain sine (or takes the feedback).
/// - *MkI fuses algorithms 4 and 6.* With feedback, MkI renders op 6's whole stack (three ops in algorithm 4, two in
///   6) at once, with the feedback taken from the stack's bottom (@ref MkIFeedbackStack).
/// - *MkI shifts algorithm 32's feedback two steps further*, to at most 16.
/// @tparam ModernVector The vector modern operator for the NONE and BUS inputs, or NoModernVector. When it renders, it
/// may write past the block, up to kMaxBlock samples, into the output and the buses.
template <class ModernVector = NoModernVector>
class Graph {
public:
	/// @brief One block's settings: the patch's routing and the six operators' levels and pitches.
	struct Settings {
		Engine engine = Engine::MODERN;    ///< The engine.
		uint8_t algorithm = 0;             ///< The algorithm, 0 to 31.
		uint8_t feedback = 0;              ///< The feedback, 0 (off) to 7.
		std::array<int32_t, 6> level_in{}; ///< Each op's level, Q24 doublings.
		std::array<int32_t, 6> freq{};     ///< Each op's phase increment, 2^24 per cycle.
		const Tables* tables = nullptr;    ///< The engine's tables.
	};

	/// @brief The graph's state as the owner sees it.
	struct View {
		std::array<OperatorState, 6> ops{}; ///< Each op's held phase and gain.
		FeedbackHistory feedback{};         ///< The feedback history.
	};

	/// @brief Renders one block over the six operators.
	/// @param settings This block's routing, levels and pitches.
	/// @param out The block, 1 to kMaxBlock samples; with a rendering ModernVector, kMaxBlock samples must be writable
	/// from its start.
	/// @param buses The intermediate buses, lent by the owner.
	void renderBlock(const Settings& settings, Buffer out, Buses& buses);

	/// @brief Restarts every op: gain 0 and the given phases (zeros for oscillator key sync, drawn otherwise). The
	/// feedback history carries over.
	void restart(std::span<const q31, 6> phases) {
		for (std::size_t op = 0; op < 6; ++op) {
			ops_[op] = {.phase = phases[op], .gain_out = 0};
		}
	}

	/// @brief The six ops' states and the feedback history.
	[[nodiscard]] View view() const { return {.ops = ops_, .feedback = feedback_}; }

	/// @brief Loads the six ops' states and the feedback history: view()'s counterpart.
	void resume(const View& view) {
		ops_ = view.ops;
		feedback_ = view.feedback;
	}

	/// @brief Op @p op's gain at the end of the last block.
	[[nodiscard]] int32_t gain_out(std::size_t op) const { return ops_[op].gain_out; }

private:
	/// @brief Op @p op's skipped block: its phase and gain move on as its kernel's skip moves them.
	[[gnu::always_inline]] void skip_op(const Operator::Settings& settings, std::size_t op, std::size_t n) {
		Mono<Operator> held{settings};
		held.on<&Operator::resume>(ops_[op], feedback_);
		held.on<&Operator::skip>(n);
		ops_[op] = held.state().op;
	}

	/// @brief Op @p op's block into @p target, through the vector modern operator when there is one and the op
	/// takes no feedback, else through its kernel.
	[[gnu::always_inline]] void render_op(const Operator::Settings& settings, std::size_t op, Signal modulation,
	                                      Buffer target) {
		const std::size_t n = target.size();
		if constexpr (ModernVector::kRenders) {
			if (settings.engine == Engine::MODERN && settings.input != Input::FEEDBACK) {
				const Gains g = Operator::gains(Operator::coefficients(settings), ops_[op], n);
				ModernVector::render(settings.input == Input::NONE ? kZeros.data() : modulation.data(),
				                     settings.output == Output::ADD ? target.data() : kZeros.data(), target.data(), n,
				                     ops_[op].phase, settings.freq, g.gain1, g.dgain);
				skip_op(settings, op, n);
				return;
			}
		}
		Mono<Operator> unit{settings};
		unit.on<&Operator::resume>(ops_[op], feedback_);
		unit.renderBlock(modulation, target, target);
		ops_[op] = unit.state().op;
		if (settings.input == Input::FEEDBACK) {
			feedback_ = unit.state().loop.feedback;
		}
	}

	/// @brief MkI's fused stack of @p N ops from op @p op into @p target.
	template <std::size_t N>
	[[gnu::always_inline]] void render_stack(const Settings& s, std::size_t op, int32_t shift, Buffer target) {
		typename MkIFeedbackStack<N>::Settings stack_settings{.feedback_shift = shift, .tables = s.tables};
		std::array<OperatorState, N> held{};
		for (std::size_t k = 0; k < N; ++k) {
			stack_settings.level_in[k] = s.level_in[op + k];
			stack_settings.freq[k] = s.freq[op + k];
			held[k] = ops_[op + k];
		}
		Mono<MkIFeedbackStack<N>> stack{stack_settings};
		stack.template on<&MkIFeedbackStack<N>::resume>(held, feedback_);
		stack.renderBlock(target);
		for (std::size_t k = 0; k < N; ++k) {
			ops_[op + k] = stack.state().op[k];
		}
		feedback_ = stack.state().loop.feedback;
	}

	std::array<OperatorState, 6> ops_{}; ///< Each op's held phase and gain.
	FeedbackHistory feedback_{};         ///< The feedback history; carries across a restart.
};

template <class ModernVector>
void Graph<ModernVector>::renderBlock(const Settings& s, Buffer out, Buses& lent) {
	const std::size_t n = out.size();
	const bool mki = s.engine == Engine::MKI;
	constexpr int32_t kFeedbackBits = 8;
	const int32_t shift = s.feedback != 0 ? kFeedbackBits - s.feedback : 16;
	const bool feedback_on = shift < 16;

	std::array<uint8_t, 6> ops = std::to_array(kAlgorithms[s.algorithm]);
	if (mki && feedback_on && (s.algorithm == 3 || s.algorithm == 5)) {
		ops[0] = kMkIFusedFeedback; // MkI fuses op 6's stack in algorithms 4 and 6
	}

	std::array<bool, 3> has_contents{true, false, false};
	const std::array<q31*, 3> buses{out.data(), lent.bus[0].data(), lent.bus[1].data()};
	for (std::size_t op = 0; op < 6; ++op) {
		const uint8_t flags = ops[op];
		bool add = (flags & OUT_BUS_ADD) != 0;
		const int inbus = (flags >> 4) & 3;
		const int outbus = flags & 3;
		const Operator::Settings probe{.engine = s.engine,
		                               .level_in = s.level_in[op],
		                               .freq = s.freq[op],
		                               .feedback_shift = shift,
		                               .tables = s.tables};
		if (!Operator::audible(Operator::coefficients(probe), ops_[op], n)) {
			skip_op(probe, op, n); // the gate
			if (!add) {
				has_contents[outbus] = false;
			}
			continue;
		}
		if (!has_contents[outbus]) {
			add = false; // adding to an empty bus sets it
		}
		Operator::Settings settings = probe;
		settings.output = add ? Output::ADD : Output::SET;
		const Buffer target{buses[outbus], n};
		if (inbus == 0 || !has_contents[inbus]) { // an empty input is no input
			if ((flags & (FB_IN | FB_OUT)) == (FB_IN | FB_OUT) && feedback_on) {
				if (mki && s.algorithm == 3) {
					render_stack<3>(s, op, shift, target);
					op += 2;
				}
				else if (mki && s.algorithm == 5) {
					render_stack<2>(s, op, shift, target);
					op += 1;
				}
				else {
					settings.input = Input::FEEDBACK;
					if (mki && s.algorithm == 31) {
						settings.feedback_shift = std::min(shift + 2, int32_t{16}); // MkI's algorithm 32
					}
					render_op(settings, op, target, target);
				}
			}
			else {
				render_op(settings, op, target, target);
			}
		}
		else {
			settings.input = Input::BUS;
			render_op(settings, op, Signal{buses[inbus], n}, target);
		}
		has_contents[outbus] = true;
	}
}

extern template class Graph<NoModernVector>;

} // namespace deluge::dsp::fixed::dx7
