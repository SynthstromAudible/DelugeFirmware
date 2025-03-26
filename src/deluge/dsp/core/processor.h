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
#include "concepts.h"
#include <algorithm>
#include <argon.hpp>
#include <span>
#include <tuple>

namespace deluge::dsp {

/// @brief A base class for generators that process a stream of samples.
/// @tparam T The type of the samples to process.
template <typename T>
struct BlockProcessor {
	/// @brief The type of the samples to process.
	using value_type = T;

	virtual ~BlockProcessor() = default;

	/// @brief Process a block of samples.
	/// @param input The input buffer of samples to process.
	/// @param output The output buffer to fill with processed samples.
	virtual void renderBlock(std::span<T> input, std::span<T> output) = 0;
};

/// @brief A base class for processors that are able to process a single sample at a time.
/// @tparam T The type of the samples to process.
template <typename T>
struct Processor : BlockProcessor<T> {
	/// @brief Process a single sample of type T.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	virtual T render(T sample) = 0;

	/// @brief Process a block of samples by calling render() for each sample.
	/// @param input The input buffer of samples to process.
	/// @param output The output buffer to fill with processed samples.
	void renderBlock(std::span<T> input, std::span<T> output) final {
		// If T is a scalar type, we process each sample sequentially
		std::ranges::transform(input, output.begin(), [this](T sample) { return render(sample); });
	}
};

/// @brief A base class for processors that are able to process a vector of samples using SIMD operations.
/// @tparam T The type of the samples to process.
template <typename T>
struct SIMDProcessor : BlockProcessor<T> {
	/// @brief Process a block of samples using SIMD operations.
	/// @param samples The input buffer of samples to process.
	/// @return The processed buffer of samples.
	virtual Argon<T> render(Argon<T> sample) = 0;

	/// @brief Process a block of samples by calling render() for each sample.
	/// @param input The input buffer of samples to process.
	/// @param output The output buffer to fill with processed samples.
	void renderBlock(std::span<T> input, std::span<T> output) final {
		auto input_view = argon::vectorize(input);
		auto output_view = argon::vectorize(output);

		for (auto input_it = input_view.begin(), output_it = output_view.begin();
		     input_it != input_view.end() && output_it != output_view.end(); ++input_it, ++output_it) {
			*output_it = render(*input_it);
		}
	}
};
} // namespace deluge::dsp
