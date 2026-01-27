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

#pragma once
#include "coefficients.hpp"
#include "dsp_ng/core/processor.hpp"
#include "dsp_ng/core/types.hpp"

namespace deluge::dsp::filters::iir {
template <size_t num_stages>
class BiquadDF1 : public BlockProcessor<fixed_point::Sample> {
	using State = std::array<q31_t, 4>;

public:
	void setCoefficients(size_t stage, Coefficients<q31_t> coefficients, size_t scale) {
		coefficients_[stage] = Coefficients<q31_t>{
		    .b0 = coefficients.b0,
		    .b1 = coefficients.b1,
		    .b2 = coefficients.b2,
		    .a0 = -coefficients.a0, // Flipped sign!
		    .a1 = -coefficients.a1, // Flipped sign!
		    .a2 = -coefficients.a2, // Flipped sign!
		};
		scale_[stage] = scale;
	}

	void setCoefficients(Coefficients<q31_t> coefficients, size_t scale) {
		for (size_t stage = 0; stage < num_stages; ++stage) {
			setCoefficients(stage, coefficients, scale);
		}
	}

	void renderBlock(fixed_point::Signal input, fixed_point::Buffer output) final {
		std::span q31_input{reinterpret_cast<const q31_t*>(input.data()), input.size()};
		std::span q31_output{reinterpret_cast<q31_t*>(output.data()), output.size()};

		const q31_t* input_it = q31_input.data();
		q31_t* output_it = q31_output.data();

		for (size_t stage = 0; stage < num_stages; ++stage) {
			const uint32_t shift = 31U - scale_[stage];

			auto coeffs = coefficients_[stage];
			auto [Xn1, Xn2, Yn1, Yn2] = states_[stage];

			for (size_t i = 0; i < q31_input.size(); ++i) {
				q31_t Xn = *input_it++;

				// acc =  b0 * x[n] + b1 * x[n-1] + b2 * x[n-2] + a1 * y[n-1] + a2 * y[n-2]
				q63_t acc = ((q63_t)coeffs.b0 * Xn);
				acc += ((q63_t)coeffs.b1 * Xn1);
				acc += ((q63_t)coeffs.b2 * Xn2);
				acc += ((q63_t)coeffs.a1 * Yn1);
				acc += ((q63_t)coeffs.a2 * Yn2);

				acc >>= shift; // convert to q31

				*output_it++ = (q31_t)acc;

				// Every time after the output is computed state should be updated.
				Xn2 = Xn1;
				Xn1 = Xn;
				Yn2 = Yn1;
				Yn1 = (q31_t)acc;
			}

			/* Store the updated state variables back into the pState array */
			states_[stage] = {Xn1, Xn2, Yn1, Yn2};

			/* The first stage goes from the input buffer to the output buffer. */
			/* Subsequent numStages occur in-place in the output buffer */
			input_it = q31_output.data();
		}
	}

private:
	std::array<State, num_stages> states_;
	std::array<Coefficients<q31_t>, num_stages> coefficients_;

	/// @brief Used to scale coefficients: c * (2**scale)
	std::array<size_t, num_stages> scale_;
};

} // namespace deluge::dsp::filters::iir
