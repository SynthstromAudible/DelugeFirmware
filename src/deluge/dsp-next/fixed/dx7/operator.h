// Based on Dexed's fm_op_kernel and fm_core (Copyright 2012 Google Inc., Apache License 2.0) and EngineMkI
// (Copyright (C) 2015-2017 Pascal Gauthier, GPLv3).

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
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/dx7/algorithms.h"
#include "dsp-next/fixed/dx7/lookup.h"
#include "dsp-next/fixed/dx7/tables.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief One DX7 operator: a sine at a phase, scaled by a gain ramped across the block, in either engine.
namespace deluge::dsp::fixed::dx7 {

/// @brief The engine an operator renders in.
enum class Engine : uint8_t {
	MODERN, ///< Dexed's: the sine table times a linear gain from exp2 of the level.
	MKI,    ///< The DX7's own: log-sine plus an attenuation, through the exp table.
};

/// @brief What an operator adds to its phase.
enum class Input : uint8_t {
	NONE,     ///< Nothing: a plain sine.
	BUS,      ///< A bus another op wrote: the modulation.
	FEEDBACK, ///< The voice's feedback history, scaled down.
};

/// @brief What an operator does with what is already in its output.
enum class Output : uint8_t {
	SET, ///< Replaces it.
	ADD, ///< Adds to it.
};

/// @brief An operator's state carried across blocks, and across changes of engine and input.
struct OperatorState {
	q31 phase = 0;    ///< The phase at the next block's start, 2^24 per cycle, wrapping.
	q31 gain_out = 0; ///< The gain the last block ended at: linear (modern) or attenuation (MkI).
};

/// @brief The voice's one feedback history: the feedback op's last two outputs.
struct FeedbackHistory {
	q31 y0 = 0; ///< The output before last.
	q31 y1 = 0; ///< The last output.
};

/// @brief A block's gain ramp.
struct Gains {
	int32_t gain1; ///< The gain the block starts from: the last block's end.
	int32_t gain2; ///< The gain the block ends at, from the level.
	int32_t dgain; ///< The step per sample, added before each sample.
};

/// @brief One DX7 operator as a mixer kernel: its two inputs are the modulation and the accumulator.
/// @details Each sample the gain takes its step, the sine is read at the phase plus the input (the modulation bus,
/// or the feedback history scaled down by feedback_shift + 1), scaled by the gain, then set into or added to the
/// accumulator; the phase moves on by freq. The mode is the engine, the input and the output; a mode change applies
/// at the next block, and the phase and gain carry across it.
///
/// **Per block** (@ref prepare): the gain ramps from the last block's end to the level's gain over the block, and the
/// held phase moves on by the whole block. The owner first loads the voice's state with @ref resume, and asks
/// @ref audible whether the block renders at all; an inaudible op's block is @ref skip, which moves the same state on
/// without rendering.
///
/// **Quirks carried for bit-exactness:**
/// - *MkI's fresh op.* MkI holds an attenuation, so a gain_out of 0 (an op that has not rendered) is read as just
///   below silence, kEnvMax - 1.
/// - *MkI's gain is 16 bits.* The ramped attenuation is passed to the log-sine as a uint16_t, wrapping.
/// - *The feedback history holds the scaled output:* modern's output after the gain, MkI's log-sine output.
struct Operator {
	using value_type = q31; ///< A sample, Q24.
	using step_type = q31;  ///< One sample per step.

	/// @brief The engine, input and output, one mode each.
	enum class Mode : uint8_t {
		MODERN_NONE_SET,
		MODERN_NONE_ADD,
		MODERN_BUS_SET,
		MODERN_BUS_ADD,
		MODERN_FEEDBACK_SET,
		MODERN_FEEDBACK_ADD,
		MKI_NONE_SET,
		MKI_NONE_ADD,
		MKI_BUS_SET,
		MKI_BUS_ADD,
		MKI_FEEDBACK_SET,
		MKI_FEEDBACK_ADD,
	};
	using Modes = ModeList<Mode::MODERN_NONE_SET, Mode::MODERN_NONE_ADD, Mode::MODERN_BUS_SET, Mode::MODERN_BUS_ADD,
	                       Mode::MODERN_FEEDBACK_SET, Mode::MODERN_FEEDBACK_ADD, Mode::MKI_NONE_SET, Mode::MKI_NONE_ADD,
	                       Mode::MKI_BUS_SET, Mode::MKI_BUS_ADD, Mode::MKI_FEEDBACK_SET, Mode::MKI_FEEDBACK_ADD>;

	/// @brief The mode of engine @p e, input @p i and output @p o.
	[[nodiscard]] static constexpr Mode mode_of(Engine e, Input i, Output o) {
		return static_cast<Mode>((static_cast<uint8_t>(e) * 6) + (static_cast<uint8_t>(i) * 2)
		                         + static_cast<uint8_t>(o));
	}
	/// @brief @p m's engine.
	[[nodiscard]] static constexpr Engine engine_of(Mode m) { return static_cast<Engine>(static_cast<uint8_t>(m) / 6); }
	/// @brief @p m's input.
	[[nodiscard]] static constexpr Input input_of(Mode m) {
		return static_cast<Input>(static_cast<uint8_t>(m) % 6 / 2);
	}
	/// @brief @p m's output.
	[[nodiscard]] static constexpr Output output_of(Mode m) { return static_cast<Output>(static_cast<uint8_t>(m) % 2); }

	/// @brief The op's routing and its block's level and pitch.
	struct Settings {
		Engine engine = Engine::MODERN; ///< The engine.
		Input input = Input::NONE;      ///< What the phase adds.
		Output output = Output::SET;    ///< Set or add.
		int32_t level_in = 0;           ///< The level, Q24 doublings, from the envelope and modulation.
		int32_t freq = 0;               ///< The phase increment, 2^24 per cycle.
		int32_t feedback_shift = 16;    ///< The scaled feedback is (y0 + y1) >> (feedback_shift + 1).
		const Tables* tables = nullptr; ///< The engine's tables.
	};

	/// @brief Settings with the routing folded into the mode.
	struct Coefficients {
		Mode mode = Mode::MODERN_NONE_SET; ///< The engine, input and output.
		int32_t level_in = 0;              ///< As Settings::level_in.
		int32_t freq = 0;                  ///< As Settings::freq.
		int32_t feedback_shift = 16;       ///< As Settings::feedback_shift.
		const Tables* tables = nullptr;    ///< As Settings::tables.
	};

	/// @brief What the sample loop steps: the phase and gain within the block, and the feedback history.
	struct Loop {
		q31 phase = 0;              ///< The phase at the next sample.
		q31 gain = 0;               ///< The gain at the last sample.
		FeedbackHistory feedback{}; ///< The voice's feedback history; read and written in the FEEDBACK modes only.
	};

	/// @brief The op between blocks, and the loop within one.
	struct State {
		OperatorState op{}; ///< The held phase and gain, moved on per block by prepare and skip.
		Loop loop{};        ///< The loop's state.
	};
	static constexpr auto kLoopState = &State::loop; ///< The sample loop steps Loop only.

	/// @brief The block's gain step.
	struct Block {
		int32_t dgain = 0; ///< Added to the gain before each sample.
	};

	/// @brief The routing as a mode, the rest as it is.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		return {.mode = mode_of(s.engine, s.input, s.output),
		        .level_in = s.level_in,
		        .freq = s.freq,
		        .feedback_shift = s.feedback_shift,
		        .tables = s.tables};
	}

	/// @brief The block's gain ramp: from the held gain to the level's, over @p n samples (n > 0).
	[[nodiscard]] [[gnu::always_inline]] static Gains gains(const Coefficients& c, const OperatorState& op,
	                                                        std::size_t n) {
		const auto len = static_cast<int32_t>(n);
		const int32_t inv_n = (1 << 30) / len;
		int32_t gain1 = 0;
		int32_t gain2 = 0;
		if (engine_of(c.mode) == Engine::MODERN) {
			gain1 = op.gain_out;
			gain2 = exp2_lookup(*c.tables, wrapping_sub(c.level_in, 14 * (1 << 24)));
		}
		else {
			gain1 = op.gain_out == 0 ? kEnvMax - 1 : op.gain_out;
			gain2 = kEnvMax - (c.level_in >> (28 - 14));
		}
		return {gain1, gain2, div_n(wrapping_add(wrapping_sub(gain2, gain1), len >> 1), inv_n)};
	}

	/// @brief Whether the op renders this block: modern by gain above kGainLevelThresh at either end, MkI by
	/// attenuation within kMkILevelThresh at either end.
	[[nodiscard]] [[gnu::always_inline]] static bool audible(const Coefficients& c, const OperatorState& op,
	                                                         std::size_t n) {
		const Gains g = gains(c, op, n);
		return engine_of(c.mode) == Engine::MODERN ? (g.gain1 >= kGainLevelThresh || g.gain2 >= kGainLevelThresh)
		                                           : (g.gain1 <= kMkILevelThresh || g.gain2 <= kMkILevelThresh);
	}

	/// @brief Starts the loop at the held phase and gain, holds the block's end gain, and moves the held phase on by
	/// the block.
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const Coefficients& c, State& s, std::size_t n) {
		const Gains g = gains(c, s.op, n);
		s.loop.phase = s.op.phase;
		s.loop.gain = g.gain1;
		s.op.gain_out = g.gain2;
		s.op.phase = advance_phase(s.op.phase, c.freq, n);
		return {.dgain = g.dgain};
	}

	/// @brief One sample at mode @p M.
	/// @param modulation The modulation bus's sample; read in the BUS modes only.
	/// @param accumulator What the output holds; read in the ADD modes only.
	/// @return The op's sample, or the accumulator plus it.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& c, const Block& b, Loop& l, q31 modulation,
	                                                     q31 accumulator) {
		l.gain = wrapping_add(l.gain, b.dgain);
		q31 at = l.phase;
		if constexpr (input_of(M) == Input::BUS) {
			at = wrapping_add(at, modulation);
		}
		else if constexpr (input_of(M) == Input::FEEDBACK) {
			at = wrapping_add(at, wrapping_add(l.feedback.y0, l.feedback.y1) >> (c.feedback_shift + 1));
			l.feedback.y0 = l.feedback.y1;
		}
		q31 y = 0;
		if constexpr (engine_of(M) == Engine::MODERN) {
			y = static_cast<q31>((static_cast<int64_t>(sin_lookup(*c.tables, at)) * l.gain) >> 24);
		}
		else {
			y = mki_sin(*c.tables, at, static_cast<uint16_t>(l.gain));
		}
		if constexpr (input_of(M) == Input::FEEDBACK) {
			l.feedback.y1 = y;
		}
		l.phase = wrapping_add(l.phase, c.freq);
		if constexpr (output_of(M) == Output::ADD) {
			return wrapping_add(accumulator, y);
		}
		return y;
	}

	/// @brief Loads the op's held state and the voice's feedback history before a block.
	[[gnu::always_inline]] static void resume(State& s, OperatorState op, FeedbackHistory feedback) {
		s.op = op;
		s.loop.feedback = feedback;
	}

	/// @brief An inaudible op's block: the held gain and phase move on as if it had rendered.
	[[gnu::always_inline]] static void skip(const Coefficients& c, State& s, std::size_t n) {
		s.op.gain_out = gains(c, s.op, n).gain2;
		s.op.phase = advance_phase(s.op.phase, c.freq, n);
	}

	/// @brief @p phase + @p freq × @p n, wrapping at 2^32 as the phase accumulator does.
	[[nodiscard]] [[gnu::always_inline]] static q31 advance_phase(q31 phase, int32_t freq, std::size_t n) {
		return static_cast<q31>(static_cast<uint32_t>(phase)
		                        + (static_cast<uint32_t>(freq) * static_cast<uint32_t>(n)));
	}
};

static_assert(mixer_kernel<Operator>);
static_assert(moded_kernel<Operator>);
static_assert(!std::is_polymorphic_v<Operator>);

} // namespace deluge::dsp::fixed::dx7
