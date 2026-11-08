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
#include "dsp-next/core/pack.h"
#include "dsp-next/core/pack_f32.h"
#include "dsp-next/core/poly_kernel.h"
#include "dsp-next/core/sample.h"
#include "dsp-next/float/math/detail.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
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
	// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
	static constexpr auto fields = std::tuple{&Segment::target,      &Segment::aim,  &Segment::rate,
	                                          &Segment::exponential, &Segment::hold, &Segment::next};
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
	/// @details The step reads and writes `value`, `a`, `b`, `threshold` and `direction` only: every stage is the
	/// affine map `value = b + value·a`, ended when `(value - threshold)·direction >= 0`. The rest is for a stage
	/// change and the events.
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
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields =
		    std::tuple{&StateT::value,     &StateT::a,      &StateT::b,    &StateT::threshold,
		               &StateT::direction, &StateT::target, &StateT::next, &StateT::index};
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

	/// @brief One sample over V: f32 for one copy, Pack<f32, N> for one per lane.
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& c, const EmptyBlock& /*b*/,
	                                                   StateT<V>& s) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		s.value = mul_add(s.b, s.value, s.a);
		const auto reached = ((s.value - s.threshold) * s.direction) >= V{0.f};
		if (any(reached)) {
			advance(c, s, reached);
		}
		return s.value;
	}

private:
	/// One copy: its stage ended.
	[[gnu::always_inline]] static void advance(const Coefficients& c, State& s, bool /*reached*/) {
		enter(c, s, s.next, true);
	}
	/// Each lane that reached enters its next stage, scalar, from its own program.
	template <std::size_t N>
	static void advance(const CoefficientsT<Pack<f32, N>>& c, StateT<Pack<f32, N>>& s, Mask<N> reached) {
		for (std::size_t l = 0; l < N; ++l) {
			if (reached.get(l)) {
				State lane = get_lane<State>(s, l);
				enter(get_lane<Coefficients>(c, l), lane, lane.next, true);
				set_lane(s, l, lane);
			}
		}
	}
};

static_assert(generator_kernel<Envelope>);
static_assert(poly_kernel<Envelope>);

} // namespace deluge::dsp::fp
