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
#include "dsp-next/core/buffer_view.h"
#include "dsp-next/core/concepts.h"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/fixed_point_neon.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/q31_ops.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/core/stereo_vector.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/filter/shift_tracker.h"

#include "loop_state_kernels.h"

#include <array>
#include <concepts>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

using namespace deluge::dsp;

namespace {

/// The block lengths every hand-loop case runs: below, at and past one vector, and a long odd tail.
constexpr std::array<std::size_t, 8> kLengths{1, 2, 3, 4, 5, 7, 8, 13};

/// The test blocks are raw words; @p v viewed as the typed samples a component takes.
std::span<Q<31>> typed(std::vector<int32_t>& v) {
	return typed_view(std::span{v});
}
/// @copydoc typed(std::vector<int32_t>&)
std::span<const Q<31>> typed(const std::vector<int32_t>& v) {
	return typed_view(std::span{v});
}

/// The lane indices {0, 1, 2, 3} as Q31 words.
QVec<31> lane_indices() {
	return QVec<31>::from_raw(Argon<int32_t>::Iota(0));
}

/// @p n in every lane, as a Q31 word.
QVec<31> splat_word(std::size_t n) {
	return QVec<31>::splat(q31::from_raw(static_cast<int32_t>(n)));
}

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
		return mul_as_wrap<q31>(x, c.gain);
	}
};

/// A generator kernel with events: a state and coefficients to change and read.
struct EventProbe {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 scale;
	};
	struct Coefficients {
		q31 scale;
	};
	struct State {
		q31 n;
	};
	static Coefficients coefficients(const Settings& s) { return {.scale = s.scale}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s) {
		s.n = add_wrap(s.n, q31::from_raw(1));
		return s.n;
	}
	static void bump(State& s, int by) { s.n = add_wrap(s.n, q31::from_raw(by)); }
	static void bump_c(const Coefficients& c, State& s) { s.n = add_wrap(s.n, c.scale); }
	static q31 take(State& s, q31 v) { return std::exchange(s.n, v); }
	static State& self(State& s) { return s; }
	static void by_value(State /*s*/) {}
	static void by_const(const State& /*s*/) {}
};

static_assert(detail::event_takes_state<EventProbe, &EventProbe::bump, int>);
static_assert(detail::event_takes_coefficients<EventProbe, &EventProbe::bump_c>);
static_assert(!detail::event_takes_coefficients<EventProbe, &EventProbe::bump, int>);
static_assert(!detail::event_takes_state<EventProbe, &EventProbe::by_value>);
static_assert(!detail::event_takes_coefficients<EventProbe, &EventProbe::by_value>);
static_assert(!detail::event_takes_state<EventProbe, &EventProbe::by_const>);
static_assert(!detail::event_takes_coefficients<EventProbe, &EventProbe::by_const>);

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
		s.n = add_wrap(s.n, q31::from_raw(1));
		return s.n;
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
		return add_wrap(a, b2);
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
			out[i] = M == Mode::COPY ? in[i] : neg_wrap(in[i]);
		}
	}
};

/// A block-staged kernel whose `process` has a deleted overload taking the coefficients by non-const reference. The
/// const overload is the staged form; the wrapper must call it, not the deleted one.
struct ConstProcess {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 offset;
	};
	struct Coefficients {
		q31 offset;
	};
	struct State {};
	static Coefficients coefficients(const Settings& s) { return {.offset = s.offset}; }
	static void process(const Coefficients& c, State& /*s*/, Signal<q31> in, Buffer<q31> out) {
		for (std::size_t i = 0; i < in.size(); ++i) {
			out[i] = add_wrap(in[i], c.offset);
		}
	}
	static void process(Coefficients& c, State& s, Signal<q31> in, Buffer<q31> out) = delete;
};

/// A moded block-staged kernel whose `process<M>` has a deleted overload taking the coefficients by non-const
/// reference.
struct ModedConstProcess {
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
			out[i] = M == Mode::COPY ? in[i] : neg_wrap(in[i]);
		}
	}
	template <Mode M>
	static void process(Coefficients& c, State& s, Signal<q31> in, Buffer<q31> out) = delete;
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
	using step_type = QVec<31>;
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
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                            QVec<31> x) {
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
			s.acc = add_wrap(s.acc, scale_shr_floor<1>(x));
		}
		else {
			s.acc = add_wrap(s.acc, scale_shl_wrap<1>(x));
		}
		return s.acc;
	}
};

/// A scalar processor whose State is an array no register file holds: a 64-sample ring and its head. Each step
/// feeds the ring from the input and the sample it overwrites, so a lost write-back or a stale copy shows at once.
struct RingCore {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 ring[64];       // NOLINT(cppcoreguidelines-avoid-c-arrays): a trivially copyable State is plain data.
		std::uint32_t head; ///< Index of the next ring slot.
		int prepares;       ///< How many times prepare has run.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) {
		++s.prepares;
		s.head = (s.head + static_cast<std::uint32_t>(s.prepares)) & 63U;
		return {};
	}
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s, q31 x) {
		const auto old = static_cast<std::uint32_t>(s.ring[s.head].raw());
		const auto ux = static_cast<std::uint32_t>(x.raw());
		s.ring[s.head] = q31::from_raw(static_cast<int32_t>((old * 3U) + ux + 1U));
		s.head = (s.head + 5U) & 63U;
		return q31::from_raw(static_cast<int32_t>(old ^ ux));
	}
	static void poke(State& s, q31 v) { s.ring[s.head] = v; }
};

/// RingCore, stepping its member State in place.
struct RingInPlace : RingCore {
	static constexpr bool kStateInPlace = true;
};

/// RingCore, with the in-place declaration spelled with the wrong type.
struct RingWrongType : RingCore {
	static constexpr int kStateInPlace = 1;
};

/// RingCore, declaring the trait false.
struct RingFalse : RingCore {
	static constexpr bool kStateInPlace = false;
};

/// A moded RingCore: the mode picks how the input enters the ring; all modes share the State.
struct ModedRingCore {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { ADD, XOR };
	using Modes = ModeList<Mode::ADD, Mode::XOR>;
	using State = RingCore::State;
	struct Settings {
		Mode mode;
	};
	struct Coefficients {
		Mode mode;
	};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, State& s, std::size_t n) {
		return RingCore::prepare({}, s, n);
	}
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s, q31 x) {
		const auto old = static_cast<std::uint32_t>(s.ring[s.head].raw());
		const auto ux = static_cast<std::uint32_t>(x.raw());
		s.ring[s.head] = q31::from_raw(static_cast<int32_t>(M == Mode::ADD ? old + ux : old ^ (ux * 7U)));
		s.head = (s.head + 3U) & 63U;
		return q31::from_raw(static_cast<int32_t>(old + ux));
	}
	static void poke(State& s, q31 v) { s.ring[s.head] = v; }
};

/// ModedRingCore, stepping its member State in place.
struct ModedRingInPlace : ModedRingCore {
	static constexpr bool kStateInPlace = true;
};

static_assert(detail::state_in_place_v<RingInPlace>);
static_assert(detail::state_in_place_v<ModedRingInPlace>);
static_assert(!detail::state_in_place_v<RingCore>);
static_assert(!detail::state_in_place_v<RingFalse>);
static_assert(!detail::state_in_place_v<RingWrongType>);
static_assert(!detail::state_in_place_v<Gain>);
static_assert(processor<Mono<RingInPlace>> && processor<Mono<ModedRingInPlace>>);
static_assert(processor<Mono<loop_state_test::SplitLoop>> && processor<Mono<loop_state_test::SplitLoopInPlace>>);

/// The lanes of a Q31 vector as raw words, lane 0 first.
std::array<int32_t, 4> lanes_of(QVec<31> v) {
	std::array<int32_t, 4> out{};
	v.raw().StoreTo(out.data());
	return out;
}

/// A vector generator emitting its sample index: lanes of `value + {0, 1, 2, 3}`. Its state advances by a whole
/// vector per step, the partial last one included.
struct VecRampWhole {
	using value_type = q31;
	using step_type = QVec<31>;
	struct Settings {};
	struct Coefficients {};
	struct State {
		QVec<31> value; ///< The index of the next step's first sample, in every lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s) {
		const QVec<31> out = add_wrap(s.value, lane_indices());
		s.value = add_wrap(s.value, splat_word(QVec<31>::kLanes));
		return out;
	}
};

/// VecRampWhole, but its partial last vector advances the state by the active lane count only.
struct VecRampCount {
	using value_type = q31;
	using step_type = QVec<31>;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {
		QVec<31> value; ///< The index of the next step's first sample, in every lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& c, const EmptyBlock& b, State& s) {
		return step(c, b, s, ActiveLanes{.n = QVec<31>::kLanes});
	}
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                            ActiveLanes active) {
		const QVec<31> out = add_wrap(s.value, lane_indices());
		s.value = add_wrap(s.value, splat_word(active.n));
		return out;
	}
};

/// A vector processor adding its sample index to the input; its partial last vector advances by the count.
struct VecOffsetCount {
	using value_type = q31;
	using step_type = QVec<31>;
	static constexpr bool kPartialStepAdvancesByCount = true;
	struct Settings {};
	struct Coefficients {};
	struct State {
		QVec<31> value; ///< The index of the next step's first sample, in every lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& c, const EmptyBlock& b, State& s, QVec<31> x) {
		return step(c, b, s, x, ActiveLanes{.n = QVec<31>::kLanes});
	}
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                            QVec<31> x, ActiveLanes active) {
		const QVec<31> out = add_wrap(add_wrap(x, s.value), lane_indices());
		s.value = add_wrap(s.value, splat_word(active.n));
		return out;
	}
};

/// VecOffsetCount without the count: a stateful vector processor whose partial last vector advances its state by a
/// whole vector, as `Amplitude`'s ramp does.
struct VecOffsetWhole {
	using value_type = q31;
	using step_type = QVec<31>;
	struct Settings {};
	struct Coefficients {};
	struct State {
		QVec<31> value; ///< The index of the next step's first sample, in every lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                            QVec<31> x) {
		const QVec<31> out = add_wrap(add_wrap(x, s.value), lane_indices());
		s.value = add_wrap(s.value, splat_word(QVec<31>::kLanes));
		return out;
	}
};

/// VecOffsetWhole with a chained step of two chains, which counts its calls: Mono must run every whole group of two
/// vectors through it and the rest through step, with the same result as step alone.
struct VecOffsetChained {
	using value_type = q31;
	using step_type = QVec<31>;
	static constexpr std::size_t kChains = 2;
	struct Settings {};
	struct Coefficients {};
	struct State {
		QVec<31> value; ///< The index of the next step's first sample, in every lane.
		int groups;     ///< The chained steps taken.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& /*c*/, const EmptyBlock& b, State& s, QVec<31> x) {
		VecOffsetWhole::State w{.value = s.value};
		const QVec<31> out = VecOffsetWhole::step({}, b, w, x);
		s.value = w.value;
		return out;
	}
	[[gnu::always_inline]] static ChainVectors<QVec<31>, 2> step_chains(const Coefficients& c, const EmptyBlock& b,
	                                                                    State& s, const ChainVectors<QVec<31>, 2>& x) {
		++s.groups;
		const QVec<31> first = step(c, b, s, x.v[0]);
		return {{first, step(c, b, s, x.v[1])}};
	}
};

/// A moded vector mixer: a + b2 (ADD) or a − b2 (SUBTRACT), plus a running offset per vector.
struct VecMixOffset {
	using value_type = q31;
	using step_type = QVec<31>;
	enum class Mode : uint8_t { ADD, SUBTRACT };
	using Modes = ModeList<Mode::ADD, Mode::SUBTRACT>;
	struct Settings {
		Mode mode;
	};
	struct Coefficients {
		Mode mode;
	};
	struct State {
		QVec<31> value; ///< The index of the next step's first sample, in every lane.
		int groups;     ///< The chained steps taken; the plain kernel takes none.
	};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	template <Mode M>
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                            QVec<31> a, QVec<31> b2) {
		const QVec<31> mixed = M == Mode::ADD ? add_wrap(a, b2) : sub_wrap(a, b2);
		const QVec<31> out = add_wrap(add_wrap(mixed, s.value), lane_indices());
		s.value = add_wrap(s.value, splat_word(QVec<31>::kLanes));
		return out;
	}
};

/// VecMixOffset with a chained step of three chains.
struct VecMixOffsetChained : VecMixOffset {
	static constexpr std::size_t kChains = 3;
	using Vectors = ChainVectors<QVec<31>, 3>;
	template <Mode M>
	[[gnu::always_inline]] static Vectors step_chains(const Coefficients& c, const EmptyBlock& b, State& s,
	                                                  const Vectors& a, const Vectors& b2) {
		++s.groups;
		const QVec<31> first = step<M>(c, b, s, a.v[0], b2.v[0]);
		const QVec<31> second = step<M>(c, b, s, a.v[1], b2.v[1]);
		return {{first, second, step<M>(c, b, s, a.v[2], b2.v[2])}};
	}
};

/// A vector mixer: the difference of its inputs.
struct VecSubtract {
	using value_type = q31;
	using step_type = QVec<31>;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                            QVec<31> a, QVec<31> b2) {
		return sub_wrap(a, b2);
	}
};

/// A scalar stereo processor: swaps the channels and offsets the new right by the frame count so far.
struct StereoSwap {
	using value_type = StereoSample;
	using step_type = StereoSample;
	struct Settings {};
	struct Coefficients {};
	struct State {
		int32_t frames; ///< Frames processed so far.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static StereoSample step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                                StereoSample x) {
		return {.l = x.r, .r = Q<31>::from_raw(x.l.raw() + s.frames++)};
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
		return {.l = x.r, .r = add_wrap(x.l, splat_word(1))};
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
		QVec<31> value; ///< The index of the next step's first frame, in every lane.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static StereoVector step(const Coefficients& c, const EmptyBlock& b, State& s) {
		return step(c, b, s, ActiveLanes{.n = QVec<31>::kLanes});
	}
	[[gnu::always_inline]] static StereoVector step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                                ActiveLanes active) {
		const QVec<31> index = add_wrap(s.value, lane_indices());
		s.value = add_wrap(s.value, splat_word(active.n));
		return {.l = index, .r = sub_wrap(splat_word(0), index)};
	}
};

/// A moded vector processor with a non-empty Block: adds (or subtracts, by mode) the block's bias and its step
/// count to each vector. prepare counts the blocks and sets the bias from that count and the block length.
struct VecModeSwitch {
	using value_type = q31;
	using step_type = QVec<31>;
	enum class Mode : uint8_t { ADD, SUBTRACT };
	using Modes = ModeList<Mode::ADD, Mode::SUBTRACT>;
	struct Settings {
		Mode mode;
	};
	struct Coefficients {
		Mode mode;
	};
	struct State {
		int32_t blocks; ///< Blocks prepared so far.
		int32_t steps;  ///< Vectors stepped so far, the partial last one included.
	};
	struct Block {
		QVec<31> bias; ///< 1000 × the block's index plus its length, in every lane.
	};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	static Block prepare(const Coefficients& /*c*/, State& s, std::size_t n) {
		return {.bias = QVec<31>::splat(q31::from_raw((s.blocks++ * 1000) + static_cast<int32_t>(n)))};
	}
	template <Mode M>
	[[gnu::always_inline]] static QVec<31> step(const Coefficients& /*c*/, const Block& b, State& s, QVec<31> x) {
		const QVec<31> offset = add_wrap(b.bias, QVec<31>::splat(q31::from_raw(s.steps++)));
		if constexpr (M == Mode::ADD) {
			return add_wrap(x, offset);
		}
		else {
			return sub_wrap(x, offset);
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
		return {.l = add_wrap(a.l, b2.r), .r = sub_wrap(a.r, b2.l)};
	}
};

/// The moded part of SeriesPoles: a shift tracker at a shift of 4, returning y (TRACK) or x - y (REMOVE).
using Tracker = fixed::ShiftTracker<4>;

/// A one-pole low-pass with a Block, a part of SeriesPoles: y += (x - y) >> 2, offset by an eighth of y at the start
/// of the block.
struct BiasedPole {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 y; ///< The last output, before the bias.
	};
	struct Block {
		q31 bias; ///< An eighth of y at the start of the block.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Block prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) {
		return {.bias = scale_shr_floor<3>(s.y)};
	}
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& b, State& s, q31 x) {
		s.y = add_wrap(s.y, scale_shr_floor<2>(sub_wrap(x, s.y)));
		return add_wrap(s.y, b.bias);
	}
};

/// Tracker then BiasedPole, composed by nesting: the parts' Coefficients, State and Block are its members, its
/// prepare prepares each part, and its step threads the sample through each part's step. Tracker is the one moded
/// part, so its modes are the composite's.
struct SeriesPoles {
	using value_type = q31;
	using step_type = q31;
	using Mode = Tracker::Mode;
	using Modes = Tracker::Modes;
	struct Settings {
		Tracker::Settings first;
		BiasedPole::Settings second;
	};
	struct Coefficients {
		Mode mode; ///< The block's mode: the first part's.
		Tracker::Coefficients first;
		BiasedPole::Coefficients second;
	};
	struct State {
		Tracker::State first;
		BiasedPole::State second;
	};
	struct Block {
		kernel_block_t<Tracker> first;
		kernel_block_t<BiasedPole> second;
	};
	static Coefficients coefficients(const Settings& s) {
		const Tracker::Coefficients first = Tracker::coefficients(s.first);
		return {.mode = first.mode, .first = first, .second = BiasedPole::coefficients(s.second)};
	}
	[[gnu::always_inline]] static Block prepare(const Coefficients& c, State& s, std::size_t n) {
		return {.first = kernel_ops::prepare_block<Tracker>(c.first, s.first, n),
		        .second = kernel_ops::prepare_block<BiasedPole>(c.second, s.second, n)};
	}
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& c, const Block& b, State& s, q31 x) {
		const q31 y = kernel_ops::step_at<Tracker, M>(c.first, b.first, s.first, x);
		return kernel_ops::step_at<BiasedPole, kernel_ops::NoMode::NONE>(c.second, b.second, s.second, y);
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
	static void reset(State& s) { s.n = q31::from_raw(100); }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s) {
		s.n = add_wrap(s.n, q31::from_raw(1));
		return s.n;
	}
};

/// A control kernel: a level falling by its rate per sample, advanced a block at a time. `Control` wraps it; `Mono`
/// and `Stereo` have no loop for it.
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
		s.level = sub_wrap(s.level, q31::from_raw(static_cast<int32_t>(n)));
		return s.level;
	}
};

/// Whether `Mono<K>` names a class: false when K fails the class's constraints.
template <typename K>
concept wraps_in_mono = requires { typename Mono<K>; };

/// Whether `Stereo<K>` names a class: false when K fails the class's constraints.
template <typename K>
concept wraps_in_stereo = requires { typename Stereo<K>; };

static_assert(control_kernel<BlockFall>);
// Stereo takes a processor kernel, and a kernel has one form, so a control kernel fails Stereo's processor_kernel
// constraint; Mono takes any kernel and rejects a control kernel by its own clause.
static_assert(!processor_kernel<BlockFall>);
static_assert(!wraps_in_mono<BlockFall> && !wraps_in_stereo<BlockFall>);
static_assert(wraps_in_mono<Gain> && wraps_in_stereo<Gain>);

static_assert(processor<Mono<Gain>>);
static_assert(generator<Mono<Counter>>);
static_assert(mixer<Mono<Add>>);
static_assert(block_processor<Mono<Staged>>);
static_assert(!processor<Mono<Staged>>);
static_assert(processor<Mono<VecOffsetCount>>);
static_assert(generator<Mono<VecRampCount>>);
static_assert(mixer<Mono<VecSubtract>>);
static_assert(processor<Mono<VecOffsetWhole>>);
static_assert(processor<Mono<StereoSwap>>);
static_assert(processor<Mono<StereoVecSwap>>);
static_assert(generator<Mono<StereoVecRampCount>>);
static_assert(processor<Mono<VecModeSwitch>>);
static_assert(mixer<Mono<StereoVecMix>>);
static_assert(moded_kernel<SeriesPoles> && processor<Mono<SeriesPoles>>);
static_assert(!std::is_polymorphic_v<Mono<Gain>>);

/// A deterministic test input of @p n samples, distinct per @p seed.
std::vector<int32_t> input(std::size_t n, int32_t seed) {
	std::vector<int32_t> v(n);
	for (std::size_t i = 0; i < n; ++i) {
		v[i] = (seed * static_cast<int32_t>(i + 1)) - 0x01234567;
	}
	return v;
}

/// A deterministic test input of @p n stereo frames.
std::vector<StereoSample> stereo_input(std::size_t n) {
	std::vector<StereoSample> v(n);
	for (std::size_t i = 0; i < n; ++i) {
		v[i] = {.l = Q<31>::from_raw(static_cast<int32_t>(i * 1000) + 7),
		        .r = Q<31>::from_raw(-static_cast<int32_t>(i * 3) - 11)};
	}
	return v;
}

/// What a Ring workload leaves behind: every block's output, then the final State.
template <typename K>
struct RingRun {
	std::vector<int32_t> out;
	typename K::State state;
};

/// Runs a fixed workload of @p n-sample blocks on Mono<K>: render, an event, render, reset, render, a mode change
/// where K has modes, render. @p in_place renders each block over its own input.
template <typename K, typename Alt>
RingRun<K> run_ring(std::size_t n, bool in_place, Alt alt) {
	Mono<K> mono;
	RingRun<K> run{};
	const std::vector<int32_t> src = input(n, 0x00765431);
	auto block = [&] {
		std::vector<int32_t> buf = src;
		if (in_place) {
			mono.render_block(typed(buf), typed(buf));
		}
		else {
			std::vector<int32_t> dst(n);
			mono.render_block(typed(buf), typed(dst));
			buf = dst;
		}
		run.out.insert(run.out.end(), buf.begin(), buf.end());
	};
	block();
	block();
	mono.template on<&K::poke>(q31::from_raw(0x13579bdf));
	block();
	mono.reset();
	block();
	alt(mono);
	block();
	block();
	run.state = mono.state();
	return run;
}

/// The outputs and final State of three @p n-sample blocks through Mono<K>: set_level before each block, XOR from
/// the third. @p in_place renders each block over its own input.
struct SplitRun {
	std::vector<int32_t> out;
	loop_state_test::SplitCore::State state;
};

template <typename K>
SplitRun run_split(std::size_t n, bool in_place) {
	Mono<K> mono{{.mode = loop_state_test::Mode::ADD}};
	SplitRun run{};
	for (std::size_t block = 0; block < 3; ++block) {
		if (block == 2) {
			mono.configure({.mode = loop_state_test::Mode::XOR});
		}
		mono.template on<&K::set_level>(q31::from_raw(static_cast<int32_t>((block * 1000) + 7)));
		std::vector<q31> buf = loop_state_test::input(n, block);
		if (in_place) {
			mono.render_block(buf, buf);
		}
		else {
			std::vector<q31> dst(n);
			mono.render_block(buf, dst);
			buf = dst;
		}
		for (const q31 x : buf) {
			run.out.push_back(x.raw());
		}
	}
	run.state = mono.state();
	return run;
}

/// The outputs, as raw Q31 words, and final State of three @p n-sample (or frame) blocks of a vector loop-state test
/// kernel through Mono<K>: set_level before each block. @p in_place renders each block over its own input.
template <typename K>
std::pair<std::vector<int32_t>, std::vector<std::uint32_t>> run_vec_split(std::size_t n, bool in_place) {
	using V = typename K::value_type;
	Mono<K> mono;
	std::vector<int32_t> out;
	for (std::size_t block = 0; block < 3; ++block) {
		mono.template on<&K::set_level>(q31::from_raw(static_cast<int32_t>((block * 1000) + 7)));
		const std::vector<q31> a = loop_state_test::input(n, block);
		const std::vector<q31> b = loop_state_test::input(n, block + 10);
		std::vector<V> buf(n);
		for (std::size_t i = 0; i < n; ++i) {
			if constexpr (std::same_as<V, StereoSample>) {
				buf[i] = {.l = a[i], .r = b[i]};
			}
			else {
				buf[i] = a[i];
			}
		}
		if (in_place) {
			mono.render_block(buf, buf);
		}
		else {
			std::vector<V> dst(n);
			mono.render_block(buf, dst);
			buf = dst;
		}
		for (const V& v : buf) {
			if constexpr (std::same_as<V, StereoSample>) {
				out.push_back(v.l.raw());
				out.push_back(v.r.raw());
			}
			else {
				out.push_back(v.raw());
			}
		}
	}
	return {out, loop_state_test::state_words(mono.state())};
}

/// The lane-0 value of a ramp kernel's state.
template <typename M>
int32_t ramp_value(const M& mono) {
	return lanes_of(mono.state().value)[0];
}

/// The vector at @p offset of @p v: its next four samples, zero past the end, as a partial step loads them.
QVec<31> vector_at(const std::vector<int32_t>& v, std::size_t offset) {
	std::array<int32_t, 4> lanes{};
	for (std::size_t i = 0; i < lanes.size() && offset + i < v.size(); ++i) {
		lanes[i] = v[offset + i];
	}
	return QVec<31>::from_raw(Argon<int32_t>::Load(lanes.data()));
}

/// The vector of frames at @p offset of @p v: its next four frames, zero past the end, one vector per channel.
StereoVector frames_at(const std::vector<StereoSample>& v, std::size_t offset) {
	std::array<int32_t, 4> l{};
	std::array<int32_t, 4> r{};
	for (std::size_t i = 0; i < l.size() && offset + i < v.size(); ++i) {
		l[i] = v[offset + i].l.raw();
		r[i] = v[offset + i].r.raw();
	}
	return {.l = QVec<31>::from_raw(Argon<int32_t>::Load(l.data())),
	        .r = QVec<31>::from_raw(Argon<int32_t>::Load(r.data()))};
}

} // namespace

// clang-format off
describe mono("Mono", ${
	it("equals a hand loop over step at every arity", _ {
		for (const std::size_t n : kLengths) {
			const std::vector<int32_t> a = input(n, 0x00321fed);
			const std::vector<int32_t> b = input(n, -0x00054321);

			const Gain::Settings settings{.gain = q31::from_raw(0x30000000)};
			Mono<Gain> gain{settings};
			std::vector<int32_t> got(n);
			gain.render_block(typed(a), typed(got));
			Gain::State gain_state{};
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(Gain::step(Gain::coefficients(settings), {}, gain_state, q31::from_raw(a[i])).raw());
			}

			Mono<Counter> counter;
			Counter::State counter_state{};
			for (int block = 0; block < 2; ++block) {
				counter.render_block(typed(got));
				for (std::size_t i = 0; i < n; ++i) {
					expect(got[i]).to_equal(Counter::step({}, {}, counter_state).raw());
				}
			}

			Mono<Add> add;
			add.render_block(typed(a), typed(b), typed(got));
			Add::State add_state{};
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(Add::step({}, {}, add_state, q31::from_raw(a[i]), q31::from_raw(b[i])).raw());
			}

			Mono<VecSubtract> subtract;
			subtract.render_block(typed(a), typed(b), typed(got));
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(a[i] - b[i]);
			}

			const std::vector<StereoSample> frames = stereo_input(n);
			std::vector<StereoSample> stereo_got(n);
			Mono<StereoSwap> swap;
			swap.render_block(frames, stereo_got);
			for (std::size_t i = 0; i < n; ++i) {
				expect(stereo_got[i].l.raw()).to_equal(frames[i].r.raw());
				expect(stereo_got[i].r.raw()).to_equal(frames[i].l.raw() + static_cast<int32_t>(i));
			}

			Mono<StereoVecSwap> vec_swap;
			vec_swap.render_block(frames, stereo_got);
			for (std::size_t i = 0; i < n; ++i) {
				expect(stereo_got[i].l.raw()).to_equal(frames[i].r.raw());
				expect(stereo_got[i].r.raw()).to_equal(frames[i].l.raw() + 1);
			}
		}
	});

	it("steps a kernel's state in place exactly as it steps a copy, in and out of place", _ {
		for (std::size_t n = 1; n <= 9; ++n) {
			for (const bool in_place : {false, true}) {
				const auto none = [](auto&) {};
				const auto want = run_ring<RingCore>(n, in_place, none);
				const auto got = run_ring<RingInPlace>(n, in_place, none);
				expect(got.out).to_equal(want.out);
				expect(std::memcmp(&got.state, &want.state, sizeof(RingCore::State))).to_equal(0);
				expect(got.state.prepares).to_equal(3);
			}
		}
	});

	it("steps a moded kernel's state in place across a mode switch", _ {
		for (std::size_t n = 1; n <= 9; ++n) {
			for (const bool in_place : {false, true}) {
				const auto xor_mode = [](auto& mono) { mono.configure({.mode = ModedRingCore::Mode::XOR}); };
				const auto want = run_ring<ModedRingCore>(n, in_place, xor_mode);
				const auto got = run_ring<ModedRingInPlace>(n, in_place, xor_mode);
				expect(got.out).to_equal(want.out);
				expect(std::memcmp(&got.state, &want.state, sizeof(RingCore::State))).to_equal(0);
				expect(got.state.prepares).to_equal(3);
			}
		}
	});

	it("steps only a kernel's loop state, and keeps what prepare and the events wrote outside it", _ {
		for (std::size_t n = 1; n <= 9; ++n) {
			for (const bool in_place : {false, true}) {
				for (const SplitRun& run : {run_split<loop_state_test::SplitLoop>(n, in_place),
				                            run_split<loop_state_test::SplitLoopInPlace>(n, in_place)}) {
					expect(run.state.prepares).to_equal(3U);
					expect(run.state.last_n).to_equal(static_cast<std::uint32_t>(n));
					expect(run.state.level.raw()).to_equal(2007);
				}
			}
		}
	});

	it("renders a kernel declaring kLoopState as the same kernel stepping its whole State, copied or in place", _ {
		for (std::size_t n = 1; n <= 9; ++n) {
			for (const bool in_place : {false, true}) {
				const SplitRun want = run_split<loop_state_test::SplitWhole>(n, in_place);
				for (const SplitRun& got : {run_split<loop_state_test::SplitLoop>(n, in_place),
				                            run_split<loop_state_test::SplitLoopInPlace>(n, in_place),
				                            run_split<loop_state_test::SplitWholeInPlace>(n, in_place)}) {
					expect(got.out).to_equal(want.out);
					expect(loop_state_test::state_words(got.state)).to_equal(loop_state_test::state_words(want.state));
				}
			}
		}
	});

	it("renders a vector or interleaved kernel declaring kLoopState as the same kernel stepping its whole State", _ {
		for (std::size_t n = 1; n <= 9; ++n) {
			for (const bool in_place : {false, true}) {
				const auto vec = run_vec_split<loop_state_test::VecSplitWhole>(n, in_place);
				expect(run_vec_split<loop_state_test::VecSplitLoop>(n, in_place)).to_equal(vec);
				expect(run_vec_split<loop_state_test::VecSplitLoopInPlace>(n, in_place)).to_equal(vec);
				const auto stereo = run_vec_split<loop_state_test::StereoVecSplitWhole>(n, in_place);
				expect(run_vec_split<loop_state_test::StereoVecSplitLoop>(n, in_place)).to_equal(stereo);
				expect(run_vec_split<loop_state_test::StereoVecSplitLoopInPlace>(n, in_place)).to_equal(stereo);
			}
		}
	});

	it("renders single samples through the one-sample block", _ {
		Mono<Gain> gain{{.gain = q31::from_raw(0x40000000)}};
		expect(gain.render(q31::from_raw(1000)).raw()).to_equal(500);
		Mono<Counter> counter;
		std::ignore = counter.render();
		expect(counter.render().raw()).to_equal(2);
		Mono<Add> add;
		expect(add.render(q31::from_raw(3), q31::from_raw(4)).raw()).to_equal(7);
		Mono<VecRampCount> ramp;
		expect(ramp.render().raw()).to_equal(0);
		expect(ramp.render().raw()).to_equal(1);
		expect(ramp_value(ramp)).to_equal(2);
	});

	it("runs prepare once per block", _ {
		std::vector<int32_t> block(13);
		Mono<CountingPrepare> scalar;
		scalar.render_block(typed(block), typed(block));
		expect(scalar.state().prepares).to_equal(1);
		scalar.render_block(typed(block), typed(block));
		scalar.render_block(typed(block), typed(block));
		expect(scalar.state().prepares).to_equal(3);

		Mono<VecCountingPrepare> vector;
		vector.render_block(typed(block), typed(block));
		expect(vector.state().prepares).to_equal(1);
		vector.render_block(typed(block), typed(block));
		vector.render_block(typed(block), typed(block));
		expect(vector.state().prepares).to_equal(3);
	});

	it("keeps state across a mode switch, each block in its own mode", _ {
		const std::vector<int32_t> first = input(13, 0x00011111);
		const std::vector<int32_t> second = input(7, -0x00022222);
		Mono<ModeSwitch> mono{{.mode = ModeSwitch::Mode::HALF}};
		std::vector<int32_t> got_first(first.size());
		std::vector<int32_t> got_second(second.size());
		mono.render_block(typed(first), typed(got_first));
		mono.configure({.mode = ModeSwitch::Mode::DOUBLE});
		mono.render_block(typed(second), typed(got_second));

		int32_t acc = 0;
		for (std::size_t i = 0; i < first.size(); ++i) {
			acc += first[i] >> 1;
			expect(got_first[i]).to_equal(acc);
		}
		for (std::size_t i = 0; i < second.size(); ++i) {
			acc += second[i] << 1;
			expect(got_second[i]).to_equal(acc);
		}
		expect(mono.state().acc.raw()).to_equal(acc);
		expect(mono.coefficients().mode).to_equal(ModeSwitch::Mode::DOUBLE);
	});

	it("advances a partial last vector by a whole vector, or by the count when declared", _ {
		constexpr auto lanes = static_cast<std::size_t>(QVec<31>::kLanes);
		for (std::size_t n = 1; n <= 9; ++n) {
			std::vector<int32_t> got(n);
			const std::vector<int32_t> zeros(n);

			Mono<VecRampWhole> whole;
			whole.render_block(typed(got));
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(static_cast<int32_t>(i));
			}
			const auto whole_end = static_cast<int32_t>(lanes * ((n + lanes - 1) / lanes));
			expect(ramp_value(whole)).to_equal(whole_end);
			expect(lanes_of(whole.state().value)[3]).to_equal(whole_end);

			Mono<VecRampCount> count;
			count.render_block(typed(got));
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(static_cast<int32_t>(i));
			}
			expect(ramp_value(count)).to_equal(static_cast<int32_t>(n));
			expect(lanes_of(count.state().value)[3]).to_equal(static_cast<int32_t>(n));
			count.render_block(typed(got));
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(static_cast<int32_t>(n + i));
			}
			expect(ramp_value(count)).to_equal(static_cast<int32_t>(2 * n));

			Mono<VecOffsetCount> offset;
			offset.render_block(typed(zeros), typed(got));
			offset.render_block(typed(zeros), typed(got));
			for (std::size_t i = 0; i < n; ++i) {
				expect(got[i]).to_equal(static_cast<int32_t>(n + i));
			}
			expect(ramp_value(offset)).to_equal(static_cast<int32_t>(2 * n));

			Mono<StereoVecRampCount> stereo;
			std::vector<StereoSample> frames(n);
			stereo.render_block(frames);
			stereo.render_block(frames);
			for (std::size_t i = 0; i < n; ++i) {
				expect(frames[i].l.raw()).to_equal(static_cast<int32_t>(n + i));
				expect(frames[i].r.raw()).to_equal(-static_cast<int32_t>(n + i));
			}
			expect(ramp_value(stereo)).to_equal(static_cast<int32_t>(2 * n));
		}
	});

	it("renders in place exactly as out of place", _ {
		for (const std::size_t n : kLengths) {
			const std::vector<int32_t> a = input(n, 0x00321fed);

			std::vector<int32_t> out(n);
			std::vector<int32_t> in_place = a;
			Mono<Gain>{{.gain = q31::from_raw(0x30000000)}}.render_block(typed(a), typed(out));
			Mono<Gain> gain{{.gain = q31::from_raw(0x30000000)}};
			gain.render_block(typed(in_place), typed(in_place));
			for (std::size_t i = 0; i < n; ++i) {
				expect(in_place[i]).to_equal(out[i]);
			}

			in_place = a;
			Mono<VecOffsetCount> offset_out;
			Mono<VecOffsetCount> offset_in;
			offset_out.render_block(typed(a), typed(out));
			offset_in.render_block(typed(in_place), typed(in_place));
			for (std::size_t i = 0; i < n; ++i) {
				expect(in_place[i]).to_equal(out[i]);
			}

			const std::vector<StereoSample> frames = stereo_input(n);
			std::vector<StereoSample> stereo_out(n);
			std::vector<StereoSample> stereo_in = frames;
			Mono<StereoSwap> swap_out;
			Mono<StereoSwap> swap_in;
			swap_out.render_block(frames, stereo_out);
			swap_in.render_block(stereo_in, stereo_in);
			for (std::size_t i = 0; i < n; ++i) {
				expect(stereo_in[i].l.raw()).to_equal(stereo_out[i].l.raw());
				expect(stereo_in[i].r.raw()).to_equal(stereo_out[i].r.raw());
			}

			stereo_in = frames;
			Mono<StereoVecSwap> vec_swap;
			vec_swap.render_block(frames, stereo_out);
			vec_swap.render_block(stereo_in, stereo_in);
			for (std::size_t i = 0; i < n; ++i) {
				expect(stereo_in[i].l.raw()).to_equal(stereo_out[i].l.raw());
				expect(stereo_in[i].r.raw()).to_equal(stereo_out[i].r.raw());
			}
		}
	});

	it("runs a moded vector kernel's block in its mode, prepare and state carried across a switch", _ {
		constexpr auto lanes = static_cast<std::size_t>(QVec<31>::kLanes);
		for (const std::size_t n : kLengths) {
			Mono<VecModeSwitch> mono{{.mode = VecModeSwitch::Mode::ADD}};
			VecModeSwitch::State s{};
			for (int block = 0; block < 3; ++block) {
				const VecModeSwitch::Mode mode = block == 1 ? VecModeSwitch::Mode::SUBTRACT : VecModeSwitch::Mode::ADD;
				mono.configure({.mode = mode});
				const std::vector<int32_t> in = input(n, 0x00013579 * (block + 1));
				std::vector<int32_t> got(n);
				mono.render_block(typed(in), typed(got));

				const VecModeSwitch::Coefficients c = VecModeSwitch::coefficients({.mode = mode});
				const VecModeSwitch::Block b = VecModeSwitch::prepare(c, s, n);
				for (std::size_t offset = 0; offset < n; offset += lanes) {
					const QVec<31> want = mode == VecModeSwitch::Mode::ADD
					                            ? VecModeSwitch::step<VecModeSwitch::Mode::ADD>(c, b, s, vector_at(in, offset))
					                            : VecModeSwitch::step<VecModeSwitch::Mode::SUBTRACT>(c, b, s, vector_at(in, offset));
					const std::array<int32_t, 4> want_lanes = lanes_of(want);
					for (std::size_t i = 0; i < lanes && offset + i < n; ++i) {
						expect(got[offset + i]).to_equal(want_lanes[i]);
					}
				}
				expect(mono.state().blocks).to_equal(s.blocks);
				expect(mono.state().steps).to_equal(s.steps);
			}
		}
	});

	it("steps whole groups of a kernel's chains through step_chains and the rest through step", _ {
		constexpr auto lanes = static_cast<std::size_t>(QVec<31>::kLanes);
		for (const std::size_t n : kLengths) {
			Mono<VecOffsetChained> chained;
			Mono<VecOffsetWhole> plain;
			const std::vector<int32_t> in = input(n, 0x00024680);
			std::vector<int32_t> got(n);
			std::vector<int32_t> want(n);
			chained.render_block(typed(in), typed(got));
			plain.render_block(typed(in), typed(want));
			expect(got == want).to_be_true();
			expect(chained.state().groups).to_equal(static_cast<int>(n / (2 * lanes)));
			expect(lanes_of(chained.state().value)).to_equal(lanes_of(plain.state().value));
		}
	});

	it("steps a moded mixer's chains in its mode, in place, as its plain step does", _ {
		constexpr auto lanes = static_cast<std::size_t>(QVec<31>::kLanes);
		for (const std::size_t n : {std::size_t{1}, std::size_t{11}, std::size_t{12}, std::size_t{13},
		                            std::size_t{25}, std::size_t{36}, std::size_t{50}}) {
			for (const VecMixOffset::Mode mode : {VecMixOffset::Mode::ADD, VecMixOffset::Mode::SUBTRACT}) {
				Mono<VecMixOffsetChained> chained{{.mode = mode}};
				Mono<VecMixOffset> plain{{.mode = mode}};
				const std::vector<int32_t> a = input(n, 0x00013579);
				std::vector<int32_t> got = input(n, 0x0002468a); // the output is the second input: in place
				std::vector<int32_t> want = got;
				chained.render_block(typed(a), typed(got), typed(got));
				plain.render_block(typed(a), typed(want), typed(want));
				expect(got == want).to_be_true();
				expect(chained.state().groups).to_equal(static_cast<int>(n / (3 * lanes)));
				expect(lanes_of(chained.state().value)).to_equal(lanes_of(plain.state().value));
			}
		}
	});

	it("mixes stereo vectors as a hand loop over step", _ {
		constexpr auto lanes = static_cast<std::size_t>(QVec<31>::kLanes);
		for (const std::size_t n : kLengths) {
			const std::vector<StereoSample> a = stereo_input(n);
			std::vector<StereoSample> b(n);
			for (std::size_t i = 0; i < n; ++i) {
				b[i] = {.l = Q<31>::from_raw(a[i].r.raw() * 5 + 3), .r = Q<31>::from_raw(a[i].l.raw() - 0x00100000)};
			}
			std::vector<StereoSample> got(n);
			Mono<StereoVecMix> mix;
			mix.render_block(a, b, got);
			StereoVecMix::State s{};
			for (std::size_t offset = 0; offset < n; offset += lanes) {
				const StereoVector want = StereoVecMix::step({}, {}, s, frames_at(a, offset), frames_at(b, offset));
				const std::array<int32_t, 4> want_l = lanes_of(want.l);
				const std::array<int32_t, 4> want_r = lanes_of(want.r);
				for (std::size_t i = 0; i < lanes && offset + i < n; ++i) {
					expect(got[offset + i].l.raw()).to_equal(want_l[i]);
					expect(got[offset + i].r.raw()).to_equal(want_r[i]);
				}
			}
		}
	});

	it("resets to State{}, or through the kernel's reset quirk", _ {
		Mono<Counter> counter;
		std::ignore = counter.render();
		std::ignore = counter.render();
		counter.reset();
		expect(counter.state().n.raw()).to_equal(0);
		expect(counter.render().raw()).to_equal(1);

		Mono<ResetQuirk> quirk;
		std::ignore = quirk.render();
		quirk.reset();
		expect(quirk.state().n.raw()).to_equal(100);
		expect(quirk.render().raw()).to_equal(101);
	});

	it("calls a staged kernel's const-coefficients process, never an overload taking them by non-const reference", _ {
		static_assert(staged_kernel<ConstProcess> && staged_kernel<ModedConstProcess>);
		const std::vector<int32_t> in{1, 2, 3};
		std::vector<int32_t> out(in.size());
		Mono<ConstProcess> offset{{.offset = q31::from_raw(10)}};
		offset.render_block(typed(in), typed(out));
		expect(out).to_equal(std::vector<int32_t>{11, 12, 13});

		Mono<ModedConstProcess> moded{{.mode = ModedConstProcess::Mode::NEGATE}};
		moded.render_block(typed(in), typed(out));
		expect(out).to_equal(std::vector<int32_t>{-1, -2, -3});
	});

	it("renders a staged kernel's block through process", _ {
		const std::vector<int32_t> in{1, 2, 3, 4, 5};
		std::vector<int32_t> out(in.size());
		Mono<Staged> staged;
		staged.render_block(typed(in), typed(out));
		for (std::size_t i = 0; i < in.size(); ++i) {
			expect(out[i]).to_equal(in[in.size() - 1 - i]);
		}

		Mono<ModedStaged> moded{{.mode = ModedStaged::Mode::NEGATE}};
		moded.render_block(typed(in), typed(out));
		for (std::size_t i = 0; i < in.size(); ++i) {
			expect(out[i]).to_equal(-in[i]);
		}
		moded.configure({.mode = ModedStaged::Mode::COPY});
		moded.render_block(typed(in), typed(out));
		for (std::size_t i = 0; i < in.size(); ++i) {
			expect(out[i]).to_equal(in[i]);
		}
	});

	it("mixes in place exactly as out of place, either input the output", _ {
		for (const std::size_t n : kLengths) {
			const std::vector<int32_t> a = input(n, 0x00321fed);
			const std::vector<int32_t> b = input(n, -0x00054321);
			std::vector<int32_t> out(n);
			std::vector<int32_t> in_place(n);

			Mono<Add> add;
			add.render_block(typed(a), typed(b), typed(out));
			in_place = a;
			add.render_block(typed(in_place), typed(b), typed(in_place));
			expect(in_place).to_equal(out);
			in_place = b;
			add.render_block(typed(a), typed(in_place), typed(in_place));
			expect(in_place).to_equal(out);

			Mono<VecSubtract> subtract;
			subtract.render_block(typed(a), typed(b), typed(out));
			in_place = a;
			subtract.render_block(typed(in_place), typed(b), typed(in_place));
			expect(in_place).to_equal(out);
			in_place = b;
			subtract.render_block(typed(a), typed(in_place), typed(in_place));
			expect(in_place).to_equal(out);

			const std::vector<StereoSample> fa = stereo_input(n);
			std::vector<StereoSample> fb(n);
			for (std::size_t i = 0; i < n; ++i) {
				fb[i] = {.l = Q<31>::from_raw(fa[i].r.raw() * 5 + 3), .r = Q<31>::from_raw(fa[i].l.raw() - 0x00100000)};
			}
			std::vector<StereoSample> frames_out(n);
			std::vector<StereoSample> frames_in_place(n);
			Mono<StereoVecMix> mix;
			mix.render_block(fa, fb, frames_out);
			for (const bool alias_a : {true, false}) {
				frames_in_place = alias_a ? fa : fb;
				if (alias_a) {
					mix.render_block(frames_in_place, fb, frames_in_place);
				}
				else {
					mix.render_block(fa, frames_in_place, frames_in_place);
				}
				for (std::size_t i = 0; i < n; ++i) {
					expect(frames_in_place[i].l.raw()).to_equal(frames_out[i].l.raw());
					expect(frames_in_place[i].r.raw()).to_equal(frames_out[i].r.raw());
				}
			}
		}
	});

	it("advances a stateful vector processor without the count by a whole vector per partial step", _ {
		constexpr auto lanes = static_cast<std::size_t>(QVec<31>::kLanes);
		for (std::size_t n = 1; n <= 9; ++n) {
			Mono<VecOffsetWhole> mono;
			VecOffsetWhole::State s{};
			for (int block = 0; block < 2; ++block) {
				const std::vector<int32_t> in = input(n, 0x00024680 * (block + 1));
				std::vector<int32_t> got(n);
				mono.render_block(typed(in), typed(got));
				for (std::size_t offset = 0; offset < n; offset += lanes) {
					const std::array<int32_t, 4> want = lanes_of(VecOffsetWhole::step({}, {}, s, vector_at(in, offset)));
					for (std::size_t i = 0; i < lanes && offset + i < n; ++i) {
						expect(got[offset + i]).to_equal(want[i]);
					}
				}
				const auto whole_end = static_cast<int32_t>(lanes * ((n + lanes - 1) / lanes) * (block + 1));
				expect(ramp_value(mono)).to_equal(whole_end);
				expect(lanes_of(mono.state().value)[3]).to_equal(whole_end);
			}
		}
	});

	it("runs a composed kernel as a hand loop over its parts, across a mode switch", _ {
		for (const std::size_t n : kLengths) {
			Mono<SeriesPoles> mono{{.first = {.mode = Tracker::Mode::TRACK}}};
			Tracker::State first{};
			BiasedPole::State second{};
			for (int block = 0; block < 3; ++block) {
				const Tracker::Mode mode = block == 1 ? Tracker::Mode::REMOVE : Tracker::Mode::TRACK;
				mono.configure({.first = {.mode = mode}});
				const std::vector<int32_t> in = input(n, 0x00135791 * (block + 1));
				std::vector<int32_t> got(n);
				mono.render_block(typed(in), typed(got));

				const Tracker::Coefficients c1 = Tracker::coefficients({.mode = mode});
				const BiasedPole::Block b2 = BiasedPole::prepare({}, second, n);
				for (std::size_t i = 0; i < n; ++i) {
					const q31 y = mode == Tracker::Mode::TRACK
					                  ? Tracker::step<Tracker::Mode::TRACK>(c1, {}, first, Q<31>::from_raw(in[i]))
					                  : Tracker::step<Tracker::Mode::REMOVE>(c1, {}, first, Q<31>::from_raw(in[i]));
					expect(got[i]).to_equal(BiasedPole::step({}, b2, second, y).raw());
				}
				expect(mono.state().first.y.raw()).to_equal(first.y.raw());
				expect(mono.state().second.y.raw()).to_equal(second.y.raw());
			}
		}
	});

	it("renders a staged kernel in place exactly as out of place", _ {
		for (const std::size_t n : kLengths) {
			const std::vector<int32_t> a = input(n, 0x00321fed);
			std::vector<int32_t> out(n);
			std::vector<int32_t> in_place = a;
			Mono<Staged> staged;
			staged.render_block(typed(a), typed(out));
			staged.render_block(typed(in_place), typed(in_place));
			for (std::size_t i = 0; i < n; ++i) {
				expect(in_place[i]).to_equal(out[i]);
			}

			in_place = a;
			Mono<ModedStaged> moded{{.mode = ModedStaged::Mode::NEGATE}};
			moded.render_block(typed(a), typed(out));
			moded.render_block(typed(in_place), typed(in_place));
			for (std::size_t i = 0; i < n; ++i) {
				expect(in_place[i]).to_equal(out[i]);
			}
		}
	});
});

// clang-format off
describe mono_events("Mono::on", ${
	it("runs an event taking the state alone on its own state", _ {
		Mono<EventProbe> probe{{.scale = q31::from_raw(0)}};
		probe.on<&EventProbe::bump>(3);
		expect(probe.state().n.raw()).to_equal(3);
		probe.on<&EventProbe::bump>(4);
		expect(probe.state().n.raw()).to_equal(7);
		expect(probe.render().raw()).to_equal(8);
	});

	it("gives an event the configured coefficients when it takes them", _ {
		Mono<EventProbe> probe{{.scale = q31::from_raw(5)}};
		probe.on<&EventProbe::bump_c>();
		expect(probe.state().n.raw()).to_equal(5);
		probe.configure({.scale = q31::from_raw(9)});
		probe.on<&EventProbe::bump_c>();
		expect(probe.state().n.raw()).to_equal(14);
	});

	it("returns what the event returns, a reference included", _ {
		Mono<EventProbe> probe{{.scale = q31::from_raw(0)}};
		probe.on<&EventProbe::bump>(2);
		static_assert(std::same_as<decltype(probe.on<&EventProbe::bump>(1)), void>);
		static_assert(std::same_as<decltype(probe.on<&EventProbe::take>(q31{})), q31>);
		static_assert(std::same_as<decltype(probe.on<&EventProbe::self>()), EventProbe::State&>);
		expect(probe.on<&EventProbe::take>(q31::from_raw(11)).raw()).to_equal(2);
		expect(probe.state().n.raw()).to_equal(11);
		probe.on<&EventProbe::self>().n = q31::from_raw(40);
		expect(probe.state().n.raw()).to_equal(40);
	});
});
// clang-format on

CPPSPEC_MAIN(mono, mono_events);
