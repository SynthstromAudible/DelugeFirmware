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
#include "processor.h"
#include <argon.hpp>
#include <span>

namespace deluge::dsp {
/// @brief A base class for mixers that process a stream of samples.
/// @tparam T The type of the samples to mix.
template <typename T>
struct BlockMixer {
	using value_type = T;

	virtual ~BlockMixer() = default;

	/// @brief Mix a block of samples from two input buffers into an output buffer.
	/// @param inputs A span of input buffers to mix.
	/// @param output The output buffer to fill with the mixed samples.
	virtual void renderBlock(std::span<T> input_a, std::span<T> input_b, std::span<T> output) = 0;
};

/// @brief A base class for mixers that process a single sample at a time.
/// @tparam T The type of the samples to mix.
template <typename T>
struct Mixer : BlockMixer<T> {
	/// @brief Mix two input samples into an output.
	/// @param input_a The first input sample to mix.
	/// @param input_b The second input sample to mix.
	/// @return The mixed sample.
	virtual T render(T input_a, T input_b) = 0;

	/// @brief Mix a block of samples from two input buffers into an output buffer.
	/// @param inputs A span of input buffers to mix.
	/// @param output The output buffer to fill with the mixed samples.
	void renderBlock(std::span<T> input_a, std::span<T> input_b, std::span<T> output) override {
		for (size_t i = 0; i < std::min(input_a.size(), std::min(input_b.size(), output.size())); ++i) {
			output[i] = render(input_a[i], input_b[i]);
		}
	}
};

/// @brief A base class for mixers that process a vector of samples using SIMD operations.
/// @tparam T The type of the samples to mix.
template <typename T>
struct SIMDMixer : BlockMixer<T> {
	/// @brief Mix a vector of samples from two inputs into an output.
	/// @param input_a The first input sample to mix.
	/// @param input_b The second input sample to mix.
	/// @return The mixed sample.
	virtual Argon<T> render(Argon<T> input_a, Argon<T> input_b) = 0;

	/// @brief Mix a block of samples from two input buffers into an output buffer.
	/// @param inputs A span of input buffers to mix.
	/// @param output The output buffer to fill with the mixed samples.
	void renderBlock(std::span<T> input_a, std::span<T> input_b, std::span<T> output) override {
		auto input_a_view = argon::vectorize(input_a);
		auto input_b_view = argon::vectorize(input_b);
		auto output_view = argon::vectorize(output);

		for (auto it_a = input_a_view.begin(), it_b = input_b_view.begin(), it_out = output_view.begin();
		     it_a != input_a_view.end() && it_b != input_b_view.end() && it_out != output_view.end();
		     ++it_a, ++it_b, ++it_out) {
			*it_out = render(*it_a, *it_b);
		}
	}
};
} // namespace deluge::dsp
