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
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/stereo_vector.h"
#include "dsp-next/core/types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

/// @file
/// @brief Test kernels for the loop-state trait: one arithmetic written twice, once stepping the whole State and
/// once declaring kLoopState, so that every wrapper path can be compared bit for bit.
namespace deluge::dsp::loop_state_test {

/// @brief How a step folds its input into the ring.
enum class Mode : uint8_t { ADD, XOR };

/// @brief The part of the test kernels' State their step reads and writes.
struct Loop {
	q31 acc = 0;          ///< The inputs' running sum, wrapping.
	std::uint32_t at = 0; ///< The ring slot the next step reads, 0 to 3.
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
	q31 ring[4]{}; ///< What each slot's last step left.
};

/// @brief What the loop-state test kernels share: a moded processor of q31 whose State holds a block counter, the
/// last block's length and a level outside the loop. `prepare` turns them into the block's offset and moves the
/// loop's slot, so a wrapper that copies the loop state before `prepare`, or the whole State, gives itself away.
/// @details Every member is a 32-bit word, so a State compares as bytes.
struct SplitCore {
	using value_type = q31;
	using step_type = q31;
	using Mode = loop_state_test::Mode;
	using Modes = ModeList<Mode::ADD, Mode::XOR>;
	struct Settings {
		Mode mode = Mode::ADD;
	};
	struct Coefficients {
		Mode mode = Mode::ADD;
	};
	struct State {
		std::uint32_t prepares = 0; ///< Blocks prepared.
		std::uint32_t last_n = 0;   ///< The last block's length.
		q31 level = 0;              ///< Set by set_level; prepare adds it to the block's offset.
		Loop loop{};                ///< The loop's part.
	};
	struct Block {
		q31 offset; ///< Added to every input of the block.
	};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	static Block prepare(const Coefficients& /*c*/, State& s, std::size_t n) {
		++s.prepares;
		s.last_n = static_cast<std::uint32_t>(n);
		s.loop.at = (s.loop.at + s.prepares) & 3U;
		return {.offset = static_cast<q31>(static_cast<std::uint32_t>(s.level) + s.prepares)};
	}
	/// @brief Event: the level the next blocks' offset starts from.
	static void set_level(State& s, q31 level) { s.level = level; }
	/// @brief One step's arithmetic on the loop's part alone.
	template <Mode M>
	[[gnu::always_inline]] static q31 advance(const Block& b, Loop& l, q31 x) {
		const auto old = static_cast<std::uint32_t>(l.ring[l.at]);
		const std::uint32_t ux = static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(b.offset);
		l.acc = static_cast<q31>(static_cast<std::uint32_t>(l.acc) + ux);
		l.ring[l.at] = static_cast<q31>(M == Mode::ADD ? old + ux : old ^ (ux * 7U));
		l.at = (l.at + 3U) & 3U;
		return static_cast<q31>(old + static_cast<std::uint32_t>(l.acc));
	}
};

/// @brief SplitCore stepping its whole State: the reference every wrapper path is compared against.
struct SplitWhole : SplitCore {
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& b, State& s, q31 x) {
		return advance<M>(b, s.loop, x);
	}
};

/// @brief SplitCore declaring its loop's part: the step takes `State::loop` alone.
struct SplitLoop : SplitCore {
	static constexpr auto kLoopState = &State::loop;
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& b, Loop& s, q31 x) {
		return advance<M>(b, s, x);
	}
};

/// @brief SplitLoop, stepping its loop state in place.
struct SplitLoopInPlace : SplitLoop {
	static constexpr bool kStateInPlace = true;
};

/// @brief SplitWhole, stepping its whole State in place.
struct SplitWholeInPlace : SplitWhole {
	static constexpr bool kStateInPlace = true;
};

/// @brief A vector kernel's State for the loop-state tests: the loop's part, a block counter and a level outside it.
/// @details Two spare words fill it to a whole number of vectors, so it has no padding and compares as bytes.
template <typename Loop>
struct VecState {
	Loop loop{};                ///< The loop's part.
	std::uint32_t prepares = 0; ///< Blocks prepared.
	q31 level = 0;              ///< Set by set_level; prepare adds it to the block's offset.
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
	std::uint32_t spare[2]{}; ///< Fills the State to a whole number of vectors.
};

/// @brief What the vector loop-state test kernels share: per vector of q31 samples, a running sum offset by the block's
/// offset, which `prepare` makes from the level outside the loop.
struct VecSplitCore {
	using value_type = q31;
	using step_type = Argon<q31>;
	struct Settings {};
	struct Coefficients {};
	struct Loop {
		Argon<q31> acc; ///< The running sum, per lane, wrapping.
	};
	using State = VecState<Loop>;
	struct Block {
		q31 offset; ///< Added to every input of the block.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static Block prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) {
		++s.prepares;
		return {.offset = static_cast<q31>(static_cast<std::uint32_t>(s.level) + s.prepares)};
	}
	/// @brief Event: the level the next blocks' offset starts from.
	static void set_level(State& s, q31 level) { s.level = level; }
	/// @brief One step's arithmetic on the loop's part alone.
	[[gnu::always_inline]] static Argon<q31> advance(const Block& b, Loop& l, Argon<q31> x) {
		l.acc = l.acc + x + Argon<q31>{b.offset};
		return l.acc;
	}
};

/// @brief VecSplitCore stepping its whole State.
struct VecSplitWhole : VecSplitCore {
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const Block& b, State& s, Argon<q31> x) {
		return advance(b, s.loop, x);
	}
};

/// @brief VecSplitCore declaring its loop's part.
struct VecSplitLoop : VecSplitCore {
	static constexpr auto kLoopState = &State::loop;
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const Block& b, Loop& s, Argon<q31> x) {
		return advance(b, s, x);
	}
};

/// @brief VecSplitLoop, stepping its loop state in place.
struct VecSplitLoopInPlace : VecSplitLoop {
	static constexpr bool kStateInPlace = true;
};

/// @brief What the interleaved loop-state test kernels share: per vector of stereo frames, a running sum per channel,
/// the block's offset added on the left and taken off on the right.
struct StereoVecSplitCore {
	using value_type = StereoSample;
	using step_type = StereoVector;
	struct Settings {};
	struct Coefficients {};
	struct Loop {
		StereoVector acc; ///< The running sums, per channel and lane, wrapping.
	};
	using State = VecState<Loop>;
	struct Block {
		q31 offset; ///< Added on the left and taken off on the right of every frame of the block.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static Block prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) {
		++s.prepares;
		return {.offset = static_cast<q31>(static_cast<std::uint32_t>(s.level) + s.prepares)};
	}
	/// @brief Event: the level the next blocks' offset starts from.
	static void set_level(State& s, q31 level) { s.level = level; }
	/// @brief One step's arithmetic on the loop's part alone.
	[[gnu::always_inline]] static StereoVector advance(const Block& b, Loop& l, StereoVector x) {
		l.acc.l = l.acc.l + x.l + Argon<q31>{b.offset};
		l.acc.r = l.acc.r + x.r - Argon<q31>{b.offset};
		return l.acc;
	}
};

/// @brief StereoVecSplitCore stepping its whole State.
struct StereoVecSplitWhole : StereoVecSplitCore {
	[[gnu::always_inline]] static StereoVector step(const Coefficients& /*c*/, const Block& b, State& s,
	                                                StereoVector x) {
		return advance(b, s.loop, x);
	}
};

/// @brief StereoVecSplitCore declaring its loop's part.
struct StereoVecSplitLoop : StereoVecSplitCore {
	static constexpr auto kLoopState = &State::loop;
	[[gnu::always_inline]] static StereoVector step(const Coefficients& /*c*/, const Block& b, Loop& s,
	                                                StereoVector x) {
		return advance(b, s, x);
	}
};

/// @brief StereoVecSplitLoop, stepping its loop state in place.
struct StereoVecSplitLoopInPlace : StereoVecSplitLoop {
	static constexpr bool kStateInPlace = true;
};

/// @return @p s as 32-bit words, so that a mismatch prints, as numbers, where two values differ.
template <typename T>
[[nodiscard]] std::vector<std::uint32_t> state_words(const T& s) {
	static_assert(sizeof s % sizeof(std::uint32_t) == 0, "the loop-state test States are whole words");
	std::vector<std::uint32_t> words(sizeof s / sizeof(std::uint32_t));
	std::memcpy(words.data(), &s, sizeof s);
	return words;
}

/// @return @p n samples of a full-range sequence, different for each @p block.
[[nodiscard]] inline std::vector<q31> input(std::size_t n, std::size_t block) {
	std::vector<q31> v(n);
	std::uint32_t x = 0x9e3779b9U * static_cast<std::uint32_t>(block + 1);
	for (q31& sample : v) {
		x = (x * 1664525U) + 1013904223U;
		sample = static_cast<q31>(x);
	}
	return v;
}

} // namespace deluge::dsp::loop_state_test
