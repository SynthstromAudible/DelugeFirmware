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

#include <algorithm>
#include <array>
#include <cstddef>
#include <tuple>

// Kernels that ask to be prepared again within a block, for the specs of the wrappers that split it.

namespace run_test {

using deluge::dsp::EmptyBlock;
using deluge::dsp::f32;
using deluge::dsp::Pack;

/// Lane @p l of a value over V.
[[nodiscard]] inline f32 lane_of(const f32& v, std::size_t /*l*/) {
	return v;
}
template <std::size_t N>
[[nodiscard]] f32 lane_of(const Pack<f32, N>& v, std::size_t l) {
	return v.get(l);
}
inline void set_lane_of(f32& v, std::size_t /*l*/, f32 x) {
	v = x;
}
template <std::size_t N>
void set_lane_of(Pack<f32, N>& v, std::size_t l, f32 x) {
	v.set(l, x);
}

/// A generator that, once started, asks for attention every `period` samples. Its sample is 1000 times how many runs of
/// it have been prepared plus how many samples it has stepped, so an output tells exactly where the runs fell.
struct Periodic {
	using value_type = f32;
	using step_type = f32;
	struct Settings {
		f32 period = 0.f;
	};
	template <class V>
	struct CoefficientsT {
		V period{};
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&CoefficientsT::period};
	};
	using Coefficients = CoefficientsT<f32>;
	template <class V>
	struct StateT {
		V left{};     ///< Until the next attention; 0 before it is started.
		V prepared{}; ///< Runs prepared.
		V steps{};    ///< Samples stepped.
		V asked{};    ///< The samples its runs were prepared for.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&StateT::left, &StateT::prepared, &StateT::steps, &StateT::asked};
	};
	using State = StateT<f32>;

	[[nodiscard]] static Coefficients coefficients(const Settings& settings) { return {.period = settings.period}; }
	/// Starts asking.
	static void start(const Coefficients& c, State& s) { s.left = c.period; }
	template <class V>
	[[nodiscard]] static std::size_t run_length(const CoefficientsT<V>& /*c*/, const StateT<V>& s, std::size_t n) {
		for (std::size_t l = 0; l < sizeof(V) / sizeof(f32); ++l) {
			if (lane_of(s.left, l) > 0.f) {
				n = std::min(n, static_cast<std::size_t>(lane_of(s.left, l)));
			}
		}
		return n;
	}
	template <class V>
	static void prepare_run(const CoefficientsT<V>& c, StateT<V>& s, std::size_t m) {
		for (std::size_t l = 0; l < sizeof(V) / sizeof(f32); ++l) {
			if (const f32 left = lane_of(s.left, l) - static_cast<f32>(m); lane_of(s.left, l) > 0.f) {
				set_lane_of(s.left, l, left > 0.f ? left : lane_of(c.period, l));
			}
			set_lane_of(s.prepared, l, lane_of(s.prepared, l) + 1.f);
			set_lane_of(s.asked, l, lane_of(s.asked, l) + static_cast<f32>(m));
		}
	}
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& /*c*/, const EmptyBlock& /*b*/,
	                                                   StateT<V>& s) {
		s.steps = s.steps + V{1.f};
		return (s.prepared * V{1000.f}) + s.steps;
	}
};

/// A processor that adds a Periodic's sample to its input.
struct AddPeriodic {
	using value_type = f32;
	using step_type = f32;
	using Settings = Periodic::Settings;
	template <class V>
	using CoefficientsT = Periodic::CoefficientsT<V>;
	using Coefficients = CoefficientsT<f32>;
	template <class V>
	using StateT = Periodic::StateT<V>;
	using State = StateT<f32>;

	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
		return Periodic::coefficients(settings);
	}
	static void start(const Coefficients& c, State& s) { Periodic::start(c, s); }
	template <class V>
	[[nodiscard]] static std::size_t run_length(const Periodic::CoefficientsT<V>& c, const Periodic::StateT<V>& s,
	                                            std::size_t n) {
		return Periodic::run_length(c, s, n);
	}
	template <class V>
	static void prepare_run(const Periodic::CoefficientsT<V>& c, Periodic::StateT<V>& s, std::size_t m) {
		Periodic::prepare_run(c, s, m);
	}
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const Periodic::CoefficientsT<V>& c, const EmptyBlock& b,
	                                                   Periodic::StateT<V>& s, std::type_identity_t<V> x) {
		return Periodic::step(c, b, s) + x;
	}
};

/// A generator whose step touches `level` and `gain` of its State and whose `prepare_run` keeps the rest: `kept` counts
/// the samples prepared for, and `gain` is what the step multiplies by.
struct Sparse {
	using value_type = f32;
	using step_type = f32;
	struct Settings {
		f32 scale = 1.f;
	};
	template <class V>
	struct CoefficientsT {
		V scale{};
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&CoefficientsT::scale};
	};
	using Coefficients = CoefficientsT<f32>;
	template <class V>
	struct StateT {
		V level{};
		V kept{};
		V gain{};
		// NOLINTNEXTLINE(readability-identifier-naming): `step_fields` is the name poly_kernel reads.
		static constexpr auto step_fields = std::tuple{&StateT::level, &StateT::gain};
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&StateT::level, &StateT::kept, &StateT::gain};
	};
	using State = StateT<f32>;

	[[nodiscard]] static Coefficients coefficients(const Settings& settings) { return {.scale = settings.scale}; }
	template <class V>
	[[nodiscard]] static std::size_t run_length(const CoefficientsT<V>& /*c*/, const StateT<V>& /*s*/, std::size_t n) {
		return std::min<std::size_t>(n, 6);
	}
	template <class V>
	static void prepare_run(const CoefficientsT<V>& c, StateT<V>& s, std::size_t m) {
		s.gain = c.scale;
		s.kept = s.kept + V{static_cast<f32>(m)};
	}
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& /*c*/, const EmptyBlock& /*b*/,
	                                                   StateT<V>& s) {
		s.level = s.level + V{1.f};
		return s.level * s.gain;
	}
};

/// A generator whose run_length answers what its settings say, whatever it was asked, and which declares a prepare:
/// its State logs every call's count, in order.
struct Logged {
	using value_type = f32;
	using step_type = f32;
	struct Settings {
		std::size_t answer = 1;
	};
	struct Coefficients {
		std::size_t answer = 1;
	};
	struct State {
		std::size_t prepares = 0;
		std::size_t prepared_for = 0;
		std::size_t runs = 0;
		std::size_t steps = 0;
		std::array<std::size_t, 16> run{};
	};
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) { return {.answer = settings.answer}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, State& s, std::size_t n) {
		++s.prepares;
		s.prepared_for = n;
		return {};
	}
	[[nodiscard]] static std::size_t run_length(const Coefficients& c, const State& /*s*/, std::size_t /*n*/) {
		return c.answer;
	}
	static void prepare_run(const Coefficients& /*c*/, State& s, std::size_t m) { s.run.at(s.runs++) = m; }
	[[nodiscard]] static f32 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s) {
		return static_cast<f32>(++s.steps);
	}
};

} // namespace run_test
