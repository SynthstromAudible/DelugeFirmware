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
#include "dsp_ng/core/types.hpp"
#include <argon.hpp>
#include <argon/vectorize/load.hpp>
#include <argon/vectorize/store.hpp>

namespace deluge::dsp {
/// @brief A base class for converters that process a block of samples.
/// @tparam T The type of the input samples.
/// @tparam U The type of the output samples.
template <typename T, typename U>
struct BlockConverter {
	using input_type = T;  ///< The type of the input samples
	using output_type = U; ///< The type of the output samples

	static_assert(!std::is_same_v<T, U>, "BlockConverter requires different types T and U.");

	/// @brief Destructor for the BlockConverter class.
	virtual ~BlockConverter() = default;

	/// @brief Convert a block of type T to type U.
	/// @param input The input buffer of type T to convert.
	/// @param output The output buffer of type U to fill with converted samples.
	virtual void renderBlock(Signal<T> input, Buffer<U> output) = 0;
};

/// @brief A base class for converters that convert a single sample.
/// @tparam T The type of the input samples.
/// @tparam U The type of the output samples.
template <typename T, typename U>
struct SampleConverter {
	using input_type = T;  ///< The type of the input samples
	using output_type = U; ///< The type of the output samples

	static_assert(!std::is_same_v<T, U>, "SampleConverter requires different types T and U.");

	/// @brief Destructor for the SampleConverter class.
	virtual ~SampleConverter() = default;

	/// @brief Convert a single sample of type T to type U.
	/// @param sample The input sample of type T to convert.
	/// @return The converted sample of type U.
	virtual U render(T sample) = 0;
};

/// @brief A base class for converters that process a block of samples and convert between two types.
/// @tparam T The type of the input samples.
/// @tparam U The type of the output samples.
template <typename T, typename U>
struct Converter : SampleConverter<T, U>, SampleConverter<U, T>, BlockConverter<T, U>, BlockConverter<U, T> {
	/// @brief Convert a block of type T to type U by calling render() for each sample.
	/// @param input The input buffer of type T to convert.
	/// @param output The output buffer of type U to fill with converted samples.
	void renderBlock(Signal<T> input, Buffer<U> output) final {
		for (size_t i = 0; i < input.size(); ++i) {
			output[i] = SampleConverter<T, U>::render(input[i]); // Call the process function for each sample
		}
	}
	/// @brief Convert a block of type U to type T by calling render() for each sample.
	/// @param input The input buffer of type U to convert.
	/// @param output The output buffer of type T to fill with converted samples.
	void renderBlock(Signal<U> input, Buffer<T> output) final {
		for (size_t i = 0; i < input.size(); ++i) {
			output[i] = SampleConverter<U, T>::render(input[i]); // Call the process function for each sample
		}
	}
};

/// @brief A base class for converters that process a vector of samples using SIMD operations.
/// @tparam T The type of the input samples.
/// @tparam U The type of the output samples.
template <typename T, typename U>
struct Converter<Argon<T>, Argon<U>> : SampleConverter<Argon<T>, Argon<U>>,
                                       SampleConverter<Argon<U>, Argon<T>>,
                                       BlockConverter<T, U>,
                                       BlockConverter<U, T> {
	/// @brief Convert a block of type T to type U using SIMD operations.
	/// @param input The input buffer of type T to convert.
	/// @param output The output buffer of type U to fill with converted samples.
	void renderBlock(Signal<T> input, Buffer<U> output) final {
		auto input_view = argon::vectorize::load(input);
		auto output_view = argon::vectorize::store(output);

		auto input_it = input_view.begin();
		auto output_it = output_view.begin();
		for (; input_it != input_view.end(); ++input_it, ++output_it) {
			*output_it = SampleConverter<Argon<T>, Argon<U>>::render(*input_it);
		}
	}

	/// @brief Convert a block of type U to type T using SIMD operations.
	/// @param input The input buffer of type U to convert.
	/// @param output The output buffer of type T to fill with converted samples.
	void renderBlock(Signal<U> input, Buffer<T> output) final {
		auto input_view = argon::vectorize::load(input);
		auto output_view = argon::vectorize::store(output);

		auto input_it = input_view.begin();
		auto output_it = output_view.begin();
		for (; input_it != input_view.end(); ++input_it, ++output_it) {
			*output_it = SampleConverter<Argon<U>, Argon<T>>::render(*input_it);
		}
	}
};
} // namespace deluge::dsp
