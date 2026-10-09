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
#include "dsp-next/core/frame.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/lanes.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/filter/shift_tracker.h"
#include "dsp-next/fixed/noise/noise.h"
#include "dsp-next/fixed/shaping/antialiased_tanh.h"
#include "util/fixedpoint.h"

#include <array>
#include <bit>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace deluge::dsp;

namespace {

using fixed::AntialiasedTanh;
using fixed::OnePole;
using fixed::SeededNoise;
using Tracker = fixed::ShiftTracker<11>;
/// A tracker at a shift of 1, whose large steps meet the wraps within a few frames.
using FastTracker = fixed::ShiftTracker<1>;

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

/// The one noise stream every lane of a NoisyAdd draws from.
SeededNoise shared_noise{0x5eed};

/// A processor kernel adding one draw of the shared noise stream to each sample.
struct NoisyAdd {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return wrapping_add(x, shared_noise.render());
	}
};

/// How many prepares have run, across every lane and every block.
int prepare_sequence = 0;

/// A pass-through kernel whose prepare counts its calls per lane and records when, in the global sequence, it last ran.
struct CountingPrepare {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		int prepares; ///< How many times this lane's prepare has run.
		int last;     ///< The global sequence number of this lane's last prepare.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) {
		++s.prepares;
		s.last = ++prepare_sequence;
		return {};
	}
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A processor kernel whose Block comes from its lane's State: prepare takes a sixteenth of the running sum, which
/// every step of the block adds to its output, and each step moves the sum by half the input. A lane stepped with
/// another lane's Block gives another lane's offset.
struct StateBiased {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 acc; ///< The running sum of half the input.
	};
	struct Block {
		q31 bias; ///< A sixteenth of the sum at the start of the block.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static Block prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) { return {.bias = s.acc >> 4}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& b, State& s, q31 x) {
		s.acc = wrapping_add(s.acc, x >> 1);
		return wrapping_add(s.acc, b.bias);
	}
};

/// A generator kernel: counts up from 1 by its step, per lane.
struct Counter {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 by; ///< The increment per sample.
	};
	struct Coefficients {
		q31 by; ///< The increment per sample.
	};
	struct State {
		q31 n; ///< The last value.
	};
	static Coefficients coefficients(const Settings& s) { return {.by = s.by}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& s) {
		s.n = wrapping_add(s.n, c.by);
		return s.n;
	}
};

/// A mixer kernel: the running sum of a − b, per lane.
struct RunningDifference {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 acc; ///< The sum so far.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s, q31 a, q31 b) {
		s.acc = wrapping_add(s.acc, wrapping_sub(a, b));
		return s.acc;
	}
};

/// A block-staged kernel: a copy. Lanes has no loop for it.
struct Staged {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static void process(const Coefficients& /*c*/, State& /*s*/, Signal<q31> in, Buffer<q31> out) {
		for (std::size_t i = 0; i < out.size(); ++i) {
			out[i] = in[i];
		}
	}
};

/// A control kernel: a level falling by one per sample. Lanes has no loop for it.
struct BlockFall {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 level; ///< Q1.31.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static q31 advance(const Coefficients& /*c*/, State& s, std::size_t n) {
		s.level -= static_cast<q31>(n);
		return s.level;
	}
};

/// Whether `Lanes<K, N>` names a class: false when K fails the class's constraints.
template <typename K, std::size_t N>
concept wraps_in_lanes = requires { typename Lanes<K, N>; };

static_assert(processor<Lanes<OnePole, 4>>);
static_assert(processor<Lanes<Gain, 1>>);
static_assert(generator<Lanes<Counter, 3>>);
static_assert(mixer<Lanes<RunningDifference, 2>>);
static_assert(processor<Lanes<OnePole, 2, StereoSample>>);
static_assert(std::same_as<Lanes<OnePole, 2, StereoSample>::value_type, StereoSample>);
static_assert(std::same_as<Lanes<OnePole, 4>::value_type, Frame<q31, 4>>);
static_assert(staged_kernel<Staged> && !wraps_in_lanes<Staged, 2>);
static_assert(control_kernel<BlockFall> && !wraps_in_lanes<BlockFall, 2>);
static_assert(wraps_in_lanes<Gain, 2>);
static_assert(!std::is_polymorphic_v<Lanes<OnePole, 2>>);

/// The lane widths every case runs.
template <std::size_t... Ns>
struct Widths {
	/// Run @p body at every width, as `body.template operator()<N>()`.
	template <typename Body>
	static void each(Body&& body) {
		(body.template operator()<Ns>(), ...);
	}
};
using AllWidths = Widths<1, 2, 3, 4, 8>;

/// The block lengths every case runs.
constexpr std::size_t kMaxLength = 9;

/// Whether two states of a kernel are the same bit for bit. An empty State has no value, only a padding byte.
template <typename State>
bool same_state(const State& a, const State& b) {
	if constexpr (std::is_empty_v<State>) {
		return true;
	}
	else {
		static_assert(std::has_unique_object_representations_v<State>, "a State compared by bytes has no padding");
		using Bytes = std::array<unsigned char, sizeof(State)>;
		return std::bit_cast<Bytes>(a) == std::bit_cast<Bytes>(b);
	}
}

/// @p n frames of seeded noise, every lane its own stream, distinct per @p seed.
template <std::size_t N>
std::vector<Frame<q31, N>> noise_frames(std::size_t n, uint32_t seed) {
	std::vector<Frame<q31, N>> v(n);
	for (std::size_t lane = 0; lane < N; ++lane) {
		SeededNoise noise{seed + (static_cast<uint32_t>(lane) * 0x9e3779b9u)};
		for (std::size_t i = 0; i < n; ++i) {
			v[i][lane] = noise.render();
		}
	}
	return v;
}

/// Lane @p lane of @p frames, as a mono block.
template <std::size_t N>
std::vector<q31> lane_of(const std::vector<Frame<q31, N>>& frames, std::size_t lane) {
	std::vector<q31> v(frames.size());
	for (std::size_t i = 0; i < frames.size(); ++i) {
		v[i] = frames[i][lane];
	}
	return v;
}

/// How many frames or samples differ between @p lanes and @p monos, plus how many lanes' states differ.
struct Mismatches {
	std::size_t samples = 0;  ///< Output samples that differ.
	std::size_t states = 0;   ///< Lanes whose final state differs.
	std::size_t compared = 0; ///< Output samples compared.
};

/// Run a processor kernel K as `Lanes<K, N>` and as N `Mono<K>`s over two blocks of every length 1 to 9, configured
/// from @p first for block 0 and @p second for block 1, and count the differences.
template <typename K, std::size_t N>
Mismatches lanes_against_monos(const typename K::Settings& first, const typename K::Settings& second) {
	Mismatches m;
	for (std::size_t n = 1; n <= kMaxLength; ++n) {
		Lanes<K, N> lanes{first};
		std::array<Mono<K>, N> monos;
		for (Mono<K>& mono : monos) {
			mono.configure(first);
		}
		for (uint32_t block = 0; block < 2; ++block) {
			if (block == 1) {
				lanes.configure(second);
				for (Mono<K>& mono : monos) {
					mono.configure(second);
				}
			}
			const std::vector<Frame<q31, N>> in = noise_frames<N>(n, (static_cast<uint32_t>(n) * 977u) + block);
			std::vector<Frame<q31, N>> out(n);
			lanes.renderBlock(in, out);
			for (std::size_t lane = 0; lane < N; ++lane) {
				const std::vector<q31> mono_in = lane_of(in, lane);
				std::vector<q31> mono_out(n);
				monos[lane].renderBlock(mono_in, mono_out);
				for (std::size_t i = 0; i < n; ++i) {
					++m.compared;
					if (out[i][lane] != mono_out[i]) {
						++m.samples;
					}
				}
			}
		}
		for (std::size_t lane = 0; lane < N; ++lane) {
			if (!same_state(lanes.state(lane), monos[lane].state())) {
				++m.states;
			}
		}
	}
	return m;
}

/// The output samples compared over every length 1 to 9 and two blocks, at @p N lanes.
constexpr std::size_t compared_at(std::size_t lanes) {
	return lanes * 2 * (kMaxLength * (kMaxLength + 1) / 2);
}

/// The output samples compared at every width in AllWidths.
constexpr std::size_t kComparedAllWidths =
    compared_at(1) + compared_at(2) + compared_at(3) + compared_at(4) + compared_at(8);

/// Every width's mismatches for kernel K from @p first to @p second, summed.
template <typename K>
Mismatches at_every_width(const typename K::Settings& first, const typename K::Settings& second) {
	Mismatches sum;
	AllWidths::each([&]<std::size_t N>() {
		const Mismatches m = lanes_against_monos<K, N>(first, second);
		sum.samples += m.samples;
		sum.states += m.states;
		sum.compared += m.compared;
	});
	return sum;
}

/// Lanes<K, N> in place against out of place over two blocks of every length 1 to 9; the count of differing samples.
template <typename K, std::size_t N>
std::size_t in_place_mismatches(const typename K::Settings& settings) {
	std::size_t mismatched = 0;
	for (std::size_t n = 1; n <= kMaxLength; ++n) {
		Lanes<K, N> out_of_place{settings};
		Lanes<K, N> in_place{settings};
		for (uint32_t block = 0; block < 2; ++block) {
			const std::vector<Frame<q31, N>> in = noise_frames<N>(n, 0x1234u + block);
			std::vector<Frame<q31, N>> out(n);
			std::vector<Frame<q31, N>> buffer = in;
			out_of_place.renderBlock(in, out);
			in_place.renderBlock(buffer, buffer);
			for (std::size_t i = 0; i < n; ++i) {
				for (std::size_t lane = 0; lane < N; ++lane) {
					mismatched += (buffer[i][lane] != out[i][lane]) ? 1 : 0;
				}
			}
		}
	}
	return mismatched;
}

/// K stepped lane by lane: K with its op-major opt-in withdrawn, so Lanes steps each lane's State in turn.
template <typename K>
struct LaneByLane : K {
	static constexpr bool kLanePackable = false;
};

/// A generator kernel without modes, written over its value type: counts up from 1 by its step, per lane.
struct PackedCounter {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 by; ///< The increment per sample.
	};
	struct Coefficients {
		q31 by; ///< The increment per sample.
	};
	template <class V>
	struct StateT {
		V n{}; ///< The last value.
	};
	using State = StateT<q31>;
	static constexpr bool kLanePackable = true;
	static Coefficients coefficients(const Settings& s) { return {.by = s.by}; }
	template <class V>
	[[gnu::always_inline]] static V step(const Coefficients& c, const EmptyBlock& /*b*/, StateT<V>& s) {
		s.n = wrapping_add(s.n, V{c.by});
		return s.n;
	}
};

/// A mixer kernel without modes, written over its value type: the running sum of a − b, per lane.
struct PackedDifference {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	template <class V>
	struct StateT {
		V acc{}; ///< The sum so far.
	};
	using State = StateT<q31>;
	static constexpr bool kLanePackable = true;
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	template <class V>
	[[gnu::always_inline]] static V step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, StateT<V>& s,
	                                     std::type_identity_t<V> a, std::type_identity_t<V> b) {
		s.acc = wrapping_add(s.acc, wrapping_sub(a, b));
		return s.acc;
	}
};

static_assert(generator_kernel<PackedCounter> && lane_packable<PackedCounter>);
static_assert(mixer_kernel<PackedDifference> && lane_packable<PackedDifference>);

/// A one-pole that declares a prepare: op-major is not offered to a kernel with one.
struct PreparedPole : OnePole {
	static EmptyBlock prepare(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) { return {}; }
};

/// A one-pole that declares a reset quirk: op-major is not offered to a kernel with one.
struct QuirkResetPole : OnePole {
	static void reset(State& s) { s.z = q31{1}; }
};

static_assert(lane_packable<OnePole> && lane_packable<Tracker> && lane_packable<FastTracker>);
static_assert(!lane_packable<Gain> && !lane_packable<AntialiasedTanh> && !lane_packable<StateBiased>);
static_assert(!lane_packable<LaneByLane<OnePole>> && !lane_packable<LaneByLane<Tracker>>);
static_assert(processor_kernel<PreparedPole> && !lane_packable<PreparedPole>);
static_assert(processor_kernel<QuirkResetPole> && !lane_packable<QuirkResetPole>);
static_assert(processor<Lanes<LaneByLane<OnePole>, 4>>);
static_assert(std::same_as<decltype(std::declval<const Lanes<OnePole, 3>&>().state(0)), OnePole::State>,
              "an op-major Lanes reads a lane's State out of the packed state, by value");
static_assert(
    std::same_as<decltype(std::declval<const Lanes<LaneByLane<OnePole>, 3>&>().state(0)), const OnePole::State&>,
    "a lane-by-lane Lanes holds each lane's State");
static_assert(std::same_as<decltype(std::declval<const Stereo<OnePole>&>().left()), OnePole::State>);

/// The extremes of q31.
constexpr q31 kMin = std::numeric_limits<q31>::min();
constexpr q31 kMax = std::numeric_limits<q31>::max();

/// @p n frames for the wrap cases: in each lane INT32_MIN, INT32_MAX and full-scale noise in turn, the pattern shifted
/// by one frame per lane, so x − z, x − y and the outputs leave q31 within a few frames in every lane.
template <std::size_t N>
std::vector<Frame<q31, N>> extreme_frames(std::size_t n, uint32_t seed) {
	std::vector<Frame<q31, N>> v = noise_frames<N>(n, seed);
	for (std::size_t i = 0; i < n; ++i) {
		for (std::size_t lane = 0; lane < N; ++lane) {
			const std::size_t phase = (i + lane) % 3;
			if (phase == 0) {
				v[i][lane] = kMin;
			}
			else if (phase == 1) {
				v[i][lane] = kMax;
			}
		}
	}
	return v;
}

/// Run K op-major (`Lanes<K, N>`) and lane by lane (`Lanes<LaneByLane<K>, N>`) over two blocks of every length 1 to 9,
/// configured from @p first for block 0 and @p second for block 1, on the wrap frames, and count the differences.
template <typename K, std::size_t N>
Mismatches op_major_against_lane_by_lane(const typename K::Settings& first, const typename K::Settings& second) {
	Mismatches m;
	for (std::size_t n = 1; n <= kMaxLength; ++n) {
		Lanes<K, N> op_major{first};
		Lanes<LaneByLane<K>, N> lane_by_lane{first};
		for (uint32_t block = 0; block < 2; ++block) {
			if (block == 1) {
				op_major.configure(second);
				lane_by_lane.configure(second);
			}
			const std::vector<Frame<q31, N>> in = extreme_frames<N>(n, (static_cast<uint32_t>(n) * 131u) + block);
			std::vector<Frame<q31, N>> packed_out(n);
			std::vector<Frame<q31, N>> lane_out(n);
			op_major.renderBlock(in, packed_out);
			lane_by_lane.renderBlock(in, lane_out);
			for (std::size_t i = 0; i < n; ++i) {
				for (std::size_t lane = 0; lane < N; ++lane) {
					++m.compared;
					m.samples += packed_out[i][lane] != lane_out[i][lane] ? 1 : 0;
				}
			}
		}
		for (std::size_t lane = 0; lane < N; ++lane) {
			m.states += same_state(op_major.state(lane), lane_by_lane.state(lane)) ? 0 : 1;
		}
	}
	return m;
}

/// Every width's op-major mismatches for kernel K from @p first to @p second, summed.
template <typename K>
Mismatches op_major_at_every_width(const typename K::Settings& first, const typename K::Settings& second) {
	Mismatches sum;
	AllWidths::each([&]<std::size_t N>() {
		const Mismatches m = op_major_against_lane_by_lane<K, N>(first, second);
		sum.samples += m.samples;
		sum.states += m.states;
		sum.compared += m.compared;
	});
	return sum;
}

/// Whether @p a − @p b leaves q31.
bool difference_overflows(q31 a, q31 b) {
	const int64_t d = int64_t{a} - int64_t{b};
	return d < int64_t{kMin} || d > int64_t{kMax};
}

/// A long run of the wrap frames at N = 4, op-major against lane by lane, one frame at a time. Counts the output
/// samples that differ and the steps whose x − @p memory (read from the lane-by-lane state before the step) leaves q31.
template <typename K, typename Memory>
std::pair<std::size_t, std::size_t> long_wrap_run(const typename K::Settings& settings, Memory memory) {
	constexpr std::size_t kFrames = 300;
	const std::vector<Frame<q31, 4>> in = extreme_frames<4>(kFrames, 0xE7E7u);
	Lanes<K, 4> op_major{settings};
	Lanes<LaneByLane<K>, 4> lane_by_lane{settings};
	std::size_t mismatched = 0;
	std::size_t wraps = 0;
	for (const Frame<q31, 4>& frame : in) {
		for (std::size_t lane = 0; lane < 4; ++lane) {
			wraps += difference_overflows(frame[lane], memory(lane_by_lane.state(lane))) ? 1 : 0;
		}
		const Frame<q31, 4> packed = op_major.render(frame);
		const Frame<q31, 4> stepped = lane_by_lane.render(frame);
		for (std::size_t lane = 0; lane < 4; ++lane) {
			mismatched += packed[lane] != stepped[lane] ? 1 : 0;
		}
	}
	return {mismatched, wraps};
}

} // namespace

// clang-format off
describe lanes_spec("Lanes<K, N>", ${
	it("equals N Monos of a gain, bit for bit", _ {
		const Mismatches m = at_every_width<Gain>({.gain = 0x30000000}, {.gain = 0x7fffffff});
		expect(m.samples).to_equal(std::size_t{0});
		expect(m.states).to_equal(std::size_t{0});
		expect(m.compared).to_equal(kComparedAllWidths);
	});

	it("equals N Monos of a one-pole in each mode, state included", _ {
		for (const OnePole::Mode mode : {OnePole::Mode::LOW_PASS, OnePole::Mode::HIGH_PASS, OnePole::Mode::ALL_PASS}) {
			const Mismatches m = at_every_width<OnePole>({.moveability = 0x12345678, .mode = mode},
			                                             {.moveability = 0x7fffffff, .mode = mode});
			expect(m.samples).to_equal(std::size_t{0});
			expect(m.states).to_equal(std::size_t{0});
			expect(m.compared).to_equal(kComparedAllWidths);
		}
	});

	it("equals N Monos of a shift tracker in each mode, state included", _ {
		for (const Tracker::Mode mode : {Tracker::Mode::TRACK, Tracker::Mode::REMOVE}) {
			const Mismatches m = at_every_width<Tracker>({.mode = mode}, {.mode = mode});
			expect(m.samples).to_equal(std::size_t{0});
			expect(m.states).to_equal(std::size_t{0});
			expect(m.compared).to_equal(kComparedAllWidths);
		}
	});

	it("equals N Monos of the anti-aliased tanh, state included", _ {
		const Mismatches m = at_every_width<AntialiasedTanh>({.drive = 3}, {.drive = 20});
		expect(m.samples).to_equal(std::size_t{0});
		expect(m.states).to_equal(std::size_t{0});
		expect(m.compared).to_equal(kComparedAllWidths);
	});

	it("steps each lane with the Block its own State prepared", _ {
		const Mismatches m = at_every_width<StateBiased>({}, {});
		expect(m.samples).to_equal(std::size_t{0});
		expect(m.states).to_equal(std::size_t{0});
		expect(m.compared).to_equal(kComparedAllWidths);
	});

	it("switches mode between blocks, every lane's state carried", _ {
		const Mismatches lp_to_hp = at_every_width<OnePole>({.moveability = 0x23456789, .mode = OnePole::Mode::LOW_PASS},
		                                                    {.moveability = 0x23456789, .mode = OnePole::Mode::HIGH_PASS});
		const Mismatches hp_to_ap = at_every_width<OnePole>({.moveability = 0x0abcdef0, .mode = OnePole::Mode::HIGH_PASS},
		                                                    {.moveability = 0x0abcdef0, .mode = OnePole::Mode::ALL_PASS});
		const Mismatches track_to_remove = at_every_width<Tracker>({.mode = Tracker::Mode::TRACK},
		                                                           {.mode = Tracker::Mode::REMOVE});
		for (const Mismatches& m : {lp_to_hp, hp_to_ap, track_to_remove}) {
			expect(m.samples).to_equal(std::size_t{0});
			expect(m.states).to_equal(std::size_t{0});
			expect(m.compared).to_equal(kComparedAllWidths);
		}

		Lanes<OnePole, 2> lanes{{.moveability = 0x23456789, .mode = OnePole::Mode::LOW_PASS}};
		lanes.configure({.moveability = 0x23456789, .mode = OnePole::Mode::ALL_PASS});
		expect(lanes.coefficients().mode).to_equal(OnePole::Mode::ALL_PASS);
	});

	it("prepares each lane once per block, in lane order", _ {
		AllWidths::each([&]<std::size_t N>() {
			Lanes<CountingPrepare, N> lanes;
			for (int block = 1; block <= 3; ++block) {
				const int before = prepare_sequence;
				std::vector<Frame<q31, N>> frames(static_cast<std::size_t>(block) * 4);
				lanes.renderBlock(frames, frames);
				expect(prepare_sequence - before).to_equal(static_cast<int>(N));
				for (std::size_t lane = 0; lane < N; ++lane) {
					expect(lanes.state(lane).prepares).to_equal(block);
					expect(lanes.state(lane).last).to_equal(before + static_cast<int>(lane) + 1);
				}
			}
			std::ignore = lanes.render(Frame<q31, N>{});
			for (std::size_t lane = 0; lane < N; ++lane) {
				expect(lanes.state(lane).prepares).to_equal(4);
			}
		});
	});

	it("draws noise for lane 0, then 1, and on, in every frame", _ {
		AllWidths::each([&]<std::size_t N>() {
			SeededNoise expected{shared_noise.state()};
			Lanes<NoisyAdd, N> lanes;
			for (std::size_t n = 1; n <= kMaxLength; ++n) {
				std::vector<Frame<q31, N>> frames(n);
				lanes.renderBlock(frames, frames);
				for (const Frame<q31, N>& frame : frames) {
					for (std::size_t lane = 0; lane < N; ++lane) {
						expect(frame[lane]).to_equal(expected.render());
					}
				}
			}
			const Frame<q31, N> one = lanes.render(Frame<q31, N>{});
			for (std::size_t lane = 0; lane < N; ++lane) {
				expect(one[lane]).to_equal(expected.render());
			}
		});
	});

	it("renders in place exactly as out of place", _ {
		AllWidths::each([&]<std::size_t N>() {
			expect(in_place_mismatches<Gain, N>({.gain = 0x40000000})).to_equal(std::size_t{0});
			expect(in_place_mismatches<OnePole, N>({.moveability = 0x12345678, .mode = OnePole::Mode::ALL_PASS}))
			    .to_equal(std::size_t{0});
			expect(in_place_mismatches<Tracker, N>({.mode = Tracker::Mode::REMOVE})).to_equal(std::size_t{0});
			expect(in_place_mismatches<AntialiasedTanh, N>({.drive = 5})).to_equal(std::size_t{0});
		});
	});

	it("renders every frame of a block and nothing past it", _ {
		AllWidths::each([&]<std::size_t N>() {
			for (std::size_t n = 1; n <= kMaxLength; ++n) {
				const std::vector<Frame<q31, N>> in = noise_frames<N>(n + 1, 0x777u);
				Frame<q31, N> sentinel{};
				for (std::size_t lane = 0; lane < N; ++lane) {
					sentinel[lane] = 77 + static_cast<q31>(lane);
				}
				std::vector<Frame<q31, N>> out(n + 1, sentinel);
				Lanes<Gain, N> lanes{{.gain = 0x30000000}};
				lanes.renderBlock(std::span{in}.first(n), std::span{out}.first(n));
				const Gain::Coefficients c = Gain::coefficients({.gain = 0x30000000});
				Gain::State s{};
				for (std::size_t i = 0; i < n; ++i) {
					for (std::size_t lane = 0; lane < N; ++lane) {
						expect(out[i][lane]).to_equal(Gain::step(c, {}, s, in[i][lane]));
					}
				}
				for (std::size_t lane = 0; lane < N; ++lane) {
					expect(out[n][lane]).to_equal(sentinel[lane]);
				}
			}
		});
	});

	it("generates N streams, each its own Mono", _ {
		AllWidths::each([&]<std::size_t N>() {
			Lanes<Counter, N> lanes{{.by = 0x01010101}};
			Mono<Counter> mono{{.by = 0x01010101}};
			for (std::size_t n = 1; n <= kMaxLength; ++n) {
				std::vector<Frame<q31, N>> out(n);
				lanes.renderBlock(out);
				std::vector<q31> expected(n);
				mono.renderBlock(expected);
				for (std::size_t i = 0; i < n; ++i) {
					for (std::size_t lane = 0; lane < N; ++lane) {
						expect(out[i][lane]).to_equal(expected[i]);
					}
				}
			}
			const Frame<q31, N> one = lanes.render();
			const q31 expected_one = mono.render();
			for (std::size_t lane = 0; lane < N; ++lane) {
				expect(one[lane]).to_equal(expected_one);
				expect(lanes.state(lane).n).to_equal(mono.state().n);
			}
			lanes.reset();
			for (std::size_t lane = 0; lane < N; ++lane) {
				expect(lanes.state(lane).n).to_equal(q31{0});
			}
		});
	});

	it("mixes two frame blocks lane by lane, either of them in place", _ {
		AllWidths::each([&]<std::size_t N>() {
			for (std::size_t n = 1; n <= kMaxLength; ++n) {
				const std::vector<Frame<q31, N>> a = noise_frames<N>(n, 0xa000u);
				const std::vector<Frame<q31, N>> b = noise_frames<N>(n, 0xb000u);
				Lanes<RunningDifference, N> lanes;
				Lanes<RunningDifference, N> over_a;
				Lanes<RunningDifference, N> over_b;
				std::array<Mono<RunningDifference>, N> monos;
				std::vector<Frame<q31, N>> out(n);
				std::vector<Frame<q31, N>> in_a = a;
				std::vector<Frame<q31, N>> in_b = b;
				lanes.renderBlock(a, b, out);
				over_a.renderBlock(in_a, b, in_a);
				over_b.renderBlock(a, in_b, in_b);
				for (std::size_t lane = 0; lane < N; ++lane) {
					const std::vector<q31> mono_a = lane_of(a, lane);
					const std::vector<q31> mono_b = lane_of(b, lane);
					std::vector<q31> mono_out(n);
					monos[lane].renderBlock(mono_a, mono_b, mono_out);
					for (std::size_t i = 0; i < n; ++i) {
						expect(out[i][lane]).to_equal(mono_out[i]);
						expect(in_a[i][lane]).to_equal(mono_out[i]);
						expect(in_b[i][lane]).to_equal(mono_out[i]);
					}
					expect(lanes.state(lane).acc).to_equal(monos[lane].state().acc);
				}
				const Frame<q31, N> one = lanes.render(a[0], b[0]);
				for (std::size_t lane = 0; lane < N; ++lane) {
					expect(one[lane]).to_equal(monos[lane].render(a[0][lane], b[0][lane]));
				}
			}
		});
	});

	it("resets every lane", _ {
		Lanes<OnePole, 3> lanes{{.moveability = 0x40000000, .mode = OnePole::Mode::LOW_PASS}};
		std::vector<Frame<q31, 3>> frames = noise_frames<3>(5, 0x42u);
		lanes.renderBlock(frames, frames);
		for (std::size_t lane = 0; lane < 3; ++lane) {
			expect(lanes.state(lane).z).not_().to_equal(q31{0});
		}
		lanes.reset();
		for (std::size_t lane = 0; lane < 3; ++lane) {
			expect(lanes.state(lane).z).to_equal(q31{0});
		}
	});
	it("equals N Monos of a one-pole and a shift tracker stepped lane by lane, state included", _ {
		const Mismatches pole = at_every_width<LaneByLane<OnePole>>(
		    {.moveability = 0x12345678, .mode = OnePole::Mode::LOW_PASS},
		    {.moveability = 0x7fffffff, .mode = OnePole::Mode::ALL_PASS});
		const Mismatches tracker =
		    at_every_width<LaneByLane<Tracker>>({.mode = Tracker::Mode::TRACK}, {.mode = Tracker::Mode::REMOVE});
		for (const Mismatches& m : {pole, tracker}) {
			expect(m.samples).to_equal(std::size_t{0});
			expect(m.states).to_equal(std::size_t{0});
			expect(m.compared).to_equal(kComparedAllWidths);
		}
	});

	it("steps a one-pole op-major exactly as lane by lane, in every mode and across mode switches, at the extremes", _ {
		using Mode = OnePole::Mode;
		for (const Mode first : {Mode::LOW_PASS, Mode::HIGH_PASS, Mode::ALL_PASS}) {
			for (const Mode second : {Mode::LOW_PASS, Mode::HIGH_PASS, Mode::ALL_PASS}) {
				const Mismatches m = op_major_at_every_width<OnePole>({.moveability = 0x7fffffff, .mode = first},
				                                                      {.moveability = 0x12345678, .mode = second});
				expect(m.samples).to_equal(std::size_t{0});
				expect(m.states).to_equal(std::size_t{0});
				expect(m.compared).to_equal(kComparedAllWidths);
			}
		}
	});

	it("steps a shift tracker op-major exactly as lane by lane, in both modes and across switches, at the extremes", _ {
		using Mode = Tracker::Mode;
		for (const Mode first : {Mode::TRACK, Mode::REMOVE}) {
			for (const Mode second : {Mode::TRACK, Mode::REMOVE}) {
				const Mismatches m = op_major_at_every_width<Tracker>({.mode = first}, {.mode = second});
				expect(m.samples).to_equal(std::size_t{0});
				expect(m.states).to_equal(std::size_t{0});
				expect(m.compared).to_equal(kComparedAllWidths);
			}
		}
		const Mismatches fast = op_major_at_every_width<FastTracker>({.mode = FastTracker::Mode::TRACK},
		                                                             {.mode = FastTracker::Mode::REMOVE});
		expect(fast.samples).to_equal(std::size_t{0});
		expect(fast.states).to_equal(std::size_t{0});
	});

	it("meets the wraps it is checked on, and steps them op-major exactly as lane by lane", _ {
		const auto [pole_mismatched, pole_wraps] = long_wrap_run<OnePole>(
		    {.moveability = 0x7fffffff, .mode = OnePole::Mode::ALL_PASS}, [](const OnePole::State& s) { return s.z; });
		expect(pole_mismatched).to_equal(std::size_t{0});
		expect(pole_wraps).to_be_greater_than(std::size_t{100});

		const auto [tracker_mismatched, tracker_wraps] = long_wrap_run<FastTracker>(
		    {.mode = FastTracker::Mode::REMOVE}, [](const FastTracker::State& s) { return s.y; });
		expect(tracker_mismatched).to_equal(std::size_t{0});
		expect(tracker_wraps).to_be_greater_than(std::size_t{100});
	});

	it("generates and mixes op-major exactly as lane by lane", _ {
		AllWidths::each([&]<std::size_t N>() {
			for (std::size_t n = 1; n <= kMaxLength; ++n) {
				Lanes<PackedCounter, N> counter{{.by = 0x7f010203}};
				Lanes<LaneByLane<PackedCounter>, N> counter_by_lane{{.by = 0x7f010203}};
				Lanes<PackedDifference, N> difference;
				Lanes<LaneByLane<PackedDifference>, N> difference_by_lane;
				for (uint32_t block = 0; block < 2; ++block) {
					std::vector<Frame<q31, N>> generated(n);
					std::vector<Frame<q31, N>> generated_by_lane(n);
					counter.renderBlock(generated);
					counter_by_lane.renderBlock(generated_by_lane);
					const std::vector<Frame<q31, N>> a = extreme_frames<N>(n, 0xa0u + block);
					const std::vector<Frame<q31, N>> b = noise_frames<N>(n, 0xb0u + block);
					std::vector<Frame<q31, N>> mixed(n);
					std::vector<Frame<q31, N>> mixed_by_lane(n);
					difference.renderBlock(a, b, mixed);
					difference_by_lane.renderBlock(a, b, mixed_by_lane);
					for (std::size_t i = 0; i < n; ++i) {
						for (std::size_t lane = 0; lane < N; ++lane) {
							expect(generated[i][lane]).to_equal(generated_by_lane[i][lane]);
							expect(mixed[i][lane]).to_equal(mixed_by_lane[i][lane]);
						}
					}
				}
				for (std::size_t lane = 0; lane < N; ++lane) {
					expect(counter.state(lane).n).to_equal(counter_by_lane.state(lane).n);
					expect(difference.state(lane).acc).to_equal(difference_by_lane.state(lane).acc);
				}
			}
			const Frame<q31, N> one = Lanes<PackedCounter, N>{{.by = 5}}.render();
			for (std::size_t lane = 0; lane < N; ++lane) {
				expect(one[lane]).to_equal(q31{5});
			}
		});
	});

	it("reads each lane's state out of the packed state", _ {
		Lanes<OnePole, 4> lanes{{.moveability = 0x40000000, .mode = OnePole::Mode::HIGH_PASS}};
		Lanes<Tracker, 8> trackers{{.mode = Tracker::Mode::TRACK}};
		std::array<Mono<OnePole>, 4> poles;
		std::array<Mono<Tracker>, 8> monos;
		for (Mono<OnePole>& mono : poles) {
			mono.configure({.moveability = 0x40000000, .mode = OnePole::Mode::HIGH_PASS});
		}
		const std::vector<Frame<q31, 4>> in4 = noise_frames<4>(7, 0x51u);
		const std::vector<Frame<q31, 8>> in8 = noise_frames<8>(7, 0x52u);
		std::vector<Frame<q31, 4>> out4(7);
		std::vector<Frame<q31, 8>> out8(7);
		lanes.renderBlock(in4, out4);
		trackers.renderBlock(in8, out8);
		for (std::size_t lane = 0; lane < 4; ++lane) {
			const std::vector<q31> mono_in = lane_of(in4, lane);
			std::vector<q31> mono_out(7);
			poles[lane].renderBlock(mono_in, mono_out);
			expect(lanes.state(lane).z).to_equal(poles[lane].state().z);
			for (std::size_t other = 0; other < lane; ++other) {
				expect(lanes.state(lane).z).not_().to_equal(lanes.state(other).z);
			}
		}
		for (std::size_t lane = 0; lane < 8; ++lane) {
			const std::vector<q31> mono_in = lane_of(in8, lane);
			std::vector<q31> mono_out(7);
			monos[lane].renderBlock(mono_in, mono_out);
			expect(trackers.state(lane).y).to_equal(monos[lane].state().y);
			for (std::size_t other = 0; other < lane; ++other) {
				expect(trackers.state(lane).y).not_().to_equal(trackers.state(other).y);
			}
		}
	});

	it("steps a stereo one-pole op-major exactly as lane by lane, each channel named", _ {
		Stereo<OnePole> op_major{{.moveability = 0x23456789, .mode = OnePole::Mode::ALL_PASS}};
		Stereo<LaneByLane<OnePole>> lane_by_lane{{.moveability = 0x23456789, .mode = OnePole::Mode::ALL_PASS}};
		std::vector<StereoSample> frames(9);
		SeededNoise noise{0x5151u};
		for (StereoSample& frame : frames) {
			frame.l = noise.render();
			frame.r = noise.render();
		}
		std::vector<StereoSample> packed = frames;
		op_major.renderBlock(packed, packed);
		lane_by_lane.renderBlock(frames, frames);
		for (std::size_t i = 0; i < frames.size(); ++i) {
			expect(packed[i].l).to_equal(frames[i].l);
			expect(packed[i].r).to_equal(frames[i].r);
		}
		expect(op_major.left().z).to_equal(lane_by_lane.left().z);
		expect(op_major.right().z).to_equal(lane_by_lane.right().z);
		expect(op_major.left().z).not_().to_equal(op_major.right().z);
	});
});
// clang-format on

CPPSPEC_MAIN(lanes_spec);
