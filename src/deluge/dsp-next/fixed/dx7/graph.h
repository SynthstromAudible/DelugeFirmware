/*
 * Derived from Dexed's fm_core, under its licence:
 *
 * Copyright 2012 Google Inc.
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

#include "dsp-next/core/mono.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/dx7/algorithms.h"
#include "dsp-next/fixed/dx7/mki_feedback_stack.h"
#include "dsp-next/fixed/dx7/modern_vector_operator.h"
#include "dsp-next/fixed/dx7/operator.h"
#include "dsp-next/fixed/dx7/tables.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

/// @file
/// @brief A DX7 voice's operators walked through its algorithm once per block, in either engine.
namespace deluge::dsp::fixed::dx7 {

/// @brief The longest block a voice renders, in samples, and the length of each bus: the voice adapter's block buffer.
inline constexpr std::size_t kMaxBlock = 132;

/// @brief The two intermediate buses operators render into. One instance, owned by the app and lent to every voice's
/// graph per block: voices render one at a time, so no two hold the buses at once.
struct Buses {
	alignas(16) std::array<std::array<q31, kMaxBlock>, 2> bus{}; ///< Bus one and bus two.
};

/// @brief A DX7 voice's six operators, walked through its algorithm once per block.
/// @details A component, not a kernel: it holds the six operators' states and the voice's feedback history, and runs
/// each operator through its kernel per block. Its verbs are @ref renderBlock, @ref restart, @ref view, @ref resume and
/// @ref gain_out. It keeps no settings between blocks: each block's levels and pitches arrive with the block, so a
/// voice holds only its six phases and gains and the history. @ref view is read-only inspection, as `FilterSet`'s is;
/// @ref resume is its counterpart, for an owner that holds the operators' state elsewhere (the reference specs drive
/// the graph from the verbatim engine's state through the two).
///
/// **The DX7 engine's rules**, each applied where @ref renderBlock walks the ops:
/// - *The gate.* An operator inaudible at both ends of the block (@ref Operator::audible) is skipped: its phase and
///   gain move on, it writes nothing, and a bus it would have set is marked empty.
/// - *Adding to an empty bus sets it.* An operator that adds into a bus nothing has written this block sets it.
/// - *An empty input is no input.* An operator whose input bus is empty renders a plain sine (or takes the feedback).
/// - *MkI fuses algorithms 4 and 6.* With feedback, MkI renders op 6's whole stack (three ops in algorithm 4, two in
///   6) at once, with the feedback taken from the stack's bottom (@ref MkIFeedbackStack).
/// - *MkI shifts algorithm 32's feedback two steps further*, to at most 16.
///
/// A modern op with no input or a bus input renders through @ref ModernVectorOperator, four samples a step, three
/// steps interleaved; a build given the device's NEON kernel (the firmware: `DELUGE_DX7_NEON_KERNEL` names its
/// symbol) renders those ops through the kernel instead, bit for bit the same. A modern feedback op and every MkI op
/// render through @ref Operator.
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
	/// @param out The block, 1 to kMaxBlock samples. In a build given the device's NEON kernel, kMaxBlock samples must
	/// be writable from its start, 16-byte aligned: the kernel pads the block and writes past it.
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

	/// @brief Op @p op's block into @p target: a modern op without feedback through the vector operator (or, in a
	/// build given the device's NEON kernel, through it), any other through the scalar operator. With no input
	/// @p modulation is not read; with SET, neither is @p target's content. Defined in graph.cpp, the one place the
	/// kernel's build switch is read, and inlined into renderBlock there, where each call's routing is known.
	[[gnu::always_inline]] inline void render_op(const Operator::Settings& settings, std::size_t op, Signal modulation,
	                                             Buffer target);

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

static_assert(!std::is_polymorphic_v<Graph>);

} // namespace deluge::dsp::fixed::dx7
