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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <tuple>
#include <type_traits>

/// @file
/// @brief fp::OnePole: the topology-preserving one-pole low- and high-pass, in float, written once over its value
/// type so Poly steps a copy per lane.
namespace deluge::dsp::fp {

/// @brief The TPT one-pole (Zavalishin): v = (x − s)·G, lp = v + s, s ← lp + v, with G = g/(1 + g), g = tan(π·fc/fs).
/// @details LOW_PASS returns lp; HIGH_PASS returns x − lp. Both move s identically, so a mode change between blocks
/// keeps the state.
struct OnePole {
	using value_type = f32; ///< A sample, normalised to ±1.
	using step_type = f32;  ///< One step per sample.

	/// @brief What a step returns.
	enum class Mode : uint8_t {
		LOW_PASS,  ///< lp.
		HIGH_PASS, ///< x − lp.
	};
	using Modes = ModeList<Mode::LOW_PASS, Mode::HIGH_PASS>;

	/// @brief What the owner sets.
	struct Settings {
		f32 cutoff = 0.25f;         ///< fc/fs, clamped to [1/48000, 0.49].
		Mode mode = Mode::LOW_PASS; ///< What a step returns.
	};

	/// @brief The gain G, over V.
	template <class V>
	struct CoefficientsT {
		V G{};                      ///< g/(1 + g).
		Mode mode = Mode::LOW_PASS; ///< Shared by every lane.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&CoefficientsT::G};
	};
	using Coefficients = CoefficientsT<f32>;

	/// @brief The integrator's state, over V. StateT{} is silence.
	template <class V>
	struct StateT {
		V s{}; ///< The integrator.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&StateT::s};
	};
	using State = StateT<f32>;

	/// @brief G for @p settings' cutoff.
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
#pragma clang fp contract(off) reassociate(off)
		const f32 fc = std::clamp(settings.cutoff, 1.f / 48000.f, 0.49f);
		const f32 g = std::tan(std::numbers::pi_v<f32> * fc);
		return {.G = g / (1.f + g), .mode = settings.mode};
	}

	/// @brief One sample over V: f32 for one copy, Pack<f32, N> for one per lane.
	template <Mode M, class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& c, const EmptyBlock& /*b*/,
	                                                   StateT<V>& st, std::type_identity_t<V> x) {
#pragma clang fp contract(off) reassociate(off)
		const V v = (x - st.s) * c.G;
		const V lp = v + st.s;
		st.s = lp + v;
		if constexpr (M == Mode::LOW_PASS) {
			return lp;
		}
		else {
			return x - lp;
		}
	}
};

static_assert(processor_kernel<OnePole>);

} // namespace deluge::dsp::fp
