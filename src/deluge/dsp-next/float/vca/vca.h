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
#include "dsp-next/core/pack_f32.h"
#include "dsp-next/core/poly_kernel.h"
#include "dsp-next/core/sample.h"

#include <tuple>
#include <type_traits>

/// @file
/// @brief fp::Vca<E>: a signal scaled by an envelope part and a gain, in float, written once over its value type so
/// Poly and Bank run a copy per lane.
namespace deluge::dsp::fp {

/// @brief The signal times an envelope generator's level, times a gain: y = (x · e) · gain, in that order.
/// @details @p E is an unmoded generator kernel with no per-block values (an @ref Envelope). Its coefficients and
/// state nest in the Vca's, so a lane carries its own program, stage and gain, and its events (`gate_on`, `gate_off`)
/// are forwarded: `on<&Vca<E>::gate_on>` reaches the envelope of a Mono, or of one lane of a Poly or Bank. The
/// envelope steps once per sample, whatever the signal.
/// @tparam E The envelope part.
template <class E>
struct Vca {
	static_assert(generator_kernel<E> && poly_kernel<E>, "a Vca's envelope part is a float generator kernel");
	static_assert(!deluge::dsp::detail::has_modes<E> && !deluge::dsp::detail::declares_prepare<E>,
	              "a Vca's envelope part has no modes and no per-block values");

	using value_type = f32; ///< A sample.
	using step_type = f32;  ///< One step per sample.

	/// @brief What the owner sets.
	struct Settings {
		E::Settings envelope{}; ///< The envelope part's settings.
		f32 gain = 1.f;         ///< The linear gain after the envelope.
	};

	/// @brief The envelope part's coefficients and the gain, over V.
	template <class V>
	struct CoefficientsT {
		E::template CoefficientsT<V> envelope{}; ///< The envelope part's coefficients.
		V gain{1.f};                             ///< The linear gain.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&CoefficientsT::envelope, &CoefficientsT::gain};
	};
	using Coefficients = CoefficientsT<f32>;

	/// @brief The envelope part's state, over V. StateT{} is silence.
	template <class V>
	struct StateT {
		E::template StateT<V> envelope{}; ///< The envelope part's state.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&StateT::envelope};
	};
	using State = StateT<f32>;

	/// @brief The coefficients for @p settings.
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		Coefficients c;
		c.envelope = E::coefficients(settings.envelope);
		c.gain = settings.gain;
		return c;
	}

	/// @brief Starts the envelope part's attack.
	static void gate_on(const Coefficients& c, State& s) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		E::gate_on(c.envelope, s.envelope);
	}
	/// @brief Starts the envelope part's release.
	static void gate_off(const Coefficients& c, State& s) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		E::gate_off(c.envelope, s.envelope);
	}

	/// @brief One sample over V: f32 for one copy, Pack<f32, N> for one per lane.
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& c, const EmptyBlock& b, StateT<V>& s,
	                                                   std::type_identity_t<V> x) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const V e = E::template step<V>(c.envelope, b, s.envelope);
		return (x * e) * c.gain;
	}
};

} // namespace deluge::dsp::fp
