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
#include "types.hpp"
#include <argon.hpp>
#include <argon/vectorize/load.hpp>
#include <argon/vectorize/store.hpp>

namespace deluge::dsp {
/// @brief A base class for mixers that process a stream of samples.
/// @tparam T The type of the samples to mix.
template <typename T>
struct BlockMixer {
	using value_type = T; ///< The type of the samples to mix.

	virtual ~BlockMixer() = default;

	/// @brief Mix a block of samples from two input buffers into an output buffer.
	/// @param inputs A span of input buffers to mix.
	/// @param output The output buffer to fill with the mixed samples.
	virtual void renderBlock(Buffer<T> input_a, Buffer<T> input_b, Buffer<T> output) = 0;
};

/// @brief A base class for mixers that process a single sample.
/// @tparam T The type of the samples to mix.
template <typename T>
struct SampleMixer {
	using value_type = T; ///< The type of the samples to mix.

	virtual ~SampleMixer() = default;

	/// @brief Mix two input samples into an output.
	/// @param input_a The first input sample to mix.
	/// @param input_b The second input sample to mix.
	/// @return The mixed sample.
	virtual T render(T input_a, T input_b) = 0;
};

/// @brief A base class for mixers that process a single sample at a time.
/// @tparam T The type of the samples to mix.
template <typename T>
struct Mixer : SampleMixer<T>, BlockMixer<T> {
	using value_type = T; ///< The type of the samples to mix.

	/// @brief Mix a block of samples from two input buffers into an output buffer.
	/// @param inputs A span of input buffers to mix.
	/// @param output The output buffer to fill with the mixed samples.
	void renderBlock(Buffer<T> input_a, Buffer<T> input_b, Buffer<T> output) final {
		for (size_t i = 0; i < std::min(input_a.size(), std::min(input_b.size(), output.size())); ++i) {
			output[i] = render(input_a[i], input_b[i]);
		}
	}
};

/// @brief A base class for mixers that process a vector of samples using SIMD operations.
/// @tparam T The type of the samples to mix.
template <typename T>
struct Mixer<Argon<T>> : SampleMixer<Argon<T>>, BlockMixer<T> {
	using value_type = T; ///< The type of the samples to mix.

	/// @brief Mix a block of samples from two input buffers into an output buffer.
	/// @param inputs A span of input buffers to mix.
	/// @param output The output buffer to fill with the mixed samples.
	void renderBlock(Signal<T> input_a, Signal<T> input_b, Buffer<T> output) final {
		auto input_a_view = argon::vectorize::load(input_a);
		auto input_b_view = argon::vectorize::load(input_b);
		auto output_view = argon::vectorize::store(output);

		auto a_it = input_a_view.begin();
		auto b_it = input_b_view.begin();
		auto output_it = output_view.begin();

		for (; a_it != input_a_view.end() && b_it != input_b_view.end() && output_it != output_view.end();
		     ++a_it, ++b_it, ++output_it) {
			*output_it = render(*a_it, *b_it);
		}
	}
};
} // namespace deluge::dsp
