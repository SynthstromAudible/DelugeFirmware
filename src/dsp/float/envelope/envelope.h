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

#include "dsp/core/kernel.h"
#include "dsp/core/pack.h"
#include "dsp/core/pack_f32.h"
#include "dsp/core/poly_kernel.h"
#include "dsp/core/sample.h"
#include "dsp/float/envelope/curve.h"
#include "dsp/float/math/detail.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <tuple>

/// @file
/// @brief fp::Envelope: a segment-program envelope in float, written once over its value type so Poly runs a copy per
/// lane, each lane in its own stage.
namespace deluge::dsp::fp {

/// @brief The most segments a program holds.
inline constexpr std::size_t kMaxSegments = 8;

/// @brief A segment's `next` (and a program's `release`) when there is none: the envelope ends at 0 (or ignores
/// gate-off).
inline constexpr f32 kOff = -1.f;

/// @brief The threshold of a stage that never ends by itself.
inline constexpr f32 kNever = std::numeric_limits<f32>::infinity();

/// @brief One stage of a program, over V; the flags and the index are 0/1 or whole numbers held in V.
template <class V>
struct Segment {
	V target{};      ///< The level the segment ends at: the value lands on it exactly.
	V aim{};         ///< The level an EXPONENTIAL segment heads for (beyond `target` for an analog-curve attack).
	V rate{};        ///< EXPONENTIAL: the one-pole coefficient. LINEAR: the length in samples.
	V exponential{}; ///< 1 for a one-pole segment, 0 for a linear one.
	V hold{};        ///< 1 to stay (never reached) until a gate-off.
	V next{kOff};    ///< The segment entered on reaching `target`, or kOff.
	V timed{};  ///< 1 for a CURVE or COSINE segment: its level in closed form from the time elapsed, ended by count.
	V cosine{}; ///< 1 for a raised cosine, 0 for a curve; timed segments only.
	V curve{};  ///< A CURVE's octaves, as fp::Curve's k.
	V length{}; ///< A timed segment's length in samples (below 1 counts as 1).
	// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
	static constexpr auto fields =
	    std::tuple{&Segment::target, &Segment::aim,   &Segment::rate,   &Segment::exponential, &Segment::hold,
	               &Segment::next,   &Segment::timed, &Segment::cosine, &Segment::curve,       &Segment::length};
};

/// @brief A program: its segments (the first is entered by a gate-on) and the segment a gate-off enters.
struct Program {
	std::array<Segment<f32>, kMaxSegments> segments{}; ///< The stages.
	f32 release = kOff;                                ///< The segment a gate-off enters, or kOff to ignore it.
};

namespace detail {
/// A one-pole coefficient with time constant @p time samples (at least 1): 1 - e^(-1/time).
[[nodiscard]] inline f32 one_pole_rate(f32 time) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	const double t = time < 1.f ? 1.0 : static_cast<double>(time);
	return static_cast<f32>(1.0 - exp_negative(1.0 / t));
}
/// An exponential segment aiming at @p aim, ending at @p target, entering @p next.
[[nodiscard]] inline Segment<f32> exponential_segment(f32 target, f32 aim, f32 time, f32 next) {
	return {.target = target, .aim = aim, .rate = one_pole_rate(time), .exponential = 1.f, .hold = 0.f, .next = next};
}
/// A linear segment to @p target in @p length samples (at least 1), entering @p next.
[[nodiscard]] inline Segment<f32> linear_segment(f32 target, f32 length, f32 next) {
	return {.target = target,
	        .aim = target,
	        .rate = length < 1.f ? 1.f : length,
	        .exponential = 0.f,
	        .hold = 0.f,
	        .next = next};
}
/// A curve of @p k octaves (fp::Curve) from wherever it starts to @p target in @p length samples, entering @p next.
[[nodiscard]] inline Segment<f32> curve_segment(f32 target, f32 length, f32 k, f32 next) {
	return {.target = target, .aim = target, .rate = 1.f, .next = next, .timed = 1.f, .curve = k, .length = length};
}
/// A raised cosine from wherever it starts to @p target in @p length samples, entering @p next.
[[nodiscard]] inline Segment<f32> cosine_segment(f32 target, f32 length, f32 next) {
	return {.target = target, .aim = target, .rate = 1.f, .next = next, .timed = 1.f, .cosine = 1.f, .length = length};
}
/// A stage that stays at @p level until a gate-off.
[[nodiscard]] inline Segment<f32> hold_segment(f32 level) {
	return {.target = level, .aim = level, .rate = 1.f, .exponential = 0.f, .hold = 1.f, .next = kOff};
}
} // namespace detail

/// @brief Attack, decay, sustain, release: an exponential attack aiming at 1.2 and ending at 1, an exponential decay to
/// @p sustain, a hold there, an exponential release to 0.
/// @param attack Attack time constant, in samples (below 1 counts as 1).
/// @param decay Decay time constant, in samples (below 1 counts as 1).
/// @param sustain The held level.
/// @param release Release time constant, in samples (below 1 counts as 1).
[[nodiscard]] inline Program adsr(f32 attack, f32 decay, f32 sustain, f32 release) {
	Program p;
	p.segments[0] = detail::exponential_segment(1.f, 1.2f, attack, 1.f);
	p.segments[1] = detail::exponential_segment(sustain, sustain, decay, 2.f);
	p.segments[2] = detail::hold_segment(sustain);
	p.segments[3] = detail::exponential_segment(0.f, 0.f, release, kOff);
	p.release = 3.f;
	return p;
}
/// @brief Attack then decay to 0, ignoring gate-off. Times as @ref adsr.
[[nodiscard]] inline Program ad(f32 attack, f32 decay) {
	Program p;
	p.segments[0] = detail::exponential_segment(1.f, 1.2f, attack, 1.f);
	p.segments[1] = detail::exponential_segment(0.f, 0.f, decay, kOff);
	return p;
}
/// @brief Attack, a hold at 1, a release to 0. Times as @ref adsr.
[[nodiscard]] inline Program asr(f32 attack, f32 release) {
	Program p;
	p.segments[0] = detail::exponential_segment(1.f, 1.2f, attack, 1.f);
	p.segments[1] = detail::hold_segment(1.f);
	p.segments[2] = detail::exponential_segment(0.f, 0.f, release, kOff);
	p.release = 2.f;
	return p;
}
/// @brief A DX7-shaped program: linear rises from 0 to @p levels[0], to @p levels[1], to @p levels[2] where it holds;
/// a gate-off falls to @p levels[3], where it stays. @p times are the segments' lengths in samples (>= 1).
[[nodiscard]] inline Program four_segment(std::array<f32, 4> levels, std::array<f32, 4> times) {
	Program p;
	p.segments[0] = detail::linear_segment(levels[0], times[0], 1.f);
	p.segments[1] = detail::linear_segment(levels[1], times[1], 2.f);
	p.segments[2] = detail::linear_segment(levels[2], times[2], 3.f);
	p.segments[3] = detail::hold_segment(levels[2]);
	p.segments[4] = detail::linear_segment(levels[3], times[3], 5.f);
	p.segments[5] = detail::hold_segment(levels[3]);
	p.release = 4.f;
	return p;
}
/// @brief Two linear segments, rising to @p high and falling to @p low in @p time samples each, forever.
[[nodiscard]] inline Program looping(f32 low, f32 high, f32 time) {
	Program p;
	p.segments[0] = detail::linear_segment(high, time, 1.f);
	p.segments[1] = detail::linear_segment(low, time, 0.f);
	return p;
}

/// @brief A segment-program envelope: one step and one end-compare per sample; a stage change, which falls on a
/// different sample in each lane, runs scalar for the lanes that reached.
struct Envelope {
	using value_type = f32; ///< The level, 0 to 1 for the shipped programs.
	using step_type = f32;  ///< One step per sample.

	/// @brief What the owner sets.
	struct Settings {
		Program program{}; ///< The stages.
	};

	/// @brief The program, over V.
	template <class V>
	struct CoefficientsT {
		std::array<Segment<V>, kMaxSegments> segments{}; ///< The stages.
		V release{kOff};                                 ///< The segment a gate-off enters, or kOff.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&CoefficientsT::segments, &CoefficientsT::release};
	};
	using Coefficients = CoefficientsT<f32>;

	/// @brief The current stage and the level, over V. StateT{} rests at 0.
	/// @details An EXPONENTIAL or LINEAR stage is the affine map `value = b + value·a`, ended when
	/// `(value - threshold)·direction >= 0`. A timed stage (CURVE or COSINE) sits on the identity map (a = 1, b = 0,
	/// threshold +infinity) and computes its level from `start`, `target` and `elapsed / length`, ended when `elapsed`
	/// reaches `length`: a count, which a long stage holds exactly in float where a slope near 1 would not.
	template <class V>
	struct StateT {
		V value{}; ///< The level.
		V a{1.f};  ///< The map's slope: 1 - rate for an EXPONENTIAL stage, 1 for a LINEAR one, a hold or rest.
		V b{};     ///< The map's offset: rate·aim for an EXPONENTIAL stage, the step for a LINEAR one, 0 else.
		V threshold{kNever}; ///< The level that ends the stage: just short of the target for an EXPONENTIAL stage
		                     ///< aiming at it; +infinity for a hold or at rest, which never end by themselves.
		V direction{1.f};    ///< +1 when the stage rises to its threshold, -1 when it falls; +1 for a hold or rest.
		V target{};          ///< Where the stage ends: the value lands on it exactly.
		V next{kOff};        ///< The stage entered at the end of this one.
		V index{kOff};       ///< The stage's segment index, or kOff at rest.
		V start{};           ///< A timed stage's level when it began.
		V elapsed{};         ///< A timed stage's samples so far (fractional after a retime).
		V length{1.f};       ///< A timed stage's length in samples.
		V curve{};           ///< A timed CURVE stage's octaves.
		V timed{};           ///< 1 in a timed stage.
		V cosine{};          ///< 1 in a timed COSINE stage.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields =
		    std::tuple{&StateT::value,  &StateT::a,     &StateT::b,     &StateT::threshold, &StateT::direction,
		               &StateT::target, &StateT::next,  &StateT::index, &StateT::start,     &StateT::elapsed,
		               &StateT::length, &StateT::curve, &StateT::timed, &StateT::cosine};
	};
	using State = StateT<f32>;

	/// @brief The program of @p settings.
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		Coefficients c;
		c.segments = settings.program.segments;
		c.release = settings.program.release;
		return c;
	}

	/// @brief Enters segment @p i of @p c in @p s: its level and rates loaded, the slope set from the current value.
	/// @param land Whether the value first lands exactly on the stage being left (the end of a stage) or is kept (a
	/// gate event).
	/// @details kOff rests at exactly 0.
	static void enter(const Coefficients& c, State& s, f32 i, bool land) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		if (land) {
			s.value = s.target;
		}
		if (i < 0.f) {
			s = State{};
			return;
		}
		const auto k = static_cast<std::size_t>(i);
		assert(k < kMaxSegments);
		const Segment<f32>& g = c.segments[k];
		const bool hold = g.hold > 0.5f;
		const bool exponential = g.exponential > 0.5f && !hold;
		s.target = g.target;
		s.next = g.next;
		s.index = i;
		s.timed = 0.f;
		s.cosine = 0.f;
		if (g.timed > 0.5f && !hold) { // the identity map, never reached by value: the count ends it
			s.a = 1.f;
			s.b = 0.f;
			s.direction = 1.f;
			s.threshold = kNever;
			s.start = s.value;
			s.elapsed = 0.f;
			s.length = g.length < 1.f ? 1.f : g.length;
			s.curve = g.curve;
			s.timed = 1.f;
			s.cosine = g.cosine;
			return;
		}
		if (hold) { // stays where it is, and never reaches
			s.a = 1.f;
			s.b = 0.f;
			s.direction = 1.f;
			s.threshold = kNever;
			return;
		}
		s.direction = g.target >= s.value ? 1.f : -1.f;
		// An exponential stage aiming at its target only approaches it, so it ends within epsilon.
		const f32 epsilon = exponential && g.aim == g.target ? 1e-5f : 0.f;
		s.threshold = g.target - (s.direction * epsilon);
		if (exponential) {
			s.a = 1.f - g.rate;
			s.b = g.rate * g.aim;
		}
		else {
			s.a = 1.f;
			s.b = (g.target - s.value) / (g.rate < 1.f ? 1.f : g.rate); // a length under a sample is one
		}
	}

	/// @brief Starts segment 0 from the current value: a retrigger never jumps.
	static void gate_on(const Coefficients& c, State& s) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		enter(c, s, 0.f, false);
	}
	/// @brief Enters the release segment from the current value; a program without one, or an envelope at rest,
	/// ignores it.
	static void gate_off(const Coefficients& c, State& s) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		if (c.release >= 0.f && s.index >= 0.f) {
			enter(c, s, c.release, false);
		}
	}

	/// @brief A timed stage takes @p length samples (below 1 counts as 1) from now on, keeping the fraction already
	/// elapsed: its level carries on from where it is. Other stages ignore it.
	static void retime(State& s, f32 length) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		if (s.timed > 0.5f) {
			const f32 l = length < 1.f ? 1.f : length;
			s.elapsed = (s.elapsed / s.length) * l;
			s.length = l;
		}
	}
	/// @brief A timed stage heads for @p target from now on, keeping its start: it lands there at its end. Other
	/// stages ignore it.
	static void retarget(State& s, f32 target) {
		if (s.timed > 0.5f) {
			s.target = target;
		}
	}

	/// @brief A timed stage's level at its elapsed fraction.
	[[nodiscard]] static f32 timed_level(const State& s) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const f32 x = s.elapsed / s.length;
		const f32 falling =
		    s.cosine > 0.5f ? 0.5f + (0.5f * std::cos(std::numbers::pi_v<f32> * x)) : curve_falling(s.curve, x);
		return s.target + ((s.start - s.target) * falling);
	}

	/// @brief Advances one copy @p n samples and returns the value after them, as @p n calls of @ref step would.
	/// @details A timed stage moves in closed form: it ends on its exact sample, lands on its target and the rest of
	/// the block runs on in the next stage, through as many stages as end within it. A hold or rest does not move.
	/// EXPONENTIAL and LINEAR stages, which no block-rate owner uses, step sample by sample, so they stay bit-equal to
	/// @ref step.
	static f32 advance_block(const Coefficients& c, State& s, uint32_t n) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		uint32_t left = n;
		while (left > 0) {
			if (s.timed > 0.5f) {
				// The samples to the end: the first count at which elapsed reaches length, as step's +1s find it.
				auto to_end = static_cast<uint32_t>(std::max(1.f, std::ceil(s.length - s.elapsed)));
				while (to_end > 1 && s.elapsed + static_cast<f32>(to_end - 1) >= s.length) {
					--to_end;
				}
				while (s.elapsed + static_cast<f32>(to_end) < s.length) {
					++to_end;
				}
				if (to_end <= left) {
					left -= to_end;
					s.elapsed = s.elapsed + static_cast<f32>(to_end);
					finish(c, s);
				}
				else {
					s.elapsed = s.elapsed + static_cast<f32>(left);
					s.value = timed_level(s);
					left = 0;
				}
			}
			else if (s.threshold == kNever) { // a hold or rest
				break;
			}
			else {
				(void)step(c, EmptyBlock{}, s);
				--left;
			}
		}
		return s.value;
	}

	/// @brief One sample over V: f32 for one copy, Pack<f32, N> for one per lane.
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& c, const EmptyBlock& /*b*/,
	                                                   StateT<V>& s) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		s.value = mul_add(s.b, s.value, s.a);
		const auto reached = ((s.value - s.threshold) * s.direction) >= V{0.f};
		const auto timed = s.timed > V{0.5f};
		if (any(reached) || any(timed)) {
			reach(c, s, reached, timed);
		}
		return s.value;
	}

private:
	/// One copy at the end of a timed stage, some fraction of a sample past it when its length is not whole: lands on
	/// its target and enters the next stage, which, when timed, starts that fraction in, so the stages keep their
	/// lengths end to end instead of each losing the fraction to the sample grid.
	static void finish(const Coefficients& c, State& s) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const f32 over = s.elapsed - s.length;
		enter(c, s, s.next, true);
		if (s.timed > 0.5f && over > 0.f) {
			s.elapsed = over;
			s.value = timed_level(s);
		}
	}
	/// One copy past one sample of a timed stage: its level, or its end when the count runs out.
	static void tick(const Coefficients& c, State& s) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		s.elapsed = s.elapsed + 1.f;
		if (s.elapsed >= s.length) {
			finish(c, s);
		}
		else {
			s.value = timed_level(s);
		}
	}
	/// One copy: its stage ended, or it is timed.
	[[gnu::always_inline]] static void reach(const Coefficients& c, State& s, bool /*reached*/, bool timed) {
		if (timed) {
			tick(c, s);
		}
		else {
			enter(c, s, s.next, true);
		}
	}
	/// Each lane that reached enters its next stage, and each timed lane ticks, scalar, from its own program.
	template <std::size_t N>
	static void reach(const CoefficientsT<Pack<f32, N>>& c, StateT<Pack<f32, N>>& s, Mask<N> reached, Mask<N> timed) {
		for (std::size_t l = 0; l < N; ++l) {
			if (reached.get(l) || timed.get(l)) {
				State lane = get_lane<State>(s, l);
				const Coefficients own = get_lane<Coefficients>(c, l);
				reach(own, lane, reached.get(l), timed.get(l));
				set_lane(s, l, lane);
			}
		}
	}
};

static_assert(generator_kernel<Envelope>);
static_assert(poly_kernel<Envelope>);

} // namespace deluge::dsp::fp
