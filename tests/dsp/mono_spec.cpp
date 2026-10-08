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
#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_vector.h"
#include "dsp-next/core/types.h"
#include "util/fixedpoint.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;

namespace {

/// The block lengths every hand-loop case runs: below, at and past one vector, and a long odd tail.
constexpr std::array<std::size_t, 8> kLengths{1, 2, 3, 4, 5, 7, 8, 13};

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

/// A block-staged kernel: reverses its block. It reads each mirrored pair before writing either, so it gives the
/// same result in place as out of place.
struct Staged {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static void process(const Coefficients& /*c*/, State& /*s*/, Signal<q31> in, Buffer<q31> out) {
		const std::size_t n = in.size();
		for (std::size_t i = 0; i < (n + 1) / 2; ++i) {
			const q31 front = in[i];
			const q31 back = in[n - 1 - i];
			out[i] = back;
			out[n - 1 - i] = front;
		}
	}
};

/// A moded block-staged kernel: process is a template on the mode, like step.
struct ModedStaged {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { COPY, NEGATE };
	using Modes = ModeList<Mode::COPY, Mode::NEGATE>;
	struct Settings {
		Mode mode;
	};
	struct Coefficients {
		Mode mode;
	};
	struct State {};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	template <Mode M>
	static void process(const Coefficients& /*c*/, State& /*s*/, Signal<q31> in, Buffer<q31> out) {
		for (std::size_t i = 0; i < in.size(); ++i) {
			out[i] = M == Mode::COPY ? in[i] : -in[i];
		}
	}
};

/// A scalar processor whose prepare counts the blocks it is called for.
struct CountingPrepare {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		int prepares; ///< How many times prepare has run.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) {
		++s.prepares;
		return {};
	}
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A vector processor whose prepare counts the blocks it is called for.
struct VecCountingPrepare {
	using value_type = q31;
	using step_type = Argon<q31>;
	struct Settings {};
	struct Coefficients {};
	struct State {
		int prepares; ///< How many times prepare has run.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) {
		++s.prepares;
		return {};
	}
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                              Argon<q31> x) {
		return x;
	}
};

/// A moded processor accumulating its input, halved or doubled by mode; all modes share the accumulator.
struct ModeSwitch {
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
	struct State {
		q31 acc;
	};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s, q31 x) {
		if constexpr (M == Mode::HALF) {
			s.acc += x >> 1;
		}
		else {
			s.acc += x << 1;
		}
		return s.acc;
	}
};

/// The lanes of a q31 vector, lane 0 first.
std::array<q31, 4> lanes_of(Argon<q31> v) {
	std::array<q31, 4> out{};
	v.StoreTo(out.data());
	return out;
}

/// A vector generator emitting its sample index: lanes of `value + {0, 1, 2, 3}`. Its state advances by a whole
/// vector per step, the partial last one included.
struct VecRampWhole {
	using value_type = q31;
	using step_type = Argon<q31>;
	struct Settings {};
	struct Coefficients {};
	struct State {
		Argon<q31> value; ///< The index of the next step's first sample, in every lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s) {
		const Argon<q31> out = s.value + Argon<q31>::Iota(0);
		s.value = s.value + Argon<q31>{static_cast<q31>(Argon<q31>::lanes)};
		return out;
	}
};

/// VecRampWhole, but its partial last vector advances the state by the active lane count only.
struct VecRampCount {
	using value_type = q31;
	using step_type = Argon<q31>;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {
		Argon<q31> value; ///< The index of the next step's first sample, in every lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& c, const EmptyBlock& b, State& s) {
		return step(c, b, s, ActiveLanes{.n = Argon<q31>::lanes});
	}
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                              ActiveLanes active) {
		const Argon<q31> out = s.value + Argon<q31>::Iota(0);
		s.value = s.value + Argon<q31>{static_cast<q31>(active.n)};
		return out;
	}
};

/// A vector processor adding its sample index to the input; its partial last vector advances by the count.
struct VecOffsetCount {
	using value_type = q31;
	using step_type = Argon<q31>;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {
		Argon<q31> value; ///< The index of the next step's first sample, in every lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& c, const EmptyBlock& b, State& s, Argon<q31> x) {
		return step(c, b, s, x, ActiveLanes{.n = Argon<q31>::lanes});
	}
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                              Argon<q31> x, ActiveLanes active) {
		const Argon<q31> out = x + s.value + Argon<q31>::Iota(0);
		s.value = s.value + Argon<q31>{static_cast<q31>(active.n)};
		return out;
	}
};

/// A vector mixer: the difference of its inputs.
struct VecSubtract {
	using value_type = q31;
	using step_type = Argon<q31>;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                              Argon<q31> a, Argon<q31> b2) {
		return a - b2;
	}
};

/// A scalar stereo processor: swaps the channels and offsets the new right by the frame count so far.
struct StereoSwap {
	using value_type = StereoSample;
	using step_type = StereoSample;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 frames; ///< Frames processed so far.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static StereoSample step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                                StereoSample x) {
		return {.l = x.r, .r = x.l + s.frames++};
	}
};

/// A stereo-vector processor: swaps the channels and adds one to the new right.
struct StereoVecSwap {
	using value_type = StereoSample;
	using step_type = StereoVector;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static StereoVector step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                                StereoVector x) {
		return {.l = x.r, .r = x.l + Argon<q31>{1}};
	}
};

/// A stereo-vector generator emitting its frame index on the left and its negation on the right; its partial last
/// vector advances by the count.
struct StereoVecRampCount {
	using value_type = StereoSample;
	using step_type = StereoVector;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {
		Argon<q31> value; ///< The index of the next step's first frame, in every lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static StereoVector step(const Coefficients& c, const EmptyBlock& b, State& s) {
		return step(c, b, s, ActiveLanes{.n = Argon<q31>::lanes});
	}
	[[gnu::always_inline]] static StereoVector step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                                ActiveLanes active) {
		const Argon<q31> index = s.value + Argon<q31>::Iota(0);
		s.value = s.value + Argon<q31>{static_cast<q31>(active.n)};
		return {.l = index, .r = Argon<q31>{0} - index};
	}
};

/// A moded vector processor with a non-empty Block: adds (or subtracts, by mode) the block's bias and its step
/// count to each vector. prepare counts the blocks and sets the bias from that count and the block length.
struct VecModeSwitch {
	using value_type = q31;
	using step_type = Argon<q31>;
	enum class Mode : uint8_t { ADD, SUBTRACT };
	using Modes = ModeList<Mode::ADD, Mode::SUBTRACT>;
	struct Settings {
		Mode mode;
	};
	struct Coefficients {
		Mode mode;
	};
	struct State {
		q31 blocks; ///< Blocks prepared so far.
		q31 steps;  ///< Vectors stepped so far, the partial last one included.
	};
	struct Block {
		Argon<q31> bias; ///< 1000 × the block's index plus its length, in every lane.
	};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	static Block prepare(const Coefficients& /*c*/, State& s, std::size_t n) {
		return {.bias = Argon<q31>{s.blocks++ * 1000 + static_cast<q31>(n)}};
	}
	template <Mode M>
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const Block& b, State& s, Argon<q31> x) {
		const Argon<q31> offset = b.bias + Argon<q31>{s.steps++};
		if constexpr (M == Mode::ADD) {
			return x + offset;
		}
		else {
			return x - offset;
		}
	}
};

/// A stereo-vector mixer: left of a plus right of b on the left, right of a minus left of b on the right.
struct StereoVecMix {
	using value_type = StereoSample;
	using step_type = StereoVector;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static StereoVector step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                                StereoVector a, StereoVector b2) {
		return {.l = a.l + b2.r, .r = a.r - b2.l};
	}
};

/// A generator whose reset is a quirk: it restarts at 100, not at State{}.
struct ResetQuirk {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 n;
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static void reset(State& s) { s.n = 100; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s) {
		return ++s.n;
	}
};

static_assert(processor<Mono<Gain>>);
static_assert(generator<Mono<Counter>>);
static_assert(mixer<Mono<Add>>);
static_assert(block_processor<Mono<Staged>>);
static_assert(!processor<Mono<Staged>>);
static_assert(processor<Mono<VecOffsetCount>>);
static_assert(generator<Mono<VecRampCount>>);
static_assert(mixer<Mono<VecSubtract>>);
static_assert(processor<Mono<StereoSwap>>);
static_assert(processor<Mono<StereoVecSwap>>);
static_assert(generator<Mono<StereoVecRampCount>>);
static_assert(processor<Mono<VecModeSwitch>>);
static_assert(mixer<Mono<StereoVecMix>>);
static_assert(!std::is_polymorphic_v<Mono<Gain>>);

/// A deterministic test input of @p n samples, distinct per @p seed.
std::vector<q31> input(std::size_t n, q31 seed) {
	std::vector<q31> v(n);
	for (std::size_t i = 0; i < n; ++i) {
		v[i] = seed * static_cast<q31>(i + 1) - 0x01234567;
	}
	return v;
}

/// A deterministic test input of @p n stereo frames.
std::vector<StereoSample> stereo_input(std::size_t n) {
	std::vector<StereoSample> v(n);
	for (std::size_t i = 0; i < n; ++i) {
		v[i] = {.l = static_cast<q31>(i * 1000) + 7, .r = -static_cast<q31>(i * 3) - 11};
	}
	return v;
}

/// The lane-0 value of a ramp kernel's state.
template <typename M>
q31 ramp_value(const M& mono) {
	return lanes_of(mono.state().value)[0];
}

/// The vector at @p offset of @p v: its next four samples, zero past the end, as a partial step loads them.
Argon<q31> vector_at(const std::vector<q31>& v, std::size_t offset) {
	std::array<q31, 4> lanes{};
	for (std::size_t i = 0; i < lanes.size() && offset + i < v.size(); ++i) {
		lanes[i] = v[offset + i];
	}
	return Argon<q31>::Load(lanes.data());
}

/// The vector of frames at @p offset of @p v: its next four frames, zero past the end, one vector per channel.
StereoVector frames_at(const std::vector<StereoSample>& v, std::size_t offset) {
	std::array<q31, 4> l{};
	std::array<q31, 4> r{};
	for (std::size_t i = 0; i < l.size() && offset + i < v.size(); ++i) {
		l[i] = v[offset + i].l;
		r[i] = v[offset + i].r;
	}
	return {.l = Argon<q31>::Load(l.data()), .r = Argon<q31>::Load(r.data())};
}

} // namespace

// clang-format off
describe mono("Mono", ${
	it("equals a hand loop over step at every arity", _ {
		for (const std::size_t n : kLengths) {
			const std::vector<q31> a = input(n, 0x00321fed);
			const std::vector<q31> b = input(n, -0x00054321);

			const Gain::Settings settings{.gain = 0x30000000};
			Mono<Gain> gain{settings};
			std::vector<q31> got(n);
			gain.renderBlock(a, got);
			Gain::State gain_state{};
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(Gain::step(Gain::coefficients(settings), {}, gain_state, a[i]));
			}

			Mono<Counter> counter;
			Counter::State counter_state{};
			for (int block = 0; block < 2; ++block) {
				counter.renderBlock(got);
				for (std::size_t i = 0; i < n; ++i) {
					expect(got[i]).to_equal(Counter::step({}, {}, counter_state));
				}
			}

			Mono<Add> add;
			add.renderBlock(a, b, got);
			Add::State add_state{};
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(Add::step({}, {}, add_state, a[i], b[i]));
			}

			Mono<VecSubtract> subtract;
			subtract.renderBlock(a, b, got);
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(a[i] - b[i]);
			}

			const std::vector<StereoSample> frames = stereo_input(n);
			std::vector<StereoSample> stereo_got(n);
			Mono<StereoSwap> swap;
			swap.renderBlock(frames, stereo_got);
			for (std::size_t i = 0; i < n; ++i) {
				expect(stereo_got[i].l).to_equal(frames[i].r);
				expect(stereo_got[i].r).to_equal(frames[i].l + static_cast<q31>(i));
			}

			Mono<StereoVecSwap> vec_swap;
			vec_swap.renderBlock(frames, stereo_got);
			for (std::size_t i = 0; i < n; ++i) {
				expect(stereo_got[i].l).to_equal(frames[i].r);
				expect(stereo_got[i].r).to_equal(frames[i].l + 1);
			}
		}
	});

	it("renders single samples through the one-sample block", _ {
		Mono<Gain> gain{{.gain = 0x40000000}};
		expect(gain.render(1000)).to_equal(500);
		Mono<Counter> counter;
		std::ignore = counter.render();
		expect(counter.render()).to_equal(2);
		Mono<Add> add;
		expect(add.render(3, 4)).to_equal(7);
		Mono<VecRampCount> ramp;
		expect(ramp.render()).to_equal(0);
		expect(ramp.render()).to_equal(1);
		expect(ramp_value(ramp)).to_equal(2);
	});

	it("runs prepare once per block", _ {
		std::vector<q31> block(13);
		Mono<CountingPrepare> scalar;
		scalar.renderBlock(block, block);
		expect(scalar.state().prepares).to_equal(1);
		scalar.renderBlock(block, block);
		scalar.renderBlock(block, block);
		expect(scalar.state().prepares).to_equal(3);

		Mono<VecCountingPrepare> vector;
		vector.renderBlock(block, block);
		expect(vector.state().prepares).to_equal(1);
		vector.renderBlock(block, block);
		vector.renderBlock(block, block);
		expect(vector.state().prepares).to_equal(3);
	});

	it("keeps state across a mode switch, each block in its own mode", _ {
		const std::vector<q31> first = input(13, 0x00011111);
		const std::vector<q31> second = input(7, -0x00022222);
		Mono<ModeSwitch> mono{{.mode = ModeSwitch::Mode::HALF}};
		std::vector<q31> got_first(first.size());
		std::vector<q31> got_second(second.size());
		mono.renderBlock(first, got_first);
		mono.configure({.mode = ModeSwitch::Mode::DOUBLE});
		mono.renderBlock(second, got_second);

		q31 acc = 0;
		for (std::size_t i = 0; i < first.size(); ++i) {
			acc += first[i] >> 1;
			expect(got_first[i]).to_equal(acc);
		}
		for (std::size_t i = 0; i < second.size(); ++i) {
			acc += second[i] << 1;
			expect(got_second[i]).to_equal(acc);
		}
		expect(mono.state().acc).to_equal(acc);
		expect(mono.coefficients().mode).to_equal(ModeSwitch::Mode::DOUBLE);
	});

	it("advances a partial last vector by a whole vector, or by the count when declared", _ {
		constexpr auto lanes = static_cast<std::size_t>(Argon<q31>::lanes);
		for (std::size_t n = 1; n <= 9; ++n) {
			std::vector<q31> got(n);
			const std::vector<q31> zeros(n);

			Mono<VecRampWhole> whole;
			whole.renderBlock(got);
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(static_cast<q31>(i));
			}
			const auto whole_end = static_cast<q31>(lanes * ((n + lanes - 1) / lanes));
			expect(ramp_value(whole)).to_equal(whole_end);
			expect(lanes_of(whole.state().value)[3]).to_equal(whole_end);

			Mono<VecRampCount> count;
			count.renderBlock(got);
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(static_cast<q31>(i));
			}
			expect(ramp_value(count)).to_equal(static_cast<q31>(n));
			expect(lanes_of(count.state().value)[3]).to_equal(static_cast<q31>(n));
			count.renderBlock(got);
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(static_cast<q31>(n + i));
			}
			expect(ramp_value(count)).to_equal(static_cast<q31>(2 * n));

			Mono<VecOffsetCount> offset;
			offset.renderBlock(zeros, got);
			offset.renderBlock(zeros, got);
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(static_cast<q31>(n + i));
			}
			expect(ramp_value(offset)).to_equal(static_cast<q31>(2 * n));

			Mono<StereoVecRampCount> stereo;
			std::vector<StereoSample> frames(n);
			stereo.renderBlock(frames);
			stereo.renderBlock(frames);
			for (std::size_t i = 0; i < n; ++i) {
				expect(frames[i].l).to_equal(static_cast<q31>(n + i));
				expect(frames[i].r).to_equal(-static_cast<q31>(n + i));
			}
			expect(ramp_value(stereo)).to_equal(static_cast<q31>(2 * n));
		}
	});

	it("renders in place exactly as out of place", _ {
		for (const std::size_t n : kLengths) {
			const std::vector<q31> a = input(n, 0x00321fed);

			std::vector<q31> out(n);
			std::vector<q31> in_place = a;
			Mono<Gain>{{.gain = 0x30000000}}.renderBlock(a, out);
			Mono<Gain> gain{{.gain = 0x30000000}};
			gain.renderBlock(in_place, in_place);
			for (std::size_t i = 0; i < n; ++i) {
				expect(in_place[i]).to_equal(out[i]);
			}

			in_place = a;
			Mono<VecOffsetCount> offset_out;
			Mono<VecOffsetCount> offset_in;
			offset_out.renderBlock(a, out);
			offset_in.renderBlock(in_place, in_place);
			for (std::size_t i = 0; i < n; ++i) {
				expect(in_place[i]).to_equal(out[i]);
			}

			const std::vector<StereoSample> frames = stereo_input(n);
			std::vector<StereoSample> stereo_out(n);
			std::vector<StereoSample> stereo_in = frames;
			Mono<StereoSwap> swap_out;
			Mono<StereoSwap> swap_in;
			swap_out.renderBlock(frames, stereo_out);
			swap_in.renderBlock(stereo_in, stereo_in);
			for (std::size_t i = 0; i < n; ++i) {
				expect(stereo_in[i].l).to_equal(stereo_out[i].l);
				expect(stereo_in[i].r).to_equal(stereo_out[i].r);
			}

			stereo_in = frames;
			Mono<StereoVecSwap> vec_swap;
			vec_swap.renderBlock(frames, stereo_out);
			vec_swap.renderBlock(stereo_in, stereo_in);
			for (std::size_t i = 0; i < n; ++i) {
				expect(stereo_in[i].l).to_equal(stereo_out[i].l);
				expect(stereo_in[i].r).to_equal(stereo_out[i].r);
			}
		}
	});

	it("runs a moded vector kernel's block in its mode, prepare and state carried across a switch", _ {
		constexpr auto lanes = static_cast<std::size_t>(Argon<q31>::lanes);
		for (const std::size_t n : kLengths) {
			Mono<VecModeSwitch> mono{{.mode = VecModeSwitch::Mode::ADD}};
			VecModeSwitch::State s{};
			for (int block = 0; block < 3; ++block) {
				const VecModeSwitch::Mode mode = block == 1 ? VecModeSwitch::Mode::SUBTRACT : VecModeSwitch::Mode::ADD;
				mono.configure({.mode = mode});
				const std::vector<q31> in = input(n, 0x00013579 * (block + 1));
				std::vector<q31> got(n);
				mono.renderBlock(in, got);

				const VecModeSwitch::Coefficients c = VecModeSwitch::coefficients({.mode = mode});
				const VecModeSwitch::Block b = VecModeSwitch::prepare(c, s, n);
				for (std::size_t offset = 0; offset < n; offset += lanes) {
					const Argon<q31> want = mode == VecModeSwitch::Mode::ADD
					                            ? VecModeSwitch::step<VecModeSwitch::Mode::ADD>(c, b, s, vector_at(in, offset))
					                            : VecModeSwitch::step<VecModeSwitch::Mode::SUBTRACT>(c, b, s, vector_at(in, offset));
					const std::array<q31, 4> want_lanes = lanes_of(want);
					for (std::size_t i = 0; i < lanes && offset + i < n; ++i) {
						expect(got[offset + i]).to_equal(want_lanes[i]);
					}
				}
				expect(mono.state().blocks).to_equal(s.blocks);
				expect(mono.state().steps).to_equal(s.steps);
			}
		}
	});

	it("mixes stereo vectors as a hand loop over step", _ {
		constexpr auto lanes = static_cast<std::size_t>(Argon<q31>::lanes);
		for (const std::size_t n : kLengths) {
			const std::vector<StereoSample> a = stereo_input(n);
			std::vector<StereoSample> b(n);
			for (std::size_t i = 0; i < n; ++i) {
				b[i] = {.l = a[i].r * 5 + 3, .r = a[i].l - 0x00100000};
			}
			std::vector<StereoSample> got(n);
			Mono<StereoVecMix> mix;
			mix.renderBlock(a, b, got);
			StereoVecMix::State s{};
			for (std::size_t offset = 0; offset < n; offset += lanes) {
				const StereoVector want = StereoVecMix::step({}, {}, s, frames_at(a, offset), frames_at(b, offset));
				const std::array<q31, 4> want_l = lanes_of(want.l);
				const std::array<q31, 4> want_r = lanes_of(want.r);
				for (std::size_t i = 0; i < lanes && offset + i < n; ++i) {
					expect(got[offset + i].l).to_equal(want_l[i]);
					expect(got[offset + i].r).to_equal(want_r[i]);
				}
			}
		}
	});

	it("resets to State{}, or through the kernel's reset quirk", _ {
		Mono<Counter> counter;
		std::ignore = counter.render();
		std::ignore = counter.render();
		counter.reset();
		expect(counter.state().n).to_equal(0);
		expect(counter.render()).to_equal(1);

		Mono<ResetQuirk> quirk;
		std::ignore = quirk.render();
		quirk.reset();
		expect(quirk.state().n).to_equal(100);
		expect(quirk.render()).to_equal(101);
	});

	it("renders a staged kernel's block through process", _ {
		const std::vector<q31> in{1, 2, 3, 4, 5};
		std::vector<q31> out(in.size());
		Mono<Staged> staged;
		staged.renderBlock(in, out);
		for (std::size_t i = 0; i < in.size(); ++i) {
			expect(out[i]).to_equal(in[in.size() - 1 - i]);
		}

		Mono<ModedStaged> moded{{.mode = ModedStaged::Mode::NEGATE}};
		moded.renderBlock(in, out);
		for (std::size_t i = 0; i < in.size(); ++i) {
			expect(out[i]).to_equal(-in[i]);
		}
		moded.configure({.mode = ModedStaged::Mode::COPY});
		moded.renderBlock(in, out);
		for (std::size_t i = 0; i < in.size(); ++i) {
			expect(out[i]).to_equal(in[i]);
		}
	});

	it("renders a staged kernel in place exactly as out of place", _ {
		for (const std::size_t n : kLengths) {
			const std::vector<q31> a = input(n, 0x00321fed);
			std::vector<q31> out(n);
			std::vector<q31> in_place = a;
			Mono<Staged> staged;
			staged.renderBlock(a, out);
			staged.renderBlock(in_place, in_place);
			for (std::size_t i = 0; i < n; ++i) {
				expect(in_place[i]).to_equal(out[i]);
			}

			in_place = a;
			Mono<ModedStaged> moded{{.mode = ModedStaged::Mode::NEGATE}};
			moded.renderBlock(a, out);
			moded.renderBlock(in_place, in_place);
			for (std::size_t i = 0; i < n; ++i) {
				expect(in_place[i]).to_equal(out[i]);
			}
		}
	});
});

CPPSPEC_MAIN(mono);
