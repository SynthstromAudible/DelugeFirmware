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
#include "dsp-next/core/pack.h"
#include "dsp-next/core/pack_f32.h"
#include "dsp-next/core/sample.h"
#include "dsp-next/float/osc/phase.h"

#include <algorithm>
#include <cstdint>
#include <tuple>

/// @file
/// @brief fp::BlepOscillator: a PolyBLEP saw and pulse, in float, written once over its value type so Poly runs a copy
/// per lane.
namespace deluge::dsp::fp {

/// @brief The two-sample PolyBLEP residual at a wrap.
/// @param t The phase, in [0, 1).
/// @param inc The phase increment.
/// @param inv_inc 1/inc.
/// @return The correction for a downward step of 2 at phase 0: nonzero only within one increment of the wrap.
template <class V>
[[nodiscard]] [[gnu::always_inline]] inline V blep(V t, V inc, V inv_inc) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	const V after = t * inv_inc;             // just after the wrap: t < inc
	const V before = (t - V{1.f}) * inv_inc; // just before it: t > 1 - inc
	const V r_after = (after + after) - (after * after) - V{1.f};
	const V r_before = (before * before) + (before + before) + V{1.f};
	return select(t < inc, r_after, select(t > V{1.f} - inc, r_before, V{0.f}));
}

/// @brief A band-limited saw or pulse: the naive waveform less a PolyBLEP residual at each discontinuity.
/// @details SAW rises from -1 to +1 over a cycle. PULSE is +1 for the last `width` of the cycle and -1 before it,
/// built as the saw less the saw a `width` later, so each edge carries its own residual.
struct BlepOscillator {
	using value_type = f32; ///< A sample, normalised to ±1.
	using step_type = f32;  ///< One step per sample.

	/// @brief The waveform.
	enum class Mode : uint8_t {
		SAW,   ///< A rising ramp.
		PULSE, ///< A rectangle of duty `width`.
	};
	using Modes = ModeList<Mode::SAW, Mode::PULSE>;

	/// @brief What the owner sets.
	struct Settings {
		f32 frequency = 0.01f; ///< f0/fs, clamped to [1e-6, 0.45].
		f32 width = 0.5f;      ///< PULSE: the fraction of the cycle high, clamped to [0.02, 0.98].
		Mode mode = Mode::SAW; ///< The waveform.
	};

	/// @brief The phase increment, its reciprocal and the duty, over V.
	template <class V>
	struct CoefficientsT {
		V inc{};               ///< f0/fs.
		V inv_inc{};           ///< 1/inc.
		V width{};             ///< PULSE duty.
		Mode mode = Mode::SAW; ///< Shared by every lane.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&CoefficientsT::inc, &CoefficientsT::inv_inc, &CoefficientsT::width};
	};
	using Coefficients = CoefficientsT<f32>;

	/// @brief The phase, over V. StateT{} starts a cycle.
	template <class V>
	struct StateT {
		V phase{}; ///< In [0, 1).
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&StateT::phase};
	};
	using State = StateT<f32>;

	/// @brief The increment, its reciprocal and the duty for @p settings.
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const f32 inc = std::clamp(settings.frequency, 1e-6f, 0.45f);
		return {
		    .inc = inc, .inv_inc = 1.f / inc, .width = std::clamp(settings.width, 0.02f, 0.98f), .mode = settings.mode};
	}

	/// @brief One sample over V: f32 for one copy, Pack<f32, N> for one per lane.
	template <Mode M, class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& c, const EmptyBlock& /*b*/,
	                                                   StateT<V>& st) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const V t = st.phase;
		const V saw = ((t + t) - V{1.f}) - blep(t, c.inc, c.inv_inc);
		V out = saw;
		if constexpr (M == Mode::PULSE) {
			V t2 = t + c.width;
			t2 = select(t2 >= V{1.f}, t2 - V{1.f}, t2);
			const V saw2 = ((t2 + t2) - V{1.f}) - blep(t2, c.inc, c.inv_inc);
			out = (saw - saw2) + ((c.width + c.width) - V{1.f});
		}
		step_phase(st.phase, c.inc);
		return out;
	}
};

static_assert(generator_kernel<BlepOscillator>);

} // namespace deluge::dsp::fp
