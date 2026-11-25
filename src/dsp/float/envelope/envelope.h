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
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <tuple>

/// @file
/// @brief fp::Envelope: an envelope of timed segments along main's curves, written once over its value type so
/// Poly runs a copy per lane, each lane in its own stage.
namespace deluge::dsp::fp {

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
	V fall{};      ///< A `follow` segment's length while it falls.
	V follow{};    ///< 1 to slew to `target`, wherever it moves, until a gate event: over `length` while it rises and
	               ///< `fall` while it falls, main's AR; a `curve` of 0 or less in a line.
	V resume{};    ///< 1 to enter it where its curve from 0 reaches the level there is, so a retrigger is shorter.
	// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
	static constexpr auto fields = std::tuple{&Segment::target, &Segment::length, &Segment::curve, &Segment::next,
	                                          &Segment::fall,   &Segment::follow, &Segment::resume};
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
	return {
	    .target = 1.f, .length = length, .curve = curve, .next = next, .fall = length, .follow = 0.f, .resume = 1.f};
}
/// A stage to @p target in @p length samples, entering @p next.
[[nodiscard]] inline Segment<f32> segment(f32 target, f32 length, f32 curve, f32 next) {
	return {
	    .target = target, .length = length, .curve = curve, .next = next, .fall = length, .follow = 0.f, .resume = 0.f};
}
/// A stage that stays at @p level until a gate event.
[[nodiscard]] inline Segment<f32> hold_segment(f32 level) {
	return {.target = level,
	        .length = kForever,
	        .curve = 0.f,
	        .next = kOff,
	        .fall = kForever,
	        .follow = 0.f,
	        .resume = 0.f};
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
/// @brief While the gate is, a slew to @p level along the decay's curve, over @p attack rising and @p release falling;
/// then release. MPE is this with a level its owner moves.
[[nodiscard]] inline Program ar(f32 attack, f32 release, f32 level, const Curves& curves) {
	Program p;
	p.segments[0] = {.target = level,
	                 .length = attack,
	                 .curve = curves.decay,
	                 .next = kOff,
	                 .fall = release,
	                 .follow = 1.f,
	                 .resume = 0.f};
	p.segments[1] = detail::segment(0.f, release, curves.release, kOff);
	p.release = 1.f;
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
/// @brief The bits of lane @p l of @p v.
[[nodiscard, gnu::always_inline]] inline uint32_t lane_bits(const f32& v, std::size_t /*l*/) {
	return std::bit_cast<uint32_t>(v);
}
template <std::size_t N>
[[nodiscard, gnu::always_inline]] inline uint32_t lane_bits(const Pack<f32, N>& v, std::size_t l) {
	return std::bit_cast<std::array<uint32_t, N>>(v.v)[l];
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
///
/// **Settings** reach a running stage once a block, in `prepare`: its phase is kept, and a level that is then off its
/// curve goes in a straight line to where the curve is 32 samples on.
struct Envelope {
	/// `at` where there is no segment whose settings to follow.
	static constexpr uint32_t kNowhere = 0xFFFFFFFFU;

	using value_type = f32; ///< The level, 0 to 1 for the shipped programs.
	using step_type = f32;  ///< One step per sample.

	/// @brief What the owner sets.
	struct Settings {
		Program program{}; ///< The stages.
	};

	/// @brief The program, over V; no length under a sample.
	template <class V>
	struct CoefficientsT {
		std::array<Segment<V>, kMaxSegments> segments{}; ///< The stages.
		V release{kOff};                                 ///< The segment a gate-off enters, or kOff.
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

		V hi{};             ///< The phase at the last sample, 0 to 1 over the stage; under 0 by less than a sample's
		                    ///< when the stage starts between samples.
		V lo{};             ///< What `hi` rounds off.
		V from{};           ///< The level the stage started at.
		V target{};         ///< The stage's target.
		V length{kForever}; ///< Its length in samples.
		V inverse{};        ///< 1 / length; 0 for kForever.
		V curve{};          ///< Its curve.
		V scale{};          ///< 1 / (2^curve - 1); 0 for a line.
		V decrement{};      ///< curve·ln 2 / length.
		V rate{};           ///< 1 - 2^(-curve / length).
		V most{kForever};   ///< The most samples a run of it takes.
		V gap_left{};       ///< The samples until the level is on its curve again, after a change of settings.
		V slew{};           ///< A slew's length, rising or falling as it is; 0 in no slew.
		V slew_inverse{};   ///< 1 / slew.
		V under{};          ///< What a slew's level, kept in `from`, rounds off.
		V fades{};          ///< 1 in a fade.
		V index{kOff};      ///< The stage's segment, or kOff at rest.
		V at{std::bit_cast<f32>(kNowhere)}; ///< The bits of that segment's number, or kNowhere at rest or in a fade.
		// NOLINTNEXTLINE(readability-identifier-naming): `step_fields` is the name poly_kernel reads.
		static constexpr auto step_fields = std::tuple{&StateT::v, &StateT::a, &StateT::b, &StateT::c};
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&StateT::v,         &StateT::a,      &StateT::b,
		                                          &StateT::c,         &StateT::hi,     &StateT::lo,
		                                          &StateT::from,      &StateT::target, &StateT::length,
		                                          &StateT::inverse,   &StateT::curve,  &StateT::scale,
		                                          &StateT::decrement, &StateT::rate,   &StateT::most,
		                                          &StateT::gap_left,  &StateT::slew,   &StateT::slew_inverse,
		                                          &StateT::under,     &StateT::fades,  &StateT::index,
		                                          &StateT::at};
	};
	using State = StateT<f32>;

	/// @brief The program of @p settings, every length at least a sample.
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		Coefficients c;
		c.segments = settings.program.segments;
		for (Segment<f32>& g : c.segments) {
			g.length = g.length < 1.f ? 1.f : g.length;
			g.fall = g.fall < 1.f ? 1.f : g.fall;
		}
		c.release = settings.program.release;
		return c;
	}

	/// @brief Starts segment 0 from the level there is: a retrigger never jumps.
	static void gate_on(const Coefficients& c, State& s) {
		s.gap_left = 0.f;
		enter(c, s, 0, 0.f, s.v, 0.f);
	}
	/// @brief Enters the release segment from the level there is; a program without one, or an envelope at rest,
	/// ignores it.
	static void gate_off(const Coefficients& c, State& s) {
		if (c.release >= 0.f && s.index >= 0.f && !(s.fades > 0.f)) {
			s.gap_left = 0.f;
			enter(c, s, 0, c.release, s.v, 0.f);
		}
	}

	/// @brief Fades to 0 along a raised cosine in @p length samples, from any stage; a fade already running keeps the
	/// shorter of what is left of it and @p length, and an envelope at rest ignores it.
	static void fade(State& s, f32 length) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		length = length < 1.f ? 1.f : length;
		if (s.index < 0.f || (s.fades > 0.f && ((1.f - s.hi) - s.lo) * s.length <= length)) {
			return;
		}
		const f32 level = s.v;
		rest(s, 0);
		s.index = kFading;
		s.fades = 1.f;
		s.from = level;
		s.length = length;
		s.inverse = 1.f / length;
		s.most = length < kFadePieces ? 1.f : trunc_nonnegative(length * (1.f / kFadePieces));
	}

	/// @brief Takes up each lane's running stage's settings where they changed.
	template <class V>
	[[gnu::noinline]] static EmptyBlock prepare(const CoefficientsT<V>& c, StateT<V>& s, std::size_t /*n*/) {
		follow_settings(c, s);
		return {};
	}

	/// @brief The samples to the first lane's stage end, or to where its map must be set again, at most @p n.
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static std::size_t run_length(const CoefficientsT<V>& /*c*/,
	                                                                   const StateT<V>& s, std::size_t n) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const V left = ((V{1.f} - s.hi) - s.lo) * s.length;
		const V most = min(s.most, select(s.gap_left > V{0.f}, s.gap_left, V{kForever}));
		const f32 samples = detail::least(min(left, most));
		return samples < static_cast<f32>(n) ? static_cast<std::size_t>(samples) : n;
	}

	/// @brief Sets each lane's map for a run of @p m samples, and moves its stage past it.
	template <class V>
	[[gnu::noinline]] static void prepare_run(const CoefficientsT<V>& c, StateT<V>& s, std::size_t m) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const V samples{static_cast<f32>(m)};
		const V rest = (V{1.f} - s.hi) - s.lo;
		const V exponential = exp2m1(s.curve * rest);
		const auto curved = abs(s.scale) > V{0.f};
		const V span = s.from - s.target;
		const V level = s.target + (span * select(curved, exponential * s.scale, rest));
		// Where the run covers little of the curve: its slope and curvature there
		const V slope = select(curved, ((span * s.scale) * (exponential + V{1.f})) * s.decrement, span * s.inverse);
		const V curvature = slope * s.decrement;
		const auto gentle = (abs(s.decrement) * samples) < V{kGentle};
		s.a = select(gentle, V{1.f}, V{1.f} - s.rate);
		s.b = select(gentle, V{0.f} - (slope + (curvature * V{0.5f})), (s.target - (span * s.scale)) * s.rate);
		s.c = select(gentle, curvature, V{0.f});
		if (any(max(s.gap_left, max(s.slew, s.fades)) > V{0.f})) [[unlikely]] {
			const V before = s.v;
			s.v = level;
			off_curve(s, samples, rest, before);
		}
		else {
			s.v = level;
		}
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
	/// The index of a fade, which is no segment of a program.
	static constexpr f32 kFading = static_cast<f32>(kMaxSegments);
	/// The pieces a fade's cosine is drawn in.
	static constexpr f32 kFadePieces = 16.f;
	/// The octaves (times ln 2) a run may cover and its map be quadratic: its error is under 2^-17 of the stage.
	static constexpr f32 kGentle = 0.02f;
	/// A stage under this length whose curve grows (k below 0) takes runs of at most kGrowingRun: its affine's slope is
	/// over 1, so rounding grows with it.
	static constexpr f32 kGrowingLength = 1000.f;
	static constexpr f32 kGrowingRun = 32.f;
	/// The samples a gap is closed over.
	static constexpr f32 kGapSamples = 32.f;
	/// A gap under this is the arithmetic's own.
	static constexpr f32 kNoGap = 0x1p-18f;
	/// The samples to a slew's arrival past which they are not counted.
	static constexpr f32 kFarSlew = 4096.f;
	/// 1 / n for a gap of n samples left.
	static constexpr std::array<f32, 33> kReciprocals = [] {
		std::array<f32, 33> reciprocals{};
		for (std::size_t n = 1; n < reciprocals.size(); ++n) {
			reciprocals[n] = 1.f / static_cast<f32>(n);
		}
		return reciprocals;
	}();

	/// The maps of the lanes that are on no curve: a level going back to its own in a line, a slew, a fade.
	/// @param before Each lane's level at its last sample.
	template <class V>
	[[gnu::noinline]] static void off_curve(StateT<V>& s, V samples, V rest, V before) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		if (const auto closing = s.gap_left > V{0.f}; any(closing)) {
			// A line from the level to where the curve is when the gap is closed, or to the stage's target if that is
			// first
			const V then = max(rest - (s.gap_left * s.inverse), V{0.f});
			const auto curved = abs(s.scale) > V{0.f};
			const V aim = s.target + ((s.from - s.target) * select(curved, exp2m1(s.curve * then) * s.scale, then));
			s.a = select(closing, V{1.f}, s.a);
			s.b = select(closing, (aim - before) * gather(kReciprocals.data(), s.gap_left), s.b);
			s.c = select(closing, V{0.f}, s.c);
			s.v = select(closing, before, s.v);
			s.gap_left = max(s.gap_left - samples, V{0.f});
		}
		if (const auto slews = s.slew > V{0.f}; any(slews)) {
			const V span = (s.from - s.target) + s.under;
			const auto curved = s.decrement > V{0.f};
			// Along a curve: what each sample leaves of the way to the target. In a line: a step, or what is left
			const V curvature = (span * s.decrement) * s.decrement;
			const auto gentle = (s.decrement * samples) < V{kGentle};
			const V most = select(span > V{0.f}, s.slew_inverse, V{0.f} - s.slew_inverse);
			const V step = select(abs(span) > s.slew_inverse, most, span);
			const V a = select(curved, select(gentle, V{1.f}, V{1.f} - s.rate), V{1.f});
			const V b = select(
			    curved, select(gentle, V{0.f} - ((span * s.decrement) + (curvature * V{0.5f})), s.target * s.rate),
			    V{0.f} - step);
			const V c = select(curved, select(gentle, curvature, V{0.f}), V{0.f});
			s.a = select(slews, a, s.a);
			s.b = select(slews, b, s.b);
			s.c = select(slews, c, s.c);
			s.v = select(slews, s.from, s.v);
			// The level at the run's end, in two floats
			const V gone = V{0.f} - exp2m1(V{0.f} - ((s.curve * s.slew_inverse) * samples));
			const V moved = (V{0.f} - select(curved, span * gone, step * samples)) + s.under;
			const V from = s.from + moved;
			const V under = moved - (from - s.from);
			const auto lands = abs(span) <= s.slew_inverse;
			const auto line = select(curved, V{0.f}, V{1.f}) > V{0.f};
			const V at = select(line, select(lands, s.target, from), from);
			s.under = select(slews, select(abs(at - s.target) > V{0.f}, under, V{0.f}), s.under);
			s.from = select(slews, at, s.from);
			// A line's arrival is a run's end
			const V left = min(abs(at - s.target) * s.slew, V{kFarSlew});
			const V whole = max(trunc_nonnegative(left), V{1.f});
			s.most = select(slews, select(line, select(left > V{0.f}, whole, V{kForever}), s.most), s.most);
		}
		if (const auto fading = s.fades > V{0.f}; any(fading)) {
			const V step = samples * s.inverse;
			const V end = max(rest - step, V{0.f});
			const V v0 = s.from * cosine(rest);
			const V v1 = s.from * cosine(end);
			const V middle = s.from * cosine(max(rest - (step * V{0.5f}), V{0.f}));
			const V reciprocal{1.f / detail::lane(samples, 0)};
			const V curvature = (((v1 - middle) - (middle - v0)) * V{4.f}) * (reciprocal * reciprocal);
			const V slope = (v1 - v0) * reciprocal;
			s.a = select(fading, V{1.f}, s.a);
			s.b = select(fading, slope - (curvature * ((samples + V{1.f}) * V{0.5f})), s.b);
			s.c = select(fading, curvature, s.c);
			s.v = select(fading, v0, s.v);
		}
	}

	/// 0.5 + 0.5·cos(pi·x) where @p rest of it, 1 - x, is left: the square of the sine of pi/2 of it.
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static V cosine(V rest) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const V u = rest * V{std::numbers::pi_v<f32> * 0.5f};
		const V u2 = u * u;
		// The sine's series to the eleventh power
		V p{-2.5052108e-8f};
		p = (p * u2) + V{2.7557319e-6f};
		p = (p * u2) + V{-1.9841270e-4f};
		p = (p * u2) + V{8.3333333e-3f};
		p = (p * u2) + V{-0.16666667f};
		const V sine = u + (u * (u2 * p));
		return sine * sine;
	}

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
			const uint32_t at = detail::lane_bits(s.at, l);
			if (at == kNowhere) {
				continue;
			}
			const Segment<V>& g = c.segments[at];
			// Compared as bits: a float compare's flags stall the integer side
			const uint32_t slew = detail::lane_bits(s.slew, l);
			const uint32_t length = slew == 0 ? detail::lane_bits(g.length, l) ^ detail::lane_bits(s.length, l)
			                                  : std::bit_cast<uint32_t>(slew_length(g, s, l)) ^ slew;
			if (((detail::lane_bits(g.target, l) ^ detail::lane_bits(s.target, l))
			     | (detail::lane_bits(g.curve, l) ^ detail::lane_bits(s.curve, l)) | length)
			    != 0) [[unlikely]] {
				take_up(c, s, l, detail::lane(s.index, l));
			}
		}
	}

	/// Lane @p l 's running stage, segment @p i, with its settings as they are now, from where its phase is: ended, if
	/// no sample of it is left. What the level is then off its curve is the gap.
	template <class V>
	[[gnu::noinline]] static void take_up(const CoefficientsT<V>& c, StateT<V>& s, std::size_t l, f32 i) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const f32 before = detail::lane(s.length, l);
		load(c, s, l, i);
		if (detail::lane(s.slew, l) > 0.f) {
			return; // a slew goes on from its level
		}
		if (const f32 hi = detail::lane(s.hi, l); hi < 0.f) {
			// Starting between samples: that fraction of one, whatever the length
			detail::set_lane_of(s.hi, l, (hi * before) * detail::lane(s.inverse, l));
		}
		const f32 rest = (1.f - detail::lane(s.hi, l)) - detail::lane(s.lo, l);
		const f32 left = rest * detail::lane(s.length, l);
		if (left < 1.f) {
			const Segment<V>& g = c.segments[static_cast<std::size_t>(i)];
			enter(c, s, l, detail::lane(g.next, l), detail::lane(s.target, l), 0.f - left);
		}
		const f32 gap = detail::lane(s.v, l) - curve_level(s, l);
		const bool closes = gap > kNoGap || gap < -kNoGap;
		detail::set_lane_of(s.gap_left, l, closes ? kGapSamples : 0.f);
	}

	/// The most samples a run of a stage takes: to be asked again each sample for a line's slew, whose arrival the next
	/// run counts to, and kGrowingRun for a short stage whose curve grows.
	[[nodiscard]] static f32 most_of(bool follows, f32 curve, f32 length) {
		if (follows) {
			return curve > 0.f ? kForever : 1.f;
		}
		return curve < 0.f && length < kGrowingLength ? kGrowingRun : kForever;
	}

	/// The length lane @p l slews over: up to its target, or down to it.
	template <class V>
	[[nodiscard, gnu::always_inline]] static f32 slew_length(const Segment<V>& g, const StateT<V>& s, std::size_t l) {
		return detail::lane(g.target, l) > detail::lane(s.from, l) ? detail::lane(g.length, l)
		                                                           : detail::lane(g.fall, l);
	}

	/// The level lane @p l 's curve gives at its phase.
	template <class V>
	[[nodiscard]] static f32 curve_level(const StateT<V>& s, std::size_t l) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const f32 rest = (1.f - detail::lane(s.hi, l)) - detail::lane(s.lo, l);
		const f32 scale = detail::lane(s.scale, l);
		const f32 shape = scale == 0.f ? rest : exp2m1(detail::lane(s.curve, l) * rest) * scale;
		const f32 target = detail::lane(s.target, l);
		return target + ((detail::lane(s.from, l) - target) * shape);
	}

	/// Segment @p i of lane @p l as its running stage: its settings and what is computed from them.
	template <class V>
	[[gnu::noinline]] static void load(const CoefficientsT<V>& c, StateT<V>& s, std::size_t l, f32 i) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const auto k = static_cast<std::size_t>(i);
		assert(k < kMaxSegments);
		const Segment<V>& g = c.segments[k];
		const f32 target = detail::lane(g.target, l);
		const f32 curve = detail::lane(g.curve, l);
		const bool follows = detail::lane(g.follow, l) > 0.5f;
		const f32 slew = follows ? slew_length(g, s, l) : 0.f;
		detail::set_lane_of(s.slew, l, slew);
		detail::set_lane_of(s.slew_inverse, l, follows ? 1.f / slew : 0.f);
		detail::set_lane_of(s.fades, l, 0.f);
		const f32 length = follows ? kForever : detail::lane(g.length, l);
		const bool forever = length == kForever;
		const f32 inverse = forever ? 0.f : 1.f / length;
		detail::set_lane_of(s.index, l, i);
		detail::set_lane_of(s.at, l, std::bit_cast<f32>(static_cast<uint32_t>(k)));
		detail::set_lane_of(s.target, l, target);
		detail::set_lane_of(s.length, l, length);
		detail::set_lane_of(s.inverse, l, inverse);
		detail::set_lane_of(s.curve, l, curve);
		detail::set_lane_of(s.scale, l, forever ? 0.f : curve_scale(curve));
		// A slew's along its own length; in a line where its curve is 0 or less
		const f32 octaves = follows ? (curve > 0.f ? curve : 0.f) * detail::lane(s.slew_inverse, l) : curve * inverse;
		detail::set_lane_of(s.decrement, l, octaves * std::numbers::ln2_v<f32>);
		detail::set_lane_of(s.rate, l, 0.f - exp2m1(0.f - octaves));
		detail::set_lane_of(s.most, l, most_of(follows, curve, length));
		if (forever && !follows) {
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
			const Segment<V>& g = c.segments[static_cast<std::size_t>(i)];
			const f32 target = detail::lane(s.target, l);
			const f32 length = detail::lane(s.length, l);
			if (length == kForever) {
				detail::set_lane_of(s.hi, l, 0.f);
				if (detail::lane(g.follow, l) > 0.5f) {
					detail::set_lane_of(s.from, l, from);
					detail::set_lane_of(s.under, l, 0.f);
					load(c, s, l, i); // its direction, from here
				}
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
		detail::set_lane_of(s.gap_left, l, resting.gap_left);
		detail::set_lane_of(s.slew, l, resting.slew);
		detail::set_lane_of(s.slew_inverse, l, resting.slew_inverse);
		detail::set_lane_of(s.under, l, resting.under);
		detail::set_lane_of(s.fades, l, resting.fades);
		detail::set_lane_of(s.index, l, resting.index);
		detail::set_lane_of(s.at, l, resting.at);
	}

	/// Each lane whose stage ends with this run enters its next.
	template <class V, class M>
	[[gnu::noinline]] static void go_on(const CoefficientsT<V>& c, StateT<V>& s, M ended) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		for (std::size_t l = 0; l < detail::kLanes<V>; ++l) {
			if (held(ended, l)) {
				const f32 left = ((1.f - detail::lane(s.hi, l)) - detail::lane(s.lo, l)) * detail::lane(s.length, l);
				const f32 index = detail::lane(s.index, l);
				const f32 next =
				    index == kFading ? kOff : detail::lane(c.segments[static_cast<std::size_t>(index)].next, l);
				enter(c, s, l, next, detail::lane(s.target, l), 0.f - left);
			}
		}
	}
};

static_assert(generator_kernel<Envelope>);
static_assert(poly_kernel<Envelope>);
static_assert(runs_kernel<Envelope>);

} // namespace deluge::dsp::fp
