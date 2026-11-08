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

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
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
/// e^(-x) for x in [0, 1]: a 20-term Taylor series in Horner form, in double, within 1e-15 of the true value. Plain
/// arithmetic in a fixed order, so every target computes the same bits (a library exp may differ).
[[nodiscard]] inline double exp_negative(double x) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	double r = 1.0;
	for (int k = 20; k >= 1; --k) {
		r = 1.0 - ((r * x) / static_cast<double>(k));
	}
	return r;
}
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
/// @param decay Decay time constant, in samples (> 0).
/// @param sustain The held level.
/// @param release Release time constant, in samples (> 0).
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
	template <class V>
	struct StateT {
		V value{};       ///< The level.
		V target{};      ///< Where the stage ends.
		V aim{};         ///< Where an EXPONENTIAL stage heads.
		V slope{};       ///< EXPONENTIAL: the coefficient. LINEAR: the step per sample.
		V exponential{}; ///< 1 while in an EXPONENTIAL stage.
		V hold{1.f};     ///< 1 while the stage never ends by itself (a hold, or at rest).
		V next{kOff};    ///< The stage entered at the end of this one.
		V direction{};   ///< +1 when the stage rises to its target, -1 when it falls.
		V epsilon{};     ///< How far short of the target counts as reached.
		V index{kOff};   ///< The stage's segment index, or kOff at rest.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields =
		    std::tuple{&StateT::value, &StateT::target, &StateT::aim,       &StateT::slope,   &StateT::exponential,
		               &StateT::hold,  &StateT::next,   &StateT::direction, &StateT::epsilon, &StateT::index};
	};
	using State = StateT<f32>;

	/// @brief The program of @p settings.
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
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
		const bool exponential = g.exponential > 0.5f;
		const bool hold = g.hold > 0.5f;
		s.target = g.target;
		s.aim = g.aim;
		s.exponential = exponential && !hold ? 1.f : 0.f;
		s.hold = g.hold;
		s.next = g.next;
		s.index = i;
		s.direction = g.target >= s.value ? 1.f : -1.f;
		s.epsilon = exponential && g.aim == g.target ? 1e-5f : 0.f;
		s.slope = 0.f; // a hold stays where it is
		if (exponential && !hold) {
			s.slope = g.rate;
		}
		else if (!hold) {
			s.slope = (g.target - s.value) / (g.rate < 1.f ? 1.f : g.rate); // a length under a sample is one
		}
	}

	/// @brief Starts segment 0 from the current value: a retrigger never jumps.
	static void gate_on(const Coefficients& c, State& s) { enter(c, s, 0.f, false); }
	/// @brief Enters the release segment from the current value; a program without one, or an envelope at rest,
	/// ignores it.
	static void gate_off(const Coefficients& c, State& s) {
		if (c.release >= 0.f && s.index >= 0.f) {
			enter(c, s, c.release, false);
		}
	}

	/// @brief One sample over V: f32 for one copy, Pack<f32, N> for one per lane.
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& c, const EmptyBlock& /*b*/,
	                                                   StateT<V>& s) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const V exp_next = mul_add(s.value, s.slope, s.aim - s.value);
		const V lin_next = s.value + s.slope;
		s.value = select(s.exponential > V{0.5f}, exp_next, lin_next);
		// Reached: at or past the target in the stage's direction, within epsilon (1e-5 for an exponential whose aim is
		// its target, which only approaches it; 0 otherwise). A hold never reaches.
		const auto reached = (((s.value - s.target) * s.direction) >= (V{0.f} - s.epsilon)) & (s.hold < V{0.5f});
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
