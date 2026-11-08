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
#include "dsp-next/core/sample.h"
#include "dsp-next/float/osc/phase.h"
#include "dsp-next/float/osc/saw_bands.h"

#include <algorithm>
#include <tuple>

/// @file
/// @brief fp::TableOscillator: a saw read from the band-limited table set by linear interpolation, one band and
/// position per lane.
namespace deluge::dsp::fp {

/// @brief A band-limited saw read from @ref saw_bands: the band for the pitch, the position linearly interpolated.
/// @details Call build_saw_bands() once before rendering. Written once over its value type; under Poly each lane's band
/// and position differ, so the two reads per sample are gathers.
struct TableOscillator {
	using value_type = f32; ///< A sample, about ±1 (the band's Gibbs overshoot rides above).
	using step_type = f32;  ///< One step per sample.

	/// @brief What the owner sets.
	struct Settings {
		f32 frequency = 0.01f; ///< f0/fs, clamped to [1e-6, 0.45].
	};

	/// @brief The phase increment and the band's offset, over V.
	template <class V>
	struct CoefficientsT {
		V inc{};    ///< f0/fs.
		V offset{}; ///< The band's first sample index in saw_bands(), as f32.
		// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
		static constexpr auto fields = std::tuple{&CoefficientsT::inc, &CoefficientsT::offset};
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

	/// @brief The increment and the band offset for @p settings.
	[[nodiscard]] static Coefficients coefficients(const Settings& settings) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const f32 inc = std::clamp(settings.frequency, 1e-6f, 0.45f);
		return {.inc = inc, .offset = static_cast<f32>(saw_band_for(inc) * (kSawBandSize + 1))};
	}

	/// @brief One sample over V: f32 for one copy, Pack<f32, N> for one per lane.
	template <class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const CoefficientsT<V>& c, const EmptyBlock& /*b*/,
	                                                   StateT<V>& st) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		const V position = st.phase * V{static_cast<f32>(kSawBandSize)};
		const V whole = trunc_nonnegative(position);
		const V frac = position - whole;
		const V index = c.offset + whole;
		const f32* table = saw_bands(); // one read per step; no call
		const V a = gather(table, index);
		const V b = gather(table, index + V{1.f});
		step_phase(st.phase, c.inc);
		return mul_add(a, frac, b - a);
	}
};

static_assert(generator_kernel<TableOscillator>);

} // namespace deluge::dsp::fp
