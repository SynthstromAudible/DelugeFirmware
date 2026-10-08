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
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/stereo_vector.h"
#include "dsp-next/core/types.h"
#include "util/fixedpoint.h"

#include <array>
#include <concepts>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <tuple>

using namespace deluge::dsp;

namespace {

/// A processor kernel without modes or prepare: a q31 gain (Q1.31, scaled ×2 to reach unity).
struct Gain {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 gain; ///< Q1.31.
	};
	struct Coefficients {
		q31 gain; ///< Q1.31.
	};
	struct State {};
	static Coefficients coefficients(const Settings& s) { return {.gain = s.gain}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return multiply_32x32_rshift32(x, c.gain) << 1;
	}
};

/// A moded processor kernel: halves or doubles.
struct Shaped {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { HALF, DOUBLE };
	using Modes = ModeList<Mode::HALF, Mode::DOUBLE>;
	struct Settings {
		Mode mode;
	};
	struct Coefficients {
		Mode mode;
	};
	struct State {};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		if constexpr (M == Mode::HALF) {
			return x >> 1;
		}
		else {
			return x << 1;
		}
	}
};

/// A generator kernel: counts up from its state.
struct Counter {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 n;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s) {
		return ++s.n;
	}
};

/// A mixer kernel: sums its two inputs.
struct Add {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 a,
	                                       q31 b2) {
		return a + b2;
	}
};

/// A processor kernel with a prepare: the block's offset comes from State.
struct Prepared {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 offset;
	};
	struct Block {
		q31 offset;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static Block prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) { return {.offset = s.offset}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& b, State& /*s*/, q31 x) {
		return x + b.offset;
	}
};

/// A vector processor kernel whose ramp persists across blocks, so its partial last vector advances by the count.
struct Ramp {
	using value_type = q31;
	using step_type = Argon<q31>;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {
		Argon<q31> value; ///< The ramp's value in each lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& c, const EmptyBlock& b, State& s, Argon<q31> x) {
		return step(c, b, s, x, Argon<q31>::lanes);
	}
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                              Argon<q31> x, std::size_t count) {
		const Argon<q31> out = x + s.value;
		s.value = s.value + Argon<q31>{static_cast<q31>(count)};
		return out;
	}
};

/// A block-staged kernel: reverses its block.
struct Staged {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static void process(const Coefficients& /*c*/, State& /*s*/, Signal<q31> in, Buffer<q31> out) {
		const std::size_t n = in.size();
		for (std::size_t i = 0; i < n; ++i) {
			out[i] = in[n - 1 - i];
		}
	}
};

/// A component written with its own verbs instead of a kernel's.
struct CustomVerbs {
	using value_type = q31;
	q31 gain = 0;
	void configure(q31 g) { gain = g; }
	void renderMono(Buffer<q31> out) {
		for (q31& v : out) {
			v = gain;
		}
	}
};

/// A kernel that provides both a processor and a mixer step.
struct TwoArities {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) { return x; }
	static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 a, q31 b2) { return a + b2; }
};

/// A kernel whose prepare has the wrong signature.
struct BadPrepare {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static int prepare(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) { return 0; }
	static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) { return x; }
};

/// A kernel whose State is not a plain value struct: copying it runs code.
struct NotPlain {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		State() = default;
		State(const State& /*other*/) {}
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) { return x; }
};

static_assert(processor_kernel<Gain>);
static_assert(moded_kernel<Shaped>);
static_assert(processor_kernel<Shaped>);
static_assert(generator_kernel<Counter>);
static_assert(mixer_kernel<Add>);
static_assert(processor_kernel<Prepared>);
static_assert(processor_kernel<Ramp>);
static_assert(staged_kernel<Staged>);
static_assert(!kernel<CustomVerbs>);
static_assert(!moded_kernel<Gain>);
static_assert(std::same_as<kernel_block_t<Gain>, EmptyBlock>);
static_assert(std::same_as<kernel_block_t<Prepared>, Prepared::Block>);
static_assert(partial_step_advances_by_count<Ramp>);
static_assert(!partial_step_advances_by_count<Gain>);

// One arity per kernel: each reference kernel satisfies exactly one of the four.
static_assert(!generator_kernel<Gain> && !mixer_kernel<Gain> && !staged_kernel<Gain>);
static_assert(!processor_kernel<Counter> && !mixer_kernel<Counter> && !staged_kernel<Counter>);
static_assert(!generator_kernel<Add> && !processor_kernel<Add> && !staged_kernel<Add>);
static_assert(!generator_kernel<Staged> && !processor_kernel<Staged> && !mixer_kernel<Staged>);
static_assert(!mixer_kernel<Ramp>);
static_assert(kernel_base<TwoArities> && !kernel<TwoArities>);
static_assert(!kernel_base<BadPrepare> && !kernel<BadPrepare>);
static_assert(!kernel_base<NotPlain> && !kernel<NotPlain>);

} // namespace

// clang-format off
describe kernel_concepts("kernel concepts", ${
	it("steps each reference kernel directly", _ {
		const EmptyBlock none{};

		Gain::State gain_state{};
		const Gain::Coefficients half = Gain::coefficients({.gain = 0x40000000}); // 0.5 in Q1.31
		expect(Gain::step(half, none, gain_state, 1000)).to_equal(500);

		Shaped::State shaped_state{};
		const Shaped::Coefficients shaped = Shaped::coefficients({.mode = Shaped::Mode::DOUBLE});
		expect(shaped.mode).to_equal(Shaped::Mode::DOUBLE);
		expect(Shaped::step<Shaped::Mode::HALF>(shaped, none, shaped_state, 100)).to_equal(50);
		expect(Shaped::step<Shaped::Mode::DOUBLE>(shaped, none, shaped_state, 100)).to_equal(200);

		Counter::State counter_state{};
		std::ignore = Counter::step({}, none, counter_state);
		expect(Counter::step({}, none, counter_state)).to_equal(2);

		Add::State add_state{};
		expect(Add::step({}, none, add_state, 3, 4)).to_equal(7);

		Prepared::State prepared_state{.offset = 10};
		const Prepared::Block block = Prepared::prepare({}, prepared_state, 4);
		expect(Prepared::step({}, block, prepared_state, 5)).to_equal(15);

		Ramp::State ramp_state{.value = Argon<q31>{0}};
		std::ignore = Ramp::step({}, none, ramp_state, Argon<q31>{100});
		std::ignore = Ramp::step({}, none, ramp_state, Argon<q31>{100}, 3);
		std::array<q31, 4> lanes{};
		ramp_state.value.StoreTo(lanes.data());
		expect(lanes[0]).to_equal(7);
		expect(lanes[3]).to_equal(7);

		Staged::State staged_state{};
		const std::array<q31, 3> in{1, 2, 3};
		std::array<q31, 3> out{};
		Staged::process({}, staged_state, in, out);
		expect(out[0]).to_equal(3);
		expect(out[2]).to_equal(1);
	});
});

CPPSPEC_MAIN(kernel_concepts);
