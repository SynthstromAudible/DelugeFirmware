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
// The component framework steps kernels written over Q<31>: each typed kernel here renders, through Mono, Linked and
// Stereo, what its arithmetic run by hand on the raw words renders, bit for bit, for every block length a loop splits
// differently (whole vectors, chained groups, a partial last vector).
#include "dsp-next/core/concepts.h"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/fixed_point_lanes.h"
#include "dsp-next/core/fixed_point_neon.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/linked.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using namespace deluge::dsp;

namespace {

constexpr Q<31> kGain = Q<31>::from_raw(0x6000'0000);
constexpr Q<31> kBy = Q<31>::from_raw(0x1234'5677);

/// A lane-packable scalar processor over Q<31>: a running sum of the scaled input.
struct TypedAccumulate {
	using value_type = Q<31>;
	using step_type = Q<31>;
	struct Settings {
		Q<31> gain;
	};
	struct Coefficients {
		Q<31> gain;
	};
	template <class V>
	struct StateT {
		V acc{}; ///< The running sum.
	};
	using State = StateT<Q<31>>;
	static constexpr bool kLanePackable = true;
	static Coefficients coefficients(const Settings& s) { return {.gain = s.gain}; }
	template <class V>
	[[gnu::always_inline]] static V step(const Coefficients& c, const EmptyBlock& /*b*/, StateT<V>& s, V x) {
		s.acc = add_wrap(s.acc, mul_doubling_sat(x, c.gain));
		return s.acc;
	}
};

/// TypedAccumulate stepped lane by lane: kStateInPlace keeps it off the op-major path.
struct TypedAccumulateByLane : TypedAccumulate {
	static constexpr bool kStateInPlace = true;
};

/// A vector processor over Q<31> with two chains: the scaled input plus an offset that grows each vector by a step and
/// the sum of the vector's lanes, so a lane read past the end of a block would reach every later output.
struct TypedVecOffset {
	using value_type = Q<31>;
	using step_type = QVec<31>;
	static constexpr std::size_t kChains = 2;
	struct Settings {
		Q<31> gain;
		Q<31> by;
	};
	struct Coefficients {
		Q<31> gain;
		Q<31> by;
	};
	struct State {
		QVec<31> offset; ///< The next vector's offset, in every lane.
		int groups;      ///< The chained steps taken.
	};
	static Coefficients coefficients(const Settings& s) { return {.gain = s.gain, .by = s.by}; }
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& c, const EmptyBlock& /*b*/, State& s, QVec<31> x) {
		const QVec<31> y = add_wrap(mul_doubling_sat(x, c.gain), s.offset);
		s.offset = add_wrap(s.offset, QVec<31>::splat(add_wrap(c.by, sum_wrap(x))));
		return y;
	}
	[[gnu::always_inline]] static ChainVectors<QVec<31>, 2> step_chains(const Coefficients& c, const EmptyBlock& b,
	                                                                    State& s, const ChainVectors<QVec<31>, 2>& x) {
		++s.groups;
		const QVec<31> first = step(c, b, s, x.v[0]);
		return {{first, step(c, b, s, x.v[1])}};
	}
};

/// A Q24 vector processor: the input plus an offset growing by its lanes' sum, as TypedVecOffset at another format.
struct TypedVecOffset24 {
	using value_type = Q<24>;
	using step_type = QVec<24>;
	struct Settings {};
	struct Coefficients {};
	struct State {
		QVec<24> offset; ///< The next vector's offset, in every lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static QVec<24> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                            QVec<24> x) {
		const QVec<24> y = add_wrap(x, s.offset);
		s.offset = add_wrap(s.offset, QVec<24>::splat(sum_wrap(x)));
		return y;
	}
};

static_assert(processor<Mono<TypedAccumulate>> && processor<Mono<TypedVecOffset>> && processor<Mono<TypedVecOffset24>>);
static_assert(lane_packable<TypedAccumulate> && !lane_packable<TypedAccumulateByLane>);
static_assert(processor<Linked<TypedAccumulate, 2>> && processor<Stereo<TypedAccumulate>>);
static_assert(std::same_as<Mono<TypedVecOffset>::value_type, Q<31>>);
static_assert(std::same_as<vector_t<Q<31>>, QVec<31>>);
static_assert(lanes_v<Q<31>> == 4);

constexpr std::array<std::size_t, 6> kLengths{1, 3, 4, 7, 8, 33};
constexpr std::size_t kBlocks = 3;
/// Samples past the end of each block, holding kSentinel: a load reading them changes the output, and a store
/// writing them is seen.
constexpr std::size_t kTail = 4;
constexpr int32_t kSentinel = 0x5a5a'5a5a;

std::uint32_t xorshift(std::uint32_t& s) {
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

/// @p n raw samples, random from @p seed, with the extremes first.
std::vector<int32_t> raw_input(std::size_t n, std::uint32_t seed) {
	std::vector<int32_t> v(n);
	for (std::size_t i = 0; i < n; ++i) {
		v[i] = i == 0 ? INT32_MIN : i == 1 ? INT32_MAX : static_cast<int32_t>(xorshift(seed));
	}
	return v;
}

std::vector<Q<31>> typed(const std::vector<int32_t>& raw) {
	std::vector<Q<31>> v(raw.size());
	for (std::size_t i = 0; i < raw.size(); ++i) {
		v[i] = Q<31>::from_raw(raw[i]);
	}
	return v;
}

std::vector<int32_t> raw(const std::vector<Q<31>>& typed) {
	std::vector<int32_t> v(typed.size());
	for (std::size_t i = 0; i < typed.size(); ++i) {
		v[i] = typed[i].raw();
	}
	return v;
}

/// TypedAccumulate's step run by hand on raw words, one running sum per lane (a channel, for stereo), carried from
/// block to block.
struct AccumulateByHand {
	Q<31> gain;                 ///< The gain each input is scaled by.
	std::array<Q<31>, 2> acc{}; ///< Each lane's running sum.

	int32_t step(std::size_t lane, int32_t x) {
		acc[lane] = add_wrap(acc[lane], mul_doubling_sat(Q<31>::from_raw(x), gain));
		return acc[lane].raw();
	}
	void render_block(std::span<const int32_t> in, std::span<int32_t> out) {
		for (std::size_t i = 0; i < in.size(); ++i) {
			out[i] = step(0, in[i]);
		}
	}
};

/// TypedVecOffset's step run by hand on raw words, four lanes a step, the last partial step's missing lanes zero.
struct VecOffsetByHand {
	Q<31> gain;                        ///< The gain each input is scaled by.
	Q<31> by;                          ///< What the offset grows by each step, beyond the lanes' sum.
	Q<31> offset = Q<31>::from_raw(0); ///< The next step's offset, the same in every lane.

	void render_block(std::span<const int32_t> in, std::span<int32_t> out) {
		const std::size_t n = in.size();
		for (std::size_t at = 0; at < n; at += 4) {
			Q<31> sum = Q<31>::from_raw(0);
			for (std::size_t lane = 0; lane < 4; ++lane) {
				const Q<31> x = Q<31>::from_raw(at + lane < n ? in[at + lane] : 0);
				if (at + lane < n) {
					out[at + lane] = add_wrap(mul_doubling_sat(x, gain), offset).raw();
				}
				sum = add_wrap(sum, x);
			}
			offset = add_wrap(offset, add_wrap(by, sum));
		}
	}
};

/// Every output of kBlocks blocks of @p n samples through @p typed_wrapper and @p raw_wrapper, which must agree, and
/// every typed block's kTail samples past its end, which must be left as they were.
template <class T, class R>
std::size_t mono_mismatches(T typed_wrapper, R raw_wrapper, std::size_t n) {
	std::size_t bad = 0;
	for (std::size_t block = 0; block < kBlocks; ++block) {
		auto in = raw_input(n, 0x9e37'79b9u + static_cast<std::uint32_t>((n * 7) + block));
		std::vector<int32_t> raw_out(n);
		raw_wrapper.render_block(in, raw_out);
		in.resize(n + kTail, kSentinel);
		const auto typed_in = typed(in);
		std::vector<Q<31>> typed_out(n + kTail, Q<31>::from_raw(kSentinel));
		typed_wrapper.render_block(std::span{typed_in}.first(n), std::span{typed_out}.first(n));
		const auto out = raw(typed_out);
		bad += static_cast<std::size_t>(!std::equal(raw_out.begin(), raw_out.end(), out.begin()));
		bad += static_cast<std::size_t>(std::count(out.begin() + static_cast<std::ptrdiff_t>(n), out.end(), kSentinel)
		                                != static_cast<std::ptrdiff_t>(kTail));
	}
	return bad;
}

/// As mono_mismatches, over frames of two lanes.
template <class T>
std::size_t lanes_mismatches(T typed_wrapper, AccumulateByHand by_hand, std::size_t n) {
	std::size_t bad = 0;
	for (std::size_t block = 0; block < kBlocks; ++block) {
		const auto in = raw_input(2 * n, 0x85eb'ca6bu + static_cast<std::uint32_t>((n * 7) + block));
		std::vector<Frame<Q<31>, 2>> typed_in(n), typed_out(n);
		for (std::size_t i = 0; i < n; ++i) {
			typed_in[i] = {{Q<31>::from_raw(in[2 * i]), Q<31>::from_raw(in[(2 * i) + 1])}};
		}
		typed_wrapper.render_block(typed_in, typed_out);
		for (std::size_t i = 0; i < n; ++i) {
			const int32_t want0 = by_hand.step(0, in[2 * i]);
			const int32_t want1 = by_hand.step(1, in[(2 * i) + 1]);
			bad += static_cast<std::size_t>(typed_out[i].lane[0].raw() != want0 || typed_out[i].lane[1].raw() != want1);
		}
	}
	return bad;
}

/// As mono_mismatches, over stereo frames.
template <class T>
std::size_t stereo_mismatches(T typed_wrapper, AccumulateByHand by_hand, std::size_t n) {
	std::size_t bad = 0;
	for (std::size_t block = 0; block < kBlocks; ++block) {
		const auto in = raw_input(2 * n, 0xc2b2'ae35u + static_cast<std::uint32_t>((n * 7) + block));
		std::vector<StereoSample> frames(n), typed_out(n);
		for (std::size_t i = 0; i < n; ++i) {
			frames[i] = {.l = Q<31>::from_raw(in[2 * i]), .r = Q<31>::from_raw(in[(2 * i) + 1])};
		}
		typed_wrapper.render_block(frames, typed_out);
		for (std::size_t i = 0; i < n; ++i) {
			const int32_t want_l = by_hand.step(0, in[2 * i]);
			const int32_t want_r = by_hand.step(1, in[(2 * i) + 1]);
			bad += static_cast<std::size_t>(typed_out[i].l.raw() != want_l || typed_out[i].r.raw() != want_r);
		}
	}
	return bad;
}

} // namespace

// clang-format off
describe typed_kernel("kernels written over Q<31>", ${
	it("render through Mono as their arithmetic on the raw words, scalar and vector, every block length", _ {
		for (std::size_t n : kLengths) {
			expect(mono_mismatches(Mono<TypedAccumulate>{{.gain = kGain}}, AccumulateByHand{.gain = kGain}, n))
			    .to_equal(std::size_t{0});
			expect(mono_mismatches(Mono<TypedVecOffset>{{.gain = kGain, .by = kBy}},
			                       VecOffsetByHand{.gain = kGain, .by = kBy}, n))
			    .to_equal(std::size_t{0});
		}
	});
	it("step a Q24 vector kernel as its arithmetic on the raw words, partial last vectors included", _ {
		for (std::size_t n : kLengths) {
			Mono<TypedVecOffset24> mono;
			std::vector<int32_t> offset(4, 0);
			std::size_t bad = 0;
			for (std::size_t block = 0; block < kBlocks; ++block) {
				const auto in = raw_input(n, 0x1b87'3593u + static_cast<std::uint32_t>((n * 7) + block));
				std::vector<Q<24>> typed_in(n);
				for (std::size_t i = 0; i < n; ++i) {
					typed_in[i] = Q<24>::from_raw(in[i]);
				}
				std::vector<Q<24>> out(n);
				mono.render_block(typed_in, out);
				// By hand: four lanes a step, the last partial step's missing lanes zero.
				for (std::size_t at = 0; at < n; at += 4) {
					std::uint32_t sum = 0;
					for (std::size_t lane = 0; lane < 4; ++lane) {
						const int32_t x = at + lane < n ? in[at + lane] : 0;
						if (at + lane < n) {
							bad += static_cast<std::size_t>(
							    out[at + lane].raw()
							    != static_cast<int32_t>(static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(offset[lane])));
						}
						sum += static_cast<std::uint32_t>(x);
					}
					for (int32_t& o : offset) {
						o = static_cast<int32_t>(static_cast<std::uint32_t>(o) + sum);
					}
				}
			}
			expect(bad).to_equal(std::size_t{0});
		}
	});
	it("step whole groups through both chains, as their arithmetic on the raw words does", _ {
		Mono<TypedVecOffset> typed_wrapper{{.gain = kGain, .by = kBy}};
		expect(mono_mismatches(typed_wrapper, VecOffsetByHand{.gain = kGain, .by = kBy}, 33)).to_equal(std::size_t{0});
		std::vector<Q<31>> typed_out(33);
		typed_wrapper.render_block(typed(raw_input(33, 1)), typed_out);
		expect(typed_wrapper.state().groups).to_equal(4);
	});
	it("render through Linked as their arithmetic on the raw words, op-major and lane by lane", _ {
		for (std::size_t n : kLengths) {
			expect(lanes_mismatches(Linked<TypedAccumulate, 2>{{.gain = kGain}}, AccumulateByHand{.gain = kGain}, n))
			    .to_equal(std::size_t{0});
			expect(lanes_mismatches(Linked<TypedAccumulateByLane, 2>{{.gain = kGain}}, AccumulateByHand{.gain = kGain}, n))
			    .to_equal(std::size_t{0});
		}
	});
	it("render stereo frames through Stereo as their arithmetic on the raw words", _ {
		for (std::size_t n : kLengths) {
			expect(stereo_mismatches(Stereo<TypedAccumulate>{{.gain = kGain}}, AccumulateByHand{.gain = kGain}, n))
			    .to_equal(std::size_t{0});
			expect(stereo_mismatches(Stereo<TypedAccumulateByLane>{{.gain = kGain}}, AccumulateByHand{.gain = kGain}, n))
			    .to_equal(std::size_t{0});
		}
	});
	it("read one lane's state out of a typed packed state", _ {
		Linked<TypedAccumulate, 2> lanes{{.gain = kGain}};
		expect(lanes_mismatches(lanes, AccumulateByHand{.gain = kGain}, 7)).to_equal(std::size_t{0});
		std::vector<Frame<Q<31>, 2>> in(5, Frame<Q<31>, 2>{{kBy, kGain}}), out(5);
		lanes.render_block(in, out);
		expect(lanes.state(1).acc == out[4].lane[1]).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(typed_kernel);
