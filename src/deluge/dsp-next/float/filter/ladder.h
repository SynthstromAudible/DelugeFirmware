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
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

/// @file
/// @brief fp::Ladder: a zero-delay-feedback four-pole ladder in float, its response a mix of its taps, written once
/// over its value type so Poly steps a copy per lane.
namespace deluge::dsp::fp {

/// @brief A four-pole ladder of topology-preserving one-pole stages, its feedback loop solved in closed form.
/// @details Each stage is the TPT one-pole y = G·x + β·s (G = g/(1+g), β = 1 − G, g = tan(π·fc)). With the stages'
/// memories s₁…s₄, the last stage's output is G⁴·u + S, S = β·(G³s₁ + G²s₂ + G·s₃ + s₄), so the loop
/// u = in_gain·x − k·y₄ resolves to u = (in_gain·x − k·S)·r, r = 1/(1 + k·G⁴): no delay in the feedback, so the ladder
/// stays in tune and stable as the cutoff moves. k runs from 0 to 4.5: at 4 the loop gain at the cutoff is exactly 1,
/// and above it an oscillation at the cutoff grows until the saturator holds it.
///
/// **Response.** The output is w₀·u + w₁·y₁ + w₂·y₂ + w₃·y₃ + w₄·y₄, five weights per lane set by @ref coefficients:
/// lanes of one Poly may run different responses, and a response can morph between two presets.
///
/// **Flavour.** The mode: CLEAN saturates the resolved input once (@ref detail::soft_clip), so drive and resonance
/// compress rather than grow without bound.
struct Ladder {
	using value_type = f32; ///< A sample, normalised to ±1.
	using step_type = f32;  ///< One step per sample.

	/// @brief How the ladder saturates.
	enum class Mode : uint8_t {
		CLEAN, ///< One saturator on the resolved input.
	};
	using Modes = ModeList<Mode::CLEAN>;

	/// @brief A named response: its tap weights are @ref weights.
	enum class Response : uint8_t {
		LP24, ///< y₄: −24 dB per octave above the cutoff.
		LP12, ///< y₂: −12 dB per octave.
		HP,   ///< (1 − H)⁴ expanded over the taps: u − 4y₁ + 6y₂ − 4y₃ + y₄.
		BP,   ///< 2H²(1 − H)² expanded: 2y₂ − 4y₃ + 2y₄.
	};

	/// @brief The tap weights (u, y₁, y₂, y₃, y₄) of @p response.
	[[nodiscard]] static constexpr std::array<f32, 5> weights(Response response) {
		switch (response) {
		case Response::LP12:
			return {0.F, 0.F, 1.F, 0.F, 0.F};
		case Response::HP:
			return {1.F, -4.F, 6.F, -4.F, 1.F};
		case Response::BP:
			return {0.F, 0.F, 2.F, -4.F, 2.F};
		case Response::LP24:
		default:
			return {0.F, 0.F, 0.F, 0.F, 1.F};
		}
	}

	/// @brief What the owner sets.
	struct Settings {
		f32 cutoff = 0.1F;                  ///< fc/fs, clamped to [1/48000, 0.49]; NaN takes the lower bound.
		f32 resonance = 0.F;                ///< k, clamped to [0, 4.5]; above 4 it self-oscillates; NaN takes 0.
		f32 drive = 1.F;                    ///< Input gain into the saturator, clamped to [1, 8]; NaN takes 1.
		bool compensate = false;            ///< Scale the input by 1 + k, so the low-pass DC gain stays 1.
		Response response = Response::LP24; ///< The response at morph 0.
		Response morph_to = Response::LP24; ///< The response at morph 1.
		f32 morph = 0.F;                    ///< Between the two, clamped to [0, 1]; NaN takes 0.
		Mode mode = Mode::CLEAN;            ///< The flavour.
	};

	/// @brief The stage gain, the loop's terms and the tap weights, over V.
	template <class V>
	struct CoefficientsT {
		V G{};                   ///< g/(1 + g).
		V G4{};                  ///< G⁴.
		V beta{};                ///< 1 − G.
		V k{};                   ///< The resonance.
		V r{};                   ///< 1/(1 + k·G⁴): the loop's closed-form gain.
		V in_gain{};             ///< drive·(1 + k) with compensation, else drive.
		std::array<V, 5> w{};    ///< The tap weights (u, y₁, y₂, y₃, y₄).
		Mode mode = Mode::CLEAN; ///< Shared by every lane.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields =
		    std::tuple{&CoefficientsT::G, &CoefficientsT::G4,      &CoefficientsT::beta, &CoefficientsT::k,
		               &CoefficientsT::r, &CoefficientsT::in_gain, &CoefficientsT::w};
	};
	using Coefficients = CoefficientsT<f32>;

	/// @brief The four stages' memories, over V. StateT{} is silence.
	template <class V>
	struct StateT {
		std::array<V, 4> s{}; ///< s₁…s₄.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&StateT::s};
	};
	using State = StateT<f32>;

	/// @brief The coefficients for @p settings, computed in double and rounded once.
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const f32 fc = clamped(settings.cutoff, 1.F / 48000.F, 0.49F);
		const f32 k = clamped(settings.resonance, 0.F, 4.5F);
		const f32 drive = clamped(settings.drive, 1.F, 8.F);
		const f32 morph = clamped(settings.morph, 0.F, 1.F);
		const double g = detail::tan_pi(static_cast<double>(fc));
		const double big_g = g / (1.0 + g);
		const double g4 = (big_g * big_g) * (big_g * big_g);
		const auto k_d = static_cast<double>(k);
		const double gain = static_cast<double>(drive) * (settings.compensate ? 1.0 + k_d : 1.0);
		Coefficients c{
		    .G = static_cast<f32>(big_g),
		    .G4 = static_cast<f32>(g4),
		    .beta = static_cast<f32>(1.0 - big_g),
		    .k = k,
		    .r = static_cast<f32>(1.0 / (1.0 + (k_d * g4))),
		    .in_gain = static_cast<f32>(gain),
		    .w = {},
		    .mode = settings.mode,
		};
		const std::array<f32, 5> from = weights(settings.response);
		const std::array<f32, 5> to = weights(settings.morph_to);
		const auto m = static_cast<double>(morph);
		for (std::size_t i = 0; i < 5; ++i) {
			const auto a = static_cast<double>(from[i]);
			c.w[i] = static_cast<f32>(a + (m * (static_cast<double>(to[i]) - a)));
		}
		return c;
	}

	/// @brief The output for @p taps (u, y₁…y₄) and weights @p w: w₀·u + w₁·y₁ + … + w₄·y₄, summed left to right.
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static V mix(const std::array<V, 5>& taps, const std::array<V, 5>& w) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		V out = w[0] * taps[0];
		for (std::size_t i = 1; i < 5; ++i) {
			out = mul_add(out, w[i], taps[i]);
		}
		return out;
	}

	/// @brief Steps the four stages from @p u, committing @p s: returns the taps (u, y₁…y₄).
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static std::array<V, 5> stages(const CoefficientsT<V>& c, StateT<V>& s, V u) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		std::array<V, 5> taps{u, u, u, u, u};
		V in = u;
		for (std::size_t i = 0; i < 4; ++i) {
			const V v = (in - s.s[i]) * c.G;
			const V y = v + s.s[i];
			s.s[i] = y + v;
			taps[i + 1] = y;
			in = y;
		}
		return taps;
	}

	/// @brief One sample over V: f32 for one copy, Pack<f32, N> for one per lane.
	template <Mode M, class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& c, const EmptyBlock& /*b*/, StateT<V>& s,
	                                                   std::type_identity_t<V> x) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		// S = β·(G³s₁ + G²s₂ + G·s₃ + s₄), in Horner form over G.
		const V feedback = c.beta * mul_add(s.s[3], c.G, mul_add(s.s[2], c.G, mul_add(s.s[1], c.G, s.s[0])));
		const V u = ((c.in_gain * x) - (c.k * feedback)) * c.r;
		return mix(stages(c, s, detail::soft_clip(u)), c.w);
	}

private:
	/// @p value clamped to [@p lo, @p hi]; NaN takes @p lo.
	[[nodiscard]] static constexpr f32 clamped(f32 value, f32 lo, f32 hi) {
		return std::clamp(value >= lo ? value : lo, lo, hi);
	}
};

static_assert(processor_kernel<Ladder>);

} // namespace deluge::dsp::fp
