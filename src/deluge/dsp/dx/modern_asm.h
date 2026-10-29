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

#include "dsp-next/core/types.h"
#include "dsp-next/fixed/dx7/graph.h"

#include <cstddef>
#include <cstdint>

/// @file
/// @brief The device's NEON modern operator (neon_fm_kernel.s) as the DX7 graph's vector operator, and the graph type
/// the app's voices use.

/// @brief The NEON FM kernel's signature (neon_fm_kernel.s).
using NeonFmKernel = void(const int32_t* in, const int32_t* busin, int32_t* out, int count, int32_t phase0,
                          int32_t freq, int32_t gain1, int32_t dgain);

/// @brief The NEON kernel @p Kernel as the graph's vector modern operator, for the NONE and BUS inputs.
/// @details The kernel takes a count that is a multiple of 4 and not 12k + 8, so the block is rounded up to one; it
/// then writes up to the next whole block of 12 past its first 4, at most kMaxBlock samples, into buffers of
/// kMaxBlock, 16-byte aligned. It computes the sine by a polynomial, not the table, so it is not bit-identical to the
/// operator.
template <NeonFmKernel* Kernel>
struct AsmModernVectorWith {
	static constexpr bool kRenders = true; ///< Renders in place of the operator.

	/// @brief One modern op's block: @p modulation (zeros for none) added to the phase, @p mix (zeros to set) added
	/// to the output.
	static void render(const deluge::dsp::q31* modulation, const deluge::dsp::q31* mix, deluge::dsp::q31* out,
	                   std::size_t n, deluge::dsp::q31 phase0, int32_t freq, int32_t gain1, int32_t dgain) {
		const auto len = static_cast<int>(n);
		const int nmod = 1 + ((len + 11) % 12);
		const int simd_n = nmod == 8 ? len + 4 : (len + 3) & ~3;
		Kernel(modulation, mix, out, simd_n, phase0, freq, gain1, dgain);
	}
};

#if defined(__ARM_NEON) && !defined(DELUGE_HOST) && !defined(DELUGE_DX_SCALAR)
extern "C" NeonFmKernel neon_fm_kernel;
using DxGraph = deluge::dsp::fixed::dx7::Graph<AsmModernVectorWith<&neon_fm_kernel>>;
#else
using DxGraph = deluge::dsp::fixed::dx7::Graph<>;
#endif
