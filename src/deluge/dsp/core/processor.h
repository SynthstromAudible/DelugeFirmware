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

template <typename T>
struct BlockProcessor {
	using value_type = T;

	/// @brief Process a block of samples.
	/// @param input The input buffer of samples to process.
	/// @param output The output buffer to fill with processed samples.
	virtual void processBlock(std::span<T> input, std::span<T> output) = 0;
};

template <typename T>
struct Processor : BlockProcessor<T> {
	/// @brief Process a single sample of type T.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	virtual T process(T sample) = 0;

	/// @brief Process a block of samples by calling process() for each sample.
	/// @param input The input buffer of samples to process.
	/// @param output The output buffer to fill with processed samples.
	void processBlock(std::span<T> input, std::span<T> output) override {
		// If T is a scalar type, we process each sample sequentially
		std::ranges::transform(input, output.begin(), [this](T sample) { return process(sample); });
	}
};

template <typename T>
struct SIMDProcessor : BlockProcessor<T> {
	/// @brief Process a block of samples using SIMD operations.
	/// @param samples The input buffer of samples to process.
	/// @return The processed buffer of samples.
	virtual Argon<T> process(Argon<T> sample) = 0;

	/// @brief Process a block of samples by calling process() for each sample.
	/// @param input The input buffer of samples to process.
	/// @param output The output buffer to fill with processed samples.
	void processBlock(std::span<T> input, std::span<T> output) override {
		auto input_view = argon::vectorize(input);
		auto output_view = argon::vectorize(output);

		for (auto input_it = input_view.begin(), output_it = output_view.begin();
		     input_it != input_view.end() && output_it != output_view.end(); ++input_it, ++output_it) {
			*output_it = process(*input_it);
		}
	}
};
