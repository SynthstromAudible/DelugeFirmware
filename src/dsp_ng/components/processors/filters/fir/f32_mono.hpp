/**
 * Copyright (c) 2025 Katherine Whitlock
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

/// @note This is heavily based on CMSIS' arm_fir_f32.c, licensed under Apache 2.0

#pragma once

#include "dsp_ng/core/processor.hpp"
#include <algorithm>
#include <argon.hpp>
#include <array>
#include <span>

namespace deluge::dsp::filters::fir {

/** FIR processor using SIMD inner product */
template <size_t order, size_t max_block_size>
class Mono : public BlockProcessor<float> {
public:
	void set_coeffs(const std::span<const float> data) { std::reverse_copy(data.begin(), data.end(), coeffs_.begin()); }

	void renderBlock(std::span<const float> input, std::span<float> output) final {
		using namespace argon;
		const size_t block_size = input.size();

		std::span<float> filter_state{state_.begin(), order - 1};
		std::span<float> input_state{&state_[order - 1U], block_size};

		std::ranges::copy(input, input_state.begin());

		const size_t neon_end = block_size & ~0x7;
		for (size_t idx = 0; idx < neon_end; idx += 8) {
			Argon<float> accv0 = 0.f;
			Argon<float> accv1 = 0.f;

			auto [x0, x1] = Argon<float>::LoadMulti<2>(&filter_state[idx]);

			size_t tap;
			for (tap = 0; tap < (order & ~0x3); tap += 4) {
				// acc =  b[numTaps-1] * x[n-numTaps-1] + b[numTaps-2] * x[n-numTaps-2] +
				//        b[numTaps-3] * x[n-numTaps-3] +...+ b[0] * x[0]
				const auto b = Argon<float>::Load(&coeffs_[tap]);

				auto x2 = Argon<float>::Load(&filter_state[idx + tap + 8]);

				auto xa = x0;
				auto xb = x1;
				accv0 = accv0.MultiplyAdd(xa, b[0]);
				accv1 = accv1.MultiplyAdd(xb, b[0]);

				xa = x0.Extract<1>(x1);
				xb = x1.Extract<1>(x2);

				accv0 = accv0.MultiplyAdd(xa, b[1]);
				accv1 = accv1.MultiplyAdd(xb, b[1]);

				xa = x0.Extract<2>(x1);
				xb = x1.Extract<2>(x2);

				accv0 = accv0.MultiplyAdd(xa, b[2]);
				accv1 = accv1.MultiplyAdd(xb, b[2]);

				xa = x0.Extract<3>(x1);
				xb = x1.Extract<3>(x2);

				accv0 = accv0.MultiplyAdd(xa, b[3]);
				accv1 = accv1.MultiplyAdd(xb, b[3]);

				x0 = x1;
				x1 = x2;
			}

			auto x2 = Argon<float>::Load(&filter_state[idx + tap + 8]);

			/* Tail */
			const size_t orderin_tail = order & 3;
			if (orderin_tail == 1) {
				accv0 = accv0.MultiplyAdd(x0, coeffs_[tap]);
				accv1 = accv1.MultiplyAdd(x1, coeffs_[tap]);
			}
			else if (orderin_tail == 2) {
				accv0 = accv0.MultiplyAdd(x0, coeffs_[tap]);
				accv1 = accv1.MultiplyAdd(x1, coeffs_[tap]);
				tap++;

				auto xa = x0.Extract<1>(x1);
				auto xb = x1.Extract<1>(x2);

				accv0 = accv0.MultiplyAdd(xa, coeffs_[tap]);
				accv1 = accv1.MultiplyAdd(xb, coeffs_[tap]);
			}
			else if (orderin_tail == 3) {
				accv0 = accv0.MultiplyAdd(x0, coeffs_[tap]);
				accv1 = accv1.MultiplyAdd(x1, coeffs_[tap]);
				tap++;

				auto xa = x0.Extract<1>(x1);
				auto xb = x1.Extract<1>(x2);

				accv0 = accv0.MultiplyAdd(xa, coeffs_[tap]);
				accv1 = accv1.MultiplyAdd(xb, coeffs_[tap]);
				tap++;

				xa = x0.Extract<2>(x1);
				xb = x1.Extract<2>(x2);

				accv0 = accv0.MultiplyAdd(xa, coeffs_[tap]);
				accv1 = accv1.MultiplyAdd(xb, coeffs_[tap]);
			}

			/* The result is stored in the destination buffer. */
			argon::store(&output[idx], accv0, accv1);
		}

		/* Tail */
		for (size_t idx = neon_end; idx < block_size; ++idx) {
			float acc = 0.0f;
#pragma GCC novector
			for (size_t i = 0; i < order; ++i) {
				// acc = b[numTaps-1] * x[n-numTaps-1] + b[numTaps-2] * x[n-numTaps-2] +
				//       b[numTaps-3] * x[n-numTaps-3] +...+ b[0] * x[0]
				acc += state_[idx + i] * coeffs_[i];
			}

			/* The result is stored in the destination buffer. */
			output[idx] = acc;
		}

		// Copy numTaps - 1 samples to the start of the state buffer
		// This prepares the state buffer for the next function call.
		std::copy(&state_[block_size], &state_[block_size + order - 1], state_.begin());
	}

private:
	std::array<float, order + max_block_size - 1> state_;
	std::array<float, order> coeffs_;
};

} // namespace deluge::dsp::filters::fir
