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
#pragma once

#include "dsp/core/kernel.h"
#include "dsp/core/pack.h"
#include "dsp/core/pack_f32.h"
#include "dsp/core/poly_kernel.h"
#include "dsp/core/sample.h"
#include "dsp/float/envelope/curve.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <tuple>

/// @file
/// @brief fp::TimedEnvelope: an envelope of timed segments along main's curves, written once over its value type so
/// Poly runs a copy per lane, each lane in its own stage.
namespace deluge::dsp::fp {

namespace timed {

/// @brief The most segments a program holds.
inline constexpr std::size_t kMaxSegments = 8;
/// @brief A segment's `next` (and a program's `release`) when there is none: the envelope rests at 0 (or ignores a
/// gate-off).
inline constexpr f32 kOff = -1.f;
/// @brief The length of a segment that never ends by itself: it stays at its target.
inline constexpr f32 kForever = std::numeric_limits<f32>::infinity();

/// @brief One stage of a program, over V; the flags and the index are 0/1 or whole numbers held in V.
template <class V>
struct Segment {
	V target{};    ///< The level the segment ends at.
	V length{1.f}; ///< Its length, in samples; kForever to stay at `target`.
	V curve{};     ///< Its curve, in octaves: positive is steep at the start, negative at the end, 0 is a line.
	V next{kOff};  ///< The segment entered at its end, or kOff.
	V resume{};    ///< 1 to enter it where its curve from 0 reaches the level there is, so a retrigger is shorter.
	// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
	static constexpr auto fields =
	    std::tuple{&Segment::target, &Segment::length, &Segment::curve, &Segment::next, &Segment::resume};
};

/// @brief A program: its segments (the first is entered by a gate-on) and the segment a gate-off enters.
struct Program {
	std::array<Segment<f32>, kMaxSegments> segments{}; ///< The stages.
	f32 release = kOff;                                ///< The segment a gate-off enters, or kOff to ignore it.
};

/// @brief Each stage's curve, in octaves; main's classic shape.
struct Curves {
	f32 attack = 4.f;
	f32 decay = 8.f;
	f32 release = 8.f;
};

namespace detail {
/// An attack to 1 in @p length samples, entering @p next.
[[nodiscard]] inline Segment<f32> attack_segment(f32 length, f32 curve, f32 next) {
	return {.target = 1.f, .length = length, .curve = curve, .next = next, .resume = 1.f};
}
/// A stage to @p target in @p length samples, entering @p next.
[[nodiscard]] inline Segment<f32> segment(f32 target, f32 length, f32 curve, f32 next) {
	return {.target = target, .length = length, .curve = curve, .next = next, .resume = 0.f};
}
/// A stage that stays at @p level until a gate event.
[[nodiscard]] inline Segment<f32> hold_segment(f32 level) {
	return {.target = level, .length = kForever, .curve = 0.f, .next = kOff, .resume = 0.f};
}
} // namespace detail

/// @brief Attack, decay, sustain, release. Lengths in samples, as for every program; under a sample counts as one.
[[nodiscard]] inline Program adsr(f32 attack, f32 decay, f32 sustain, f32 release, const Curves& curves) {
	Program p;
	p.segments[0] = detail::attack_segment(attack, curves.attack, 1.f);
	p.segments[1] = detail::segment(sustain, decay, curves.decay, 2.f);
	p.segments[2] = detail::hold_segment(sustain);
	p.segments[3] = detail::segment(0.f, release, curves.release, kOff);
	p.release = 3.f;
	return p;
}
/// @brief Attack then decay to 0, ignoring gate-off.
[[nodiscard]] inline Program ad(f32 attack, f32 decay, const Curves& curves) {
	Program p;
	p.segments[0] = detail::attack_segment(attack, curves.attack, 1.f);
	p.segments[1] = detail::segment(0.f, decay, curves.decay, kOff);
	return p;
}
/// @brief Attack, a hold at 1 for @p hold samples, then decay to 0, ignoring gate-off.
[[nodiscard]] inline Program ahd(f32 attack, f32 hold, f32 decay, const Curves& curves) {
	Program p;
	p.segments[0] = detail::attack_segment(attack, curves.attack, 1.f);
	p.segments[1] = detail::segment(1.f, hold, 0.f, 2.f);
	p.segments[2] = detail::segment(0.f, decay, curves.decay, kOff);
	return p;
}
/// @brief Attack, a hold at 1 while the gate is, release.
[[nodiscard]] inline Program asr(f32 attack, f32 release, const Curves& curves) {
	Program p;
	p.segments[0] = detail::attack_segment(attack, curves.attack, 1.f);
	p.segments[1] = detail::hold_segment(1.f);
	p.segments[2] = detail::segment(0.f, release, curves.release, kOff);
	p.release = 2.f;
	return p;
}
/// @brief A DX7-shaped program: straight to @p levels[0], to @p levels[1], to @p levels[2] where it holds; a gate-off
/// goes to @p levels[3], where it stays. @p times are the stages' lengths.
[[nodiscard]] inline Program four_segment(std::array<f32, 4> levels, std::array<f32, 4> times) {
	Program p;
	p.segments[0] = detail::segment(levels[0], times[0], 0.f, 1.f);
	p.segments[1] = detail::segment(levels[1], times[1], 0.f, 2.f);
	p.segments[2] = detail::segment(levels[2], times[2], 0.f, 3.f);
	p.segments[3] = detail::hold_segment(levels[2]);
	p.segments[4] = detail::segment(levels[3], times[3], 0.f, 5.f);
	p.segments[5] = detail::hold_segment(levels[3]);
	p.release = 4.f;
	return p;
}
/// @brief Straight to @p high and to @p low in @p time samples each, forever.
[[nodiscard]] inline Program looping(f32 low, f32 high, f32 time) {
	Program p;
	p.segments[0] = detail::segment(high, time, 0.f, 1.f);
	p.segments[1] = detail::segment(low, time, 0.f, 0.f);
	return p;
}

} // namespace timed

namespace detail {
/// @brief How many lanes a V has.
template <class V>
inline constexpr std::size_t kLanes = sizeof(V) / sizeof(f32);

/// @brief Lane @p l of @p v.
[[nodiscard, gnu::always_inline]] inline f32 lane(const f32& v, std::size_t /*l*/) {
	return v;
}
template <std::size_t N>
[[nodiscard, gnu::always_inline]] inline f32 lane(const Pack<f32, N>& v, std::size_t l) {
	return v.get(l);
}
/// @brief Sets lane @p l of @p v.
[[gnu::always_inline]] inline void set_lane_of(f32& v, std::size_t /*l*/, f32 x) {
	v = x;
}
template <std::size_t N>
[[gnu::always_inline]] inline void set_lane_of(Pack<f32, N>& v, std::size_t l, f32 x) {
	v.set(l, x);
}

/// @brief The least lane of @p v.
[[nodiscard, gnu::always_inline]] inline f32 least(f32 v) {
	return v;
}
template <std::size_t N>
[[nodiscard, gnu::always_inline]] inline f32 least(Pack<f32, N> v) {
	if constexpr (N == 4) {
		const Pack<f32, 2> low{__builtin_shufflevector(v.v, v.v, 0, 1)};
		const Pack<f32, 2> high{__builtin_shufflevector(v.v, v.v, 2, 3)};
		return least(min(low, high));
	}
	else {
		return min(v, Pack<f32, 2>{__builtin_shufflevector(v.v, v.v, 1, 0)}).get(0);
	}
}
} // namespace detail

/// @brief An envelope of timed segments: each runs from the level there is to its target along main's curve.
/// @details The step is the run's map alone: `b += c; v = b + v·a`. `prepare_run` anchors each lane's level on its
/// curve at the run's start, exactly, and sets the map for the run: affine, the curve's own asymptote (a curve at a
/// constant rate is one), or quadratic where the run covers so little of the curve that the affine's slope would round.
/// A stage's end is a run's end, where the lane enters its next stage, the fraction of a sample past the end carried.
struct TimedEnvelope {
	using value_type = f32; ///< The level, 0 to 1 for the shipped programs.
	using step_type = f32;  ///< One step per sample.

	/// @brief What the owner sets.
	struct Settings {
		timed::Program program{}; ///< The stages.
	};

	/// @brief The program, over V; no length under a sample.
	template <class V>
	struct CoefficientsT {
		std::array<timed::Segment<V>, timed::kMaxSegments> segments{}; ///< The stages.
		V release{timed::kOff};                                        ///< The segment a gate-off enters, or kOff.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&CoefficientsT::segments, &CoefficientsT::release};
	};
	using Coefficients = CoefficientsT<f32>;

	/// @brief The level, the run's map and the stage, over V. StateT{} rests at 0.
	template <class V>
	struct StateT {
		V v{};    ///< The level.
		V a{1.f}; ///< The map's slope.
		V b{};    ///< The map's offset.
		V c{};    ///< What the offset gains a sample.

		V hi{};     ///< The phase at the last sample, 0 to 1 over the stage; under 0 by less than a sample's
		            ///< when the stage starts between samples.
		V lo{};     ///< What `hi` rounds off.
		V from{};   ///< The level the stage started at.
		V target{}; ///< The stage's target.
		V length{timed::kForever}; ///< Its length in samples.
		V inverse{};               ///< 1 / length; 0 for kForever.
		V curve{};                 ///< Its curve.
		V scale{};                 ///< 1 / (2^curve - 1); 0 for a line.
		V decrement{};             ///< curve·ln 2 / length.
		V rate{};                  ///< 1 - 2^(-curve / length).
		V most{timed::kForever};   ///< The most samples a run of it takes.
		V index{timed::kOff};      ///< The stage's segment, or kOff at rest.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&StateT::v,         &StateT::a,       &StateT::b,     &StateT::c,
		                                          &StateT::hi,        &StateT::lo,      &StateT::from,  &StateT::target,
		                                          &StateT::length,    &StateT::inverse, &StateT::curve, &StateT::scale,
		                                          &StateT::decrement, &StateT::rate,    &StateT::most,  &StateT::index};
	};
	using State = StateT<f32>;

	/// @brief The program of @p settings, every length at least a sample.
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		Coefficients c;
		c.segments = settings.program.segments;
		for (timed::Segment<f32>& g : c.segments) {
			g.length = g.length < 1.f ? 1.f : g.length;
		}
		c.release = settings.program.release;
		return c;
	}

	/// @brief Starts segment 0 from the level there is: a retrigger never jumps.
	static void gate_on(const Coefficients& c, State& s) { enter(c, s, 0, 0.f, s.v, 0.f); }
	/// @brief Enters the release segment from the level there is; a program without one, or an envelope at rest,
	/// ignores it.
	static void gate_off(const Coefficients& c, State& s) {
		if (c.release >= 0.f && s.index >= 0.f) {
			enter(c, s, 0, c.release, s.v, 0.f);
		}
	}

	/// @brief The samples to the first lane's stage end, or to where its map must be set again, at most @p n.
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static std::size_t run_length(const CoefficientsT<V>& /*c*/,
	                                                                   const StateT<V>& s, std::size_t n) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const V left = ((V{1.f} - s.hi) - s.lo) * s.length;
		const f32 samples = detail::least(min(left, s.most));
		return samples < static_cast<f32>(n) ? static_cast<std::size_t>(samples) : n;
	}

	/// @brief Sets each lane's map for a run of @p m samples, and moves its stage past it.
	template <class V>
	[[gnu::noinline]] static void prepare_run(const CoefficientsT<V>& c, StateT<V>& s, std::size_t m) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		follow_settings(c, s);
		const V samples{static_cast<f32>(m)};
		const V rest = (V{1.f} - s.hi) - s.lo;
		const V exponential = exp2m1(s.curve * rest);
		const auto curved = abs(s.scale) > V{0.f};
		const V span = s.from - s.target;
		s.v = s.target + (span * select(curved, exponential * s.scale, rest));
		// Where the run covers little of the curve: its slope and curvature there
		const V slope = select(curved, ((span * s.scale) * (exponential + V{1.f})) * s.decrement, span * s.inverse);
		const V curvature = slope * s.decrement;
		const auto gentle = (abs(s.decrement) * samples) < V{kGentle};
		s.a = select(gentle, V{1.f}, V{1.f} - s.rate);
		s.b = select(gentle, V{0.f} - (slope + (curvature * V{0.5f})), (s.target - (span * s.scale)) * s.rate);
		s.c = select(gentle, curvature, V{0.f});
		// The phase at the run's end, the rounding kept
		const V moved = (samples * s.inverse) + s.lo;
		const V hi = s.hi + moved;
		s.lo = moved - (hi - s.hi);
		s.hi = hi;
		const V left = ((V{1.f} - s.hi) - s.lo) * s.length;
		const auto ended = left < V{1.f};
		if (any(ended)) [[unlikely]] {
			go_on(c, s, ended);
		}
	}

	/// @brief One sample over V: f32 for one copy, Pack<f32, N> for one per lane.
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& /*c*/, const EmptyBlock& /*b*/,
	                                                   StateT<V>& s) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		s.b = s.b + s.c;
		s.v = s.b + (s.v * s.a);
		return s.v;
	}

private:
	/// The octaves (times ln 2) a run may cover and its map be quadratic: its error is under 2^-17 of the stage.
	static constexpr f32 kGentle = 0.02f;
	/// A stage under this length whose curve grows (k below 0) takes runs of at most kGrowingRun: its affine's slope is
	/// over 1, so rounding grows with it.
	static constexpr f32 kGrowingLength = 1000.f;
	static constexpr f32 kGrowingRun = 32.f;

	/// Whether lane @p l 's comparison held.
	[[nodiscard]] [[gnu::always_inline]] static bool held(bool m, std::size_t /*l*/) { return m; }
	template <std::size_t N>
	[[nodiscard]] [[gnu::always_inline]] static bool held(Mask<N> m, std::size_t l) {
		return m.get(l);
	}

	/// Takes up a running stage's settings where they changed, its phase kept.
	template <class V>
	[[gnu::always_inline]] static void follow_settings(const CoefficientsT<V>& c, StateT<V>& s) {
		for (std::size_t l = 0; l < detail::kLanes<V>; ++l) {
			const f32 index = detail::lane(s.index, l);
			if (index < 0.f) {
				continue;
			}
			const timed::Segment<V>& g = c.segments[static_cast<std::size_t>(index)];
			if (detail::lane(g.target, l) != detail::lane(s.target, l)
			    || detail::lane(g.length, l) != detail::lane(s.length, l)
			    || detail::lane(g.curve, l) != detail::lane(s.curve, l)) [[unlikely]] {
				load(c, s, l, index);
			}
		}
	}

	/// Segment @p i of lane @p l as its running stage: its settings and what is computed from them.
	template <class V>
	[[gnu::noinline]] static void load(const CoefficientsT<V>& c, StateT<V>& s, std::size_t l, f32 i) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const auto k = static_cast<std::size_t>(i);
		assert(k < timed::kMaxSegments);
		const timed::Segment<V>& g = c.segments[k];
		const f32 target = detail::lane(g.target, l);
		const f32 length = detail::lane(g.length, l);
		const f32 curve = detail::lane(g.curve, l);
		const bool forever = length == timed::kForever;
		const f32 inverse = forever ? 0.f : 1.f / length;
		detail::set_lane_of(s.index, l, i);
		detail::set_lane_of(s.target, l, target);
		detail::set_lane_of(s.length, l, length);
		detail::set_lane_of(s.inverse, l, inverse);
		detail::set_lane_of(s.curve, l, curve);
		detail::set_lane_of(s.scale, l, forever ? 0.f : curve_scale(curve));
		detail::set_lane_of(s.decrement, l, (curve * std::numbers::ln2_v<f32>)*inverse);
		detail::set_lane_of(s.rate, l, 0.f - exp2m1(0.f - (curve * inverse)));
		detail::set_lane_of(s.most, l, curve < 0.f && length < kGrowingLength ? kGrowingRun : timed::kForever);
		if (forever) {
			detail::set_lane_of(s.from, l, target);
		}
	}

	/// Enters segment @p i of lane @p l from the level @p from, the last sample @p position samples into it: over -1,
	/// so that the next is in it.
	template <class V>
	[[gnu::noinline]] static void enter(const CoefficientsT<V>& c, StateT<V>& s, std::size_t l, f32 i, f32 from,
	                                    f32 position) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		for (;;) {
			detail::set_lane_of(s.lo, l, 0.f);
			if (i < 0.f) {
				rest(s, l);
				return;
			}
			load(c, s, l, i);
			const timed::Segment<V>& g = c.segments[static_cast<std::size_t>(i)];
			const f32 target = detail::lane(s.target, l);
			const f32 length = detail::lane(s.length, l);
			if (length == timed::kForever) {
				detail::set_lane_of(s.hi, l, 0.f);
				return;
			}
			f32 hi = position * detail::lane(s.inverse, l);
			f32 left = length - position;
			f32 end = target;
			if (detail::lane(g.resume, l) > 0.5f && from > 0.f) {
				if (from < target) {
					const CurvePosition at = curve_position(detail::lane(s.curve, l), from / target);
					hi = at.phase;
					detail::set_lane_of(s.lo, l, at.rest);
					left = ((1.f - at.phase) - at.rest) * length;
					from = 0.f;
				}
				else { // there already
					left = 0.f;
					end = from;
				}
			}
			detail::set_lane_of(s.from, l, from);
			detail::set_lane_of(s.hi, l, hi);
			if (left >= 1.f) {
				return;
			}
			// Nothing of it is left for a sample: on to the next
			position = 0.f - left;
			from = end;
			i = detail::lane(g.next, l);
		}
	}

	/// Lane @p l at rest.
	template <class V>
	static void rest(StateT<V>& s, std::size_t l) {
		const State resting{};
		detail::set_lane_of(s.hi, l, resting.hi);
		detail::set_lane_of(s.lo, l, resting.lo);
		detail::set_lane_of(s.from, l, resting.from);
		detail::set_lane_of(s.target, l, resting.target);
		detail::set_lane_of(s.length, l, resting.length);
		detail::set_lane_of(s.inverse, l, resting.inverse);
		detail::set_lane_of(s.curve, l, resting.curve);
		detail::set_lane_of(s.scale, l, resting.scale);
		detail::set_lane_of(s.decrement, l, resting.decrement);
		detail::set_lane_of(s.rate, l, resting.rate);
		detail::set_lane_of(s.most, l, resting.most);
		detail::set_lane_of(s.index, l, resting.index);
	}

	/// Each lane whose stage ends with this run enters its next.
	template <class V, class M>
	[[gnu::noinline]] static void go_on(const CoefficientsT<V>& c, StateT<V>& s, M ended) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		for (std::size_t l = 0; l < detail::kLanes<V>; ++l) {
			if (held(ended, l)) {
				const f32 left = ((1.f - detail::lane(s.hi, l)) - detail::lane(s.lo, l)) * detail::lane(s.length, l);
				const timed::Segment<V>& g = c.segments[static_cast<std::size_t>(detail::lane(s.index, l))];
				enter(c, s, l, detail::lane(g.next, l), detail::lane(s.target, l), 0.f - left);
			}
		}
	}
};

static_assert(generator_kernel<TimedEnvelope>);
static_assert(poly_kernel<TimedEnvelope>);
static_assert(runs_kernel<TimedEnvelope>);

} // namespace deluge::dsp::fp
