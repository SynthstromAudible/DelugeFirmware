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
#include "dsp_ng/core/mixer.hpp"

namespace deluge::dsp::mixer {

/// @brief UnityMixer is a mixer that simply adds two input samples together.
/// @tparam T The type of the input samples.
template <typename T>
struct UnityMixer : Mixer<Argon<T>>, Mixer<T> {
	/// @brief Mix two input samples into an output, treating the second input as a unity gain.
	/// @param input_a The first input sample to mix.
	/// @param input_b The second input sample to mix (unity gain).
	/// @return The mixed sample.
	[[gnu::always_inline]] T render(T input_a, T input_b) final { return input_a + input_b; }

	/// @brief Mix a vector of samples from two inputs into an output, treating the second input as a unity gain.
	/// @param input_a The first input sample to mix.
	/// @param input_b The second input sample to mix (unity gain).
	/// @return The mixed sample.
	[[gnu::always_inline]] Argon<T> render(Argon<T> input_a, Argon<T> input_b) final { return input_a + input_b; }
};

/// @brief UnityMixerProcessor is a processor that mixes input samples with a unity-gain input buffer.
template <typename T>
struct UnityMixerProcessor : Processor<Argon<T>>, Processor<T>, UnityMixer<T> {
	std::span<T>::iterator unity_input_iterator; ///< Iterator for the unity-input buffer

	/// @brief Constructor for UnityMixerProcessor.
	/// @param unity_input A span of input samples to mix with (unity gain).
	/// @details The iterator is initialized to the beginning of the unity input span.
	UnityMixerProcessor(std::span<T> unity_input)
	    : unity_input_iterator(unity_input.begin()) {} ///< Constructor to initialize the unity-input iterator

	/// @brief Process a block of samples by mixing the input with the unity-input buffer.
	/// @param input The input samples to process.
	/// @return The mixed output samples.
	[[gnu::always_inline]] Argon<T> render(Argon<T> input) final {
		auto output = UnityMixer<T>::render(input, Argon<T>::Load(&*unity_input_iterator));
		std::advance(unity_input_iterator, Argon<T>::lanes); ///< Advance the iterator for the next call
		return output;
	}

	/// @brief Process a single sample by mixing it with the current unity-input sample.
	/// @param input The input sample to process.
	/// @return The mixed output sample.
	[[gnu::always_inline]] T render(T input) final { return UnityMixer<T>::render(input, *unity_input_iterator++); }
};
} // namespace deluge::dsp::mixer
