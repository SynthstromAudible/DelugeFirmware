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
#include <algorithm>
#include <argon.hpp>
#include <argon/vectorize/store.hpp>

namespace deluge::dsp {
/// @brief A base class for objects that generate a stream of samples.
/// @tparam T The type of the samples to generate.
template <typename T>
struct BlockGenerator {
	using value_type = T;

	virtual ~BlockGenerator() = default;

	/// @brief Generate a block of samples.
	/// @param buffer The output buffer to fill with generated samples.
	/// @param size The number of samples to generate.
	virtual void renderBlock(Buffer<T> buffer) = 0;
};

template <typename T>
struct SampleGenerator {
	using value_type = T;

	virtual ~SampleGenerator() = default;

	/// @brief Generate a single sample.
	/// @return The generated sample.
	virtual T render() = 0;
};

/// @brief A base class for objects that generate a single sample at a time.
/// @tparam T The type of the samples to generate.
template <typename T>
struct Generator : SampleGenerator<T>, BlockGenerator<T> {
	using value_type = T;

	// Note: typically there will also be some kind of function to
	// configure the generator's parameters, e.g. set frequency, amplitude, etc
	// before calling process.

	/// @brief Generate a block of samples by calling render() for each sample.
	/// @param buffer The output buffer to fill with generated samples.
	void renderBlock(Buffer<T> buffer) final {
		for (T& sample : buffer) {
			sample = this->render();
		}
	}
};

/// @brief A base class for objects that generate a vector of samples using SIMD operations.
/// @tparam T The type of the samples to generate.
template <typename T>
struct Generator<Argon<T>> : SampleGenerator<Argon<T>>, BlockGenerator<T> {
	using value_type = T;

	/// @brief Generate a block of samples by calling render() for each sample.
	/// @param buffer The output buffer to fill with generated samples.
	void renderBlock(Buffer<T> buffer) final {
		for (Argon<T>& sample : argon::vectorize::store(buffer)) {
			sample = this->render();
		}
	}
};
} // namespace deluge::dsp
