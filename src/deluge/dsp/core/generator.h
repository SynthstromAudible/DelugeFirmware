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
#include <algorithm>
#include <argon.hpp>
#include <span>

namespace deluge::dsp {
/// @brief A base class for generators that process a stream of samples.
/// @tparam T The type of the samples to generate.
template <typename T>
struct BlockGenerator {
	using value_type = T;

	virtual ~BlockGenerator() = default;

	/// @brief Generate a block of samples.
	/// @param buffer The output buffer to fill with generated samples.
	/// @param size The number of samples to generate.
	virtual void renderBlock(std::span<T> buffer) = 0;
};

/// @brief A base class for generators that process a single sample at a time.
/// @tparam T The type of the samples to generate.
template <typename T>
struct Generator : BlockGenerator<T> {
	/// @brief Generate a single sample of type T.
	/// @return The generated sample.
	virtual T render() = 0;

	// Note: typically there will also be some kind of function to
	// configure the generator's parameters, e.g. set frequency, amplitude, etc
	// before calling process.

	/// @brief Generate a block of samples by calling render() for each sample.
	/// @param buffer The output buffer to fill with generated samples.
	void renderBlock(std::span<T> buffer) final {
		// If T is a scalar type, we process each sample sequentially
		std::generate(buffer.begin(), buffer.end(), [this]() { return render(); });
	}
};

/// @brief A base class for generators that generate a vector of samples using SIMD operations.
/// @tparam T The type of the samples to generate.
template <typename T>
struct SIMDGenerator : BlockGenerator<T> {
	/// @brief Generate a vector of samples of type T.
	/// @return The generated vector of samples.
	virtual Argon<T> render() = 0;

	/// @brief Generate a block of samples by calling render() for each sample.
	/// @param buffer The output buffer to fill with generated samples.
	void renderBlock(std::span<T> buffer) final {
		for (Argon<T>& sample : argon::vectorize(buffer)) {
			sample = render();
		}
	}
};
} // namespace deluge::dsp
