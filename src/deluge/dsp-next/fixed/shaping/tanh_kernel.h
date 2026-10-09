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
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/shaping/tanh.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief Tanh<Drive>: @ref tanh at a compile-time drive, as a stateless processor kernel, so that it can sit in a
/// chain. It has its own header so that a caller of the plain function needs no kernel core.
namespace deluge::dsp::fixed {

/// @brief Tanh saturation at drive 2^@p Drive with no memory: each step is @ref tanh<Drive> of its sample, Q31 in and
/// Q31 out, shifted down by Drive + 2.
/// @tparam Drive The drive as a power of two. Saturation is at 32 − Drive bits for every drive.
template <uint32_t Drive>
requires(Drive <= 31)
struct Tanh {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.

	/// @brief Nothing to set: the drive is the template's.
	struct Settings {};
	/// @brief Nothing to read.
	struct Coefficients {};
	/// @brief No memory.
	struct State {};

	/// @brief The empty coefficients.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& /*s*/) {
		return {};
	}

	/// @brief One sample through the curve.
	/// @param x The input, Q31.
	/// @return @ref tanh<Drive> of @p x, Q31.
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/,
	                                                     State& /*s*/, q31 x) {
		return tanh<Drive>(x);
	}
};

static_assert(processor_kernel<Tanh<3>>);
static_assert(!std::is_polymorphic_v<Tanh<3>>);

} // namespace deluge::dsp::fixed
