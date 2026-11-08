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
#include "dsp-next/core/mode.h"
#include "dsp-next/core/pack_f32.h"
#include "dsp-next/core/sample.h"
#include "dsp-next/float/math/detail.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>
#include <type_traits>

/// @file
/// @brief fp::Svf: the topology-preserving state-variable filter (Simper/Cytomic), in float, written once over its
/// value type so Poly steps a copy per lane.
namespace deluge::dsp::fp {

/// @brief The trapezoidal SVF: g = tan(π·fc/fs), k = 1/Q, a1 = 1/(1 + g(g + k)), a2 = g·a1, a3 = g·a2.
/// Per sample: v3 = x − ic2; v1 = a1·ic1 + a2·v3; v2 = ic2 + a2·ic1 + a3·v3; ic1 ← 2·v1 − ic1; ic2 ← 2·v2 − ic2.
/// @details LOW_PASS returns v2, BAND_PASS v1, HIGH_PASS x − k·v1 − v2, NOTCH x − k·v1. Every mode moves the state
/// identically, so a mode change between blocks keeps the state. v1 peaks at Q at fc; the other modes at unity.
/// Stable at every setting the clamps allow.
struct Svf {
	using value_type = f32; ///< A sample, normalised to ±1.
	using step_type = f32;  ///< One step per sample.

	/// @brief What a step returns.
	enum class Mode : uint8_t {
		LOW_PASS,  ///< v2.
		BAND_PASS, ///< v1: gain Q at fc (k·v1 is the unity-gain band-pass).
		HIGH_PASS, ///< x − k·v1 − v2.
		NOTCH,     ///< x − k·v1.
	};
	using Modes = ModeList<Mode::LOW_PASS, Mode::BAND_PASS, Mode::HIGH_PASS, Mode::NOTCH>;

	/// @brief What the owner sets.
	struct Settings {
		f32 cutoff = 0.25f;         ///< fc/fs, clamped to [1/48000, 0.49].
		f32 q = 0.70710678f;        ///< Q, clamped to [0.5, 40].
		Mode mode = Mode::LOW_PASS; ///< What a step returns.
	};

	/// @brief k and the three gains, over V.
	template <class V>
	struct CoefficientsT {
		V k{};                      ///< 1/Q.
		V a1{};                     ///< 1/(1 + g(g + k)).
		V a2{};                     ///< g·a1.
		V a3{};                     ///< g·a2.
		Mode mode = Mode::LOW_PASS; ///< Shared by every lane.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields =
		    std::tuple{&CoefficientsT::k, &CoefficientsT::a1, &CoefficientsT::a2, &CoefficientsT::a3};
	};
	using Coefficients = CoefficientsT<f32>;

	/// @brief The two integrators, over V. StateT{} is silence.
	template <class V>
	struct StateT {
		V ic1{}; ///< The band-pass integrator.
		V ic2{}; ///< The low-pass integrator.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&StateT::ic1, &StateT::ic2};
	};
	using State = StateT<f32>;

	/// @brief The coefficients for @p settings.
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		// A NaN cutoff takes the lower bound, so tan_pi only ever sees a valid frequency.
		constexpr f32 kLowest = 1.f / 48000.f;
		constexpr f32 kHighest = 0.49f;
		const f32 requested = settings.cutoff;
		const f32 fc = std::clamp(requested >= kLowest ? requested : kLowest, kLowest, kHighest);
		const f32 q = std::clamp(settings.q, 0.5f, 40.f);
		const f32 g = static_cast<f32>(detail::tan_pi(static_cast<double>(fc)));
		const f32 k = 1.f / q;
		const f32 a1 = 1.f / (1.f + (g * (g + k)));
		const f32 a2 = g * a1;
		return {.k = k, .a1 = a1, .a2 = a2, .a3 = g * a2, .mode = settings.mode};
	}

	/// @brief One sample over V: f32 for one copy, Pack<f32, N> for one per lane.
	template <Mode M, class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& c, const EmptyBlock& /*b*/, StateT<V>& s,
	                                                   std::type_identity_t<V> x) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const V v3 = x - s.ic2;
		const V v1 = mul_add(c.a1 * s.ic1, c.a2, v3);
		const V v2 = mul_add(mul_add(s.ic2, c.a2, s.ic1), c.a3, v3);
		s.ic1 = (v1 + v1) - s.ic1;
		s.ic2 = (v2 + v2) - s.ic2;
		if constexpr (M == Mode::LOW_PASS) {
			return v2;
		}
		else if constexpr (M == Mode::BAND_PASS) {
			return v1;
		}
		else if constexpr (M == Mode::HIGH_PASS) {
			return (x - (c.k * v1)) - v2;
		}
		else {
			return x - (c.k * v1);
		}
	}
};

static_assert(processor_kernel<Svf>);

} // namespace deluge::dsp::fp
