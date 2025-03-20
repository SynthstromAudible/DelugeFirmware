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
#include <algorithm>
#include <argon.hpp>
#include <span>

template <typename T, typename U>
struct BlockAdapter {
	static_assert(!std::is_same_v<T, U>, "BlockAdapter requires different types T and U.");

	/// @brief Destructor for the BlockAdapter class.
	virtual ~BlockAdapter() = default;

	/// @brief Convert a block of type T to type U.
	/// @param input The input buffer of type T to convert.
	/// @param output The output buffer of type U to fill with converted samples.
	virtual void renderBlock(std::span<T> input, std::span<U> output) = 0;
};

template <typename T, typename U>
struct Adapter : BlockAdapter<T, U> {
	/// @brief Destructor for the Adaptor class.
	virtual ~Adapter() = default;

	/// @brief Convert a single sample of type T to type U.
	/// @param sample The input sample of type T to convert.
	/// @return The converted sample of type U.
	virtual U render(T sample) = 0;

	/// @brief Convert a block of type T to type U by calling render() for each sample.
	/// @param input The input buffer of type T to convert.
	/// @param output The output buffer of type U to fill with converted samples.
	virtual void renderBlock(std::span<T> input, std::span<U> output) {
		// If T and U are different types, we convert each sample sequentially
		std::ranges::transform(input, output.begin(), [this](T sample) { return render(sample); });
	}
};

template <typename T, typename U>
struct SIMDAdapter : BlockAdapter<T, U> {
	/// @brief Destructor for the SIMDAdapter class.
	virtual ~SIMDAdapter() = default;

	/// @brief Convert a vector of samples of type T to type U using SIMD operations.
	/// @param sample The input samples of type T to convert.
	/// @return The converted samples of type U.
	virtual Argon<U> render(Argon<T> sample) = 0;

	/// @brief Convert a block of type T to type U using SIMD operations.
	/// @param input The input buffer of type T to convert.
	/// @param output The output buffer of type U to fill with converted samples.
	virtual void renderBlock(std::span<T> input, std::span<U> output) {
		auto input_view = argon::vectorize(input);
		auto output_view = argon::vectorize(output);
		auto size = std::min(input_view.size(), output_view.size());
		for (size_t i = 0; i < size; ++i) {
			output_view[i] = render(input_view[i]); // Call the process function for each vector
		}
	}
};
