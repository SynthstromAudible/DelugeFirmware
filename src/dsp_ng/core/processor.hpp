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
#include <argon/vectorize/load_interleaved.hpp>
#include <argon/vectorize/store.hpp>
#include <argon/vectorize/store_interleaved.hpp>

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
	virtual void renderBlock(Signal<T> input, Buffer<T> output) = 0;
};

/// @brief A base class for processors that process a single sample at a time.
/// @note Only inherit from this class if you plan on also inheriting from BlockProcessor and providing a custom
/// implementation of renderBlock().
/// @tparam T The type of the samples to process.
template <typename T>
struct SampleProcessor {
	virtual ~SampleProcessor() = default;

	/// @brief Process a single sample of type T.
	/// @param sample The input sample to process.
	/// @return The processed sample.
	virtual T render(T sample) = 0;
};

/// @brief A base class for processors.
/// @tparam T The type of the samples to process.
template <typename T>
struct Processor : public SampleProcessor<T>, BlockProcessor<T> {
	/// @brief Process a block of samples by calling render() for each sample.
	/// @param input The input buffer of samples to process.
	/// @param output The output buffer to fill with processed samples.
	void renderBlock(Signal<T> input, Buffer<T> output) final {
		for (size_t i = 0; i < input.size(); ++i) {
			output[i] = this->render(input[i]); // Call the process function for each sample
		}
	}
};

/// @brief A base class for processors that are able to process a vector of samples using SIMD operations.
/// @tparam T The type of the samples to process.
template <typename T>
struct Processor<Argon<T>> : SampleProcessor<Argon<T>>, BlockProcessor<T> {
	/// @brief Process a block of samples by calling render() for each sample.
	/// @param input The input buffer of samples to process.
	/// @param output The output buffer to fill with processed samples.
	void renderBlock(Signal<T> input, Buffer<T> output) final {
		auto input_view = argon::vectorize::load(input);
		auto output_view = argon::vectorize::store(output);

		auto input_it = input_view.begin();
		auto output_it = output_view.begin();
		for (; input_it != input_view.end(); ++input_it, ++output_it) {
			*output_it = this->render(*input_it);
		}
	};
};

/// @brief A base class for processors that are able to process a vector of samples using SIMD operations.
/// @tparam T The type of the samples to process.
template <>
struct Processor<Argon<q31_t>> : SampleProcessor<Argon<q31_t>>, BlockProcessor<fixed_point::Sample> {
	/// @brief Process a block of samples by calling render() for each sample.
	/// @param input The input buffer of samples to process.
	/// @param output The output buffer to fill with processed samples.
	void renderBlock(fixed_point::Signal input, fixed_point::Buffer output) final {
		auto input_view = argon::vectorize::load(reinterpret_cast<std::span<const q31_t>&>(input));
		auto output_view = argon::vectorize::store(reinterpret_cast<std::span<q31_t>&>(output));

		auto input_it = input_view.begin();
		auto output_it = output_view.begin();
		for (; input_it != input_view.end(); ++input_it, ++output_it) {
			*output_it = this->render(*input_it);
		}
	};
};

/// @brief A base class for processors that process stereo samples using SIMD operations.
/// @tparam T The type of the samples to process.
template <typename T>
struct Processor<StereoSample<Argon<T>>> : SampleProcessor<StereoSample<Argon<T>>>, BlockProcessor<StereoSample<T>> {
	/// @brief Process a block of samples by calling render() for each sample.
	/// @param input The input buffer of samples to process.
	/// @param output The output buffer to fill with processed samples.
	void renderBlock(StereoSignal<T> input, StereoBuffer<T> output) final {
		auto input_view = argon::vectorize::load_interleaved<q31_t, 2>(input);
		auto output_view = argon::vectorize::store_interleaved<q31_t, 2>(output);

		auto input_it = input_view.begin();
		auto output_it = output_view.begin();
		for (; input_it != input_view.end() && output_it != output_view.end(); ++input_it, ++output_it) {
			auto [left_in, right_in] = *input_it;
			*output_it = this->render({left_in, right_in});
		}
	};
};

/// @brief A base class for processors that process stereo samples using SIMD operations.
/// @tparam T The type of the samples to process.
template <>
struct Processor<StereoSample<Argon<q31_t>>> : SampleProcessor<StereoSample<Argon<q31_t>>>,
                                               BlockProcessor<fixed_point::StereoSample> {
	/// @brief Process a block of samples by calling render() for each sample.
	/// @param input The input buffer of samples to process.
	/// @param output The output buffer to fill with processed samples.
	void renderBlock(fixed_point::StereoSignal input, fixed_point::StereoBuffer output) final {
		auto input_view =
		    argon::vectorize::load_interleaved<q31_t, 2>(reinterpret_cast<std::span<const q31_t>&>(input));
		auto output_view = argon::vectorize::store_interleaved<q31_t, 2>(reinterpret_cast<std::span<q31_t>&>(output));

		auto input_it = input_view.begin();
		auto output_it = output_view.begin();
		for (; input_it != input_view.end() && output_it != output_view.end(); ++input_it, ++output_it) {
			auto [left_in, right_in] = *input_it;
			*output_it = this->render({left_in, right_in});
		}
	};
};

} // namespace deluge::dsp
